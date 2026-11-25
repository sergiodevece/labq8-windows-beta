#include "assist/AssistClient.h"

#include <juce_events/juce_events.h>

namespace iaq8::assist
{
namespace
{
constexpr int kMaxReplyBytes = 1 << 20;   // a reply is a few KB; more is treated as invalid
constexpr int kShutdownWaitMs = 2000;     // see ~HttpsTransport

// Lifetime of the HTTPS transport
// --------------------------------
// The transport owns a 1-thread juce::ThreadPool; no thread is ever detached.
// A job owns copies of everything it uses (URL, headers, body, `done`) plus a
// shared `shuttingDown` flag: it never touches the transport object.
//
// ~HttpsTransport sets shuttingDown, then removeAllJobs (true, 2 s): queued
// jobs are deleted without running, the running job is signalled. The job
// checks the flag (and shouldExit()) from the URL progress callback, which
// JUCE's macOS backend polls every 1 ms while waiting for the response, so a
// pending connection (up to the 30 s timeout) is abandoned within a few ms.
// The callback is not polled while the body is read; a small JSON body
// arrives with the headers, and NSURLSession's timeout (30 s without data)
// bounds a stalled read. If the job is still running after the 2 s, the
// pool's own destructor waits up to 5 s more and then stops the thread
// (JUCE's last resort). So destruction is always bounded (~7.5 s worst
// case, a few ms normally). A cancelled job never calls `done`.
class HttpsJob final : public juce::ThreadPoolJob
{
public:
    HttpsJob (juce::URL u, juce::String h, juce::String b, int timeout,
              std::shared_ptr<std::atomic<bool>> stopping, std::function<void (int, juce::String)> d)
        : juce::ThreadPoolJob ("IAQ8 assist request"),
          url (std::move (u)), headers (std::move (h)), body (std::move (b)), timeoutMs (timeout),
          shuttingDown (std::move (stopping)), done (std::move (d))
    {
    }

    JobStatus runJob() override
    {
        if (shouldStop())
            return jobHasFinished;

        int status = 0;
        auto stream = url.withPOSTData (body).createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                .withExtraHeaders (headers)
                .withConnectionTimeoutMs (timeoutMs)
                .withStatusCode (&status)
                .withNumRedirectsToFollow (0) // never forward the access code elsewhere
                .withProgressCallback ([this] (int, int) { return ! shouldStop(); }));

        juce::MemoryBlock data;
        if (stream != nullptr)
        {
            char buffer[4096];
            while (! shouldStop() && ! stream->isExhausted() && (int) data.getSize() <= kMaxReplyBytes)
            {
                const auto n = stream->read (buffer, (int) sizeof (buffer));
                if (n <= 0)
                    break;
                data.append (buffer, (size_t) n);
            }
        }

        if (shouldStop())
            return jobHasFinished; // cancelled: nobody is waiting

        if ((int) data.getSize() > kMaxReplyBytes)
            data.reset(); // an empty body: invalidResponse on 200
        done (status, juce::String::fromUTF8 (static_cast<const char*> (data.getData()), (int) data.getSize()));
        return jobHasFinished;
    }

private:
    bool shouldStop() const { return shouldExit() || shuttingDown->load(); }

    const juce::URL url;
    const juce::String headers, body;
    const int timeoutMs;
    const std::shared_ptr<std::atomic<bool>> shuttingDown;
    const std::function<void (int, juce::String)> done;
};

class HttpsTransport final : public Transport
{
public:
    explicit HttpsTransport (int timeout)
        : timeoutMs (timeout),
          pool (juce::ThreadPoolOptions {}.withThreadName ("IAQ8 assist").withNumberOfThreads (1))
    {
    }

    ~HttpsTransport() override
    {
        shuttingDown->store (true);
        pool.removeAllJobs (true, kShutdownWaitMs);
    }

    void post (const juce::URL& url, const juce::String& headers, const juce::String& body,
               std::function<void (int, juce::String)> done) override
    {
        if (! shuttingDown->load())
            pool.addJob (new HttpsJob (url, headers, body, timeoutMs, shuttingDown, std::move (done)), true);
    }

private:
    const int timeoutMs;
    const std::shared_ptr<std::atomic<bool>> shuttingDown = std::make_shared<std::atomic<bool>> (false);
    juce::ThreadPool pool; // last member: destroyed (joined) first
};

bool hasControlCharacters (const juce::String& s)
{
    for (auto p = s.getCharPointer(); ! p.isEmpty(); ++p)
        if (*p < 0x20 || *p == 0x7f)
            return true;
    return false;
}
} // namespace

std::unique_ptr<Transport> makeHttpsTransport (int timeoutMs)
{
    return std::make_unique<HttpsTransport> (timeoutMs);
}

bool isAllowedServer (const juce::URL& server)
{
    const auto scheme = server.getScheme().toLowerCase();
    const auto host = server.getDomain().toLowerCase();
    if (scheme == "https")
        return host.isNotEmpty();
    if (scheme == "http")
        return host == "localhost" || host == "127.0.0.1" || server.toString (false).startsWithIgnoreCase ("http://[::1]");
    return false;
}

AssistClient::AssistClient (std::unique_ptr<Transport> t, std::function<juce::URL()> s, std::function<juce::String()> a)
    : transport (std::move (t)), server (std::move (s)), accessCode (std::move (a)),
      alive (std::make_shared<std::atomic<bool>> (true))
{
}

AssistClient::~AssistClient()
{
    alive->store (false);
    transport.reset(); // cancels and joins (bounded) before the members it might use go away
}

void AssistClient::cancelAll()
{
    JUCE_ASSERT_MESSAGE_THREAD
    alive->store (false);
    alive = std::make_shared<std::atomic<bool>> (true);
}

void AssistClient::deliver (Alive a, std::function<void (Reply)> callback, Reply reply)
{
    if (! a->load())
        return;
    juce::MessageManager::callAsync ([a, cb = std::move (callback), r = std::move (reply)]() mutable
    {
        if (a->load() && cb) // checked on the message thread, where the client is destroyed
            cb (std::move (r));
    });
}

void AssistClient::send (const TurnRequest& request, std::function<void (Reply)> onReplyOnMessageThread)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const auto url = server ? server() : juce::URL();
    const auto code = accessCode ? accessCode().trim() : juce::String();

    if (code.isEmpty() || hasControlCharacters (code))
        return deliver (alive, std::move (onReplyOnMessageThread), makeError (ErrorKind::noAccess));
    if (transport == nullptr || ! isAllowedServer (url))
        return deliver (alive, std::move (onReplyOnMessageThread), makeError (ErrorKind::unavailable));

    const auto headers = "Content-Type: application/json\r\nAuthorization: Bearer " + code;
    transport->post (url.getChildURL ("v1/turn"), headers, toJson (request),
                     [a = alive, callback = std::move (onReplyOnMessageThread)] (int status, juce::String body) mutable
                     {
                         // Background thread: only the shared flag, the body and our own callback copy.
                         if (a->load())
                             deliver (a, std::move (callback), parseReply (status, body));
                     });
}
} // namespace iaq8::assist
