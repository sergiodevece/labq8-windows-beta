#include "assist/AssistClient.h"

#include <juce_events/juce_events.h>

#include <cmath>
#include <mutex>

namespace iaq8::assist
{
namespace
{
constexpr int kMaxReplyBytes = 1 << 20;   // a reply is a few KB; more is treated as invalid
constexpr int kShutdownWaitMs = 10000;    // never reached in practice, see below

// Lifetime of the HTTPS transport
// --------------------------------
// The transport owns a 1-thread juce::ThreadPool (the requests) and one
// juce::Thread (the deadline watchdog); both are joined in the destructor and
// no thread is ever detached. A job owns copies of everything it uses (URL,
// headers, body, `done`) plus the shared Control block; it never touches the
// transport.
//
// Every request is cancellable at any point: the job registers its
// juce::WebInputStream in Control (under Control::lock) before connecting and
// unregisters it before destroying it. cancelActive() calls
// WebInputStream::cancel() on the registered stream under the same lock,
// which (macOS backend) wakes a pending connect and a blocked read at once
// and prevents a later connect. The lock makes "registered" mean "alive", and
// cancel() is called at most once per stream (JUCE's connect() resets its
// connection without a lock after a cancelled start, so a second cancel could
// race with that reset).
//
// ~HttpsTransport and cancelAll() cancel: queued jobs are removed without
// running and the running one ends within milliseconds, so the destructor's
// bounded wait returns at once and JUCE's forced thread stop is never
// reached. A cancelled request never calls `done`.
//
// Each request has an overall deadline (timeoutMs, 30 s by default). The read
// loop checks it between reads, but JUCE's macOS read() blocks until the whole
// buffer is filled or the body ends, so a server that drips its body could
// hold one read far past it; the watchdog thread therefore cancels the active
// stream when its deadline passes. Either way the request ends with status 0
// (unavailable).
struct Control
{
    std::mutex lock;
    bool shuttingDown = false;
    juce::uint64 generation = 0;              // cancelAll() increments it
    juce::WebInputStream* active = nullptr;   // the running job's stream, if any
    bool activeCancelled = false, activeTimedOut = false;
    double activeDeadlineMs = 0;              // Time::getMillisecondCounterHiRes() scale
    juce::WaitableEvent wakeWatchdog;

    void cancelActive() // call with `lock` held
    {
        if (active != nullptr && ! activeCancelled)
        {
            activeCancelled = true;
            active->cancel();
        }
    }
};

class DeadlineWatchdog final : public juce::Thread
{
public:
    explicit DeadlineWatchdog (std::shared_ptr<Control> c) : juce::Thread ("IAQ8 assist deadline"), control (std::move (c)) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            int waitMs = -1; // nothing active: sleep until woken
            {
                const std::lock_guard<std::mutex> l (control->lock);
                if (control->active != nullptr && ! control->activeCancelled)
                {
                    const auto remaining = control->activeDeadlineMs - juce::Time::getMillisecondCounterHiRes();
                    if (remaining <= 0.0)
                    {
                        control->activeTimedOut = true;
                        control->cancelActive();
                    }
                    else
                    {
                        waitMs = juce::jmax (1, (int) std::ceil (remaining));
                    }
                }
            }
            control->wakeWatchdog.wait (waitMs);
        }
    }

private:
    const std::shared_ptr<Control> control;
};

class HttpsJob final : public juce::ThreadPoolJob
{
public:
    HttpsJob (juce::URL u, juce::String h, juce::String b, int timeout,
              std::shared_ptr<Control> c, juce::uint64 gen, std::function<void (int, juce::String)> d)
        : juce::ThreadPoolJob ("IAQ8 assist request"),
          url (std::move (u)), headers (std::move (h)), body (std::move (b)), timeoutMs (timeout),
          control (std::move (c)), generation (gen), done (std::move (d))
    {
    }

    JobStatus runJob() override
    {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
        int status = 0;
        juce::MemoryBlock data;
        Outcome outcome = Outcome::finished;
        {
            juce::WebInputStream stream (url.withPOSTData (body), true);
            stream.withExtraHeaders (headers)
                  .withConnectionTimeout (timeoutMs)
                  .withNumRedirectsToFollow (0); // never forward the access code elsewhere

            if (! registerStream (stream, deadline))
                return jobHasFinished;

            if (stream.connect (nullptr))
            {
                status = stream.getStatusCode();
                char buffer[4096];
                while (! stream.isExhausted() && (int) data.getSize() <= kMaxReplyBytes)
                {
                    if (juce::Time::getMillisecondCounterHiRes() > deadline)
                        break; // reported as a time-out below
                    const auto n = stream.read (buffer, (int) sizeof (buffer));
                    if (n <= 0)
                        break;
                    data.append (buffer, (size_t) n);
                }
            }
            outcome = unregisterStream (deadline);
        } // the stream is destroyed only once nobody can cancel it

        if (outcome == Outcome::cancelled)
            return jobHasFinished; // nobody is waiting

        if (outcome == Outcome::timedOut)
        {
            status = 0; // too slow overall: unavailable
            data.reset();
        }
        if ((int) data.getSize() > kMaxReplyBytes)
            data.reset(); // an empty body: invalidResponse on 200
        done (status, juce::String::fromUTF8 (static_cast<const char*> (data.getData()), (int) data.getSize()));
        return jobHasFinished;
    }

private:
    enum class Outcome { finished, timedOut, cancelled };

    // Call with control->lock held.
    bool stale() const { return shouldExit() || control->shuttingDown || control->generation != generation; }

    bool registerStream (juce::WebInputStream& stream, double deadline)
    {
        {
            const std::lock_guard<std::mutex> l (control->lock);
            if (stale())
                return false;
            control->active = &stream;
            control->activeCancelled = false;
            control->activeTimedOut = false;
            control->activeDeadlineMs = deadline;
        }
        control->wakeWatchdog.signal();
        return true;
    }

    Outcome unregisterStream (double deadline)
    {
        const std::lock_guard<std::mutex> l (control->lock);
        const auto outcome = stale() ? Outcome::cancelled
                           : (control->activeTimedOut || juce::Time::getMillisecondCounterHiRes() > deadline) ? Outcome::timedOut
                           : control->activeCancelled ? Outcome::cancelled
                                                      : Outcome::finished;
        control->active = nullptr;
        control->activeCancelled = false;
        control->activeTimedOut = false;
        return outcome;
    }

    const juce::URL url;
    const juce::String headers, body;
    const int timeoutMs;
    const std::shared_ptr<Control> control;
    const juce::uint64 generation;
    const std::function<void (int, juce::String)> done;
};

class HttpsTransport final : public Transport
{
public:
    explicit HttpsTransport (int timeout)
        : timeoutMs (timeout),
          watchdog (control),
          pool (juce::ThreadPoolOptions {}.withThreadName ("IAQ8 assist").withNumberOfThreads (1))
    {
        watchdog.startThread();
    }

    ~HttpsTransport() override
    {
        {
            const std::lock_guard<std::mutex> l (control->lock);
            control->shuttingDown = true;
            control->cancelActive();
        }
        [[maybe_unused]] const bool stopped = pool.removeAllJobs (true, kShutdownWaitMs);
        jassert (stopped); // a cancelled request always ends within milliseconds

        watchdog.signalThreadShouldExit();
        control->wakeWatchdog.signal();
        watchdog.stopThread (kShutdownWaitMs);
    }

    void post (const juce::URL& url, const juce::String& headers, const juce::String& body,
               std::function<void (int, juce::String)> done) override
    {
        juce::uint64 generation = 0;
        {
            const std::lock_guard<std::mutex> l (control->lock);
            if (control->shuttingDown)
                return;
            generation = control->generation;
        }
        pool.addJob (new HttpsJob (url, headers, body, timeoutMs, control, generation, std::move (done)), true);
    }

    void cancelAll() override
    {
        {
            const std::lock_guard<std::mutex> l (control->lock);
            ++control->generation; // queued jobs of earlier generations end without connecting
            control->cancelActive();
        }
        pool.removeAllJobs (false, 0); // deletes the queued ones now; the running one ends by itself
    }

private:
    const int timeoutMs;
    const std::shared_ptr<Control> control = std::make_shared<Control>();
    DeadlineWatchdog watchdog;
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
    // Parsed by hand from the text: scheme "://" authority [/?#...]. Anything
    // a URL library could read differently is refused outright: a backslash
    // anywhere (NSURL percent-encodes it, so "http://localhost\\@evil.com"
    // would reach evil.com), user info, and any authority character outside
    // [A-Za-z0-9.-:[]].
    const auto text = server.toString (false).trim();
    const auto schemeEnd = text.indexOf ("://");
    if (schemeEnd <= 0 || hasControlCharacters (text) || text.containsAnyOf (" \\"))
        return false;
    const auto scheme = text.substring (0, schemeEnd).toLowerCase();
    auto authority = text.substring (schemeEnd + 3);
    for (auto stop : { '/', '?', '#' })
        if (const auto i = authority.indexOfChar (stop); i >= 0)
            authority = authority.substring (0, i);
    if (authority.isEmpty()
        || ! authority.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:[]"))
        return false; // also no user info: "http://[::1]@evil.com" is evil.com

    juce::String host;
    if (authority.startsWithChar ('['))
    {
        const auto close = authority.indexOfChar (']');
        if (close < 0)
            return false;
        host = authority.substring (0, close + 1);
        const auto rest = authority.substring (close + 1);
        if (rest.isNotEmpty() && ! (rest.startsWithChar (':') && rest.substring (1).containsOnly ("0123456789")))
            return false;
    }
    else
    {
        host = authority.upToFirstOccurrenceOf (":", false, false);
        const auto port = authority.fromFirstOccurrenceOf (":", false, false);
        if (host.containsAnyOf ("[]") || ! port.containsOnly ("0123456789"))
            return false;
    }
    host = host.toLowerCase();
    if (host.isEmpty())
        return false;

    if (scheme == "https")
        return true;
    if (scheme == "http")
        return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
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
    if (transport != nullptr)
        transport->cancelAll();
}

AssistClient::Callback AssistClient::holdOnMessageThread (std::function<void (Reply)> callback)
{
    return Callback (new std::function<void (Reply)> (std::move (callback)), [] (std::function<void (Reply)>* f)
    {
        // Released off the message thread (a dropped reply on the transport
        // thread): delete it there. Without a message loop, delete here.
        auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        if (mm != nullptr && ! mm->isThisTheMessageThread() && juce::MessageManager::callAsync ([f] { delete f; }))
            return;
        delete f;
    });
}

void AssistClient::deliver (Alive a, Callback callback, Reply reply)
{
    if (! a->load())
        return;
    juce::MessageManager::callAsync ([a, cb = std::move (callback), r = std::move (reply)]() mutable
    {
        if (a->load() && cb != nullptr && *cb) // checked on the message thread, where the client is destroyed
            (*cb) (std::move (r));
    });
}

void AssistClient::send (const TurnRequest& request, std::function<void (Reply)> onReplyOnMessageThread)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const auto url = server ? server() : juce::URL();
    const auto code = accessCode ? accessCode().trim() : juce::String();
    auto callback = holdOnMessageThread (std::move (onReplyOnMessageThread));

    if (code.isEmpty() || hasControlCharacters (code))
        return deliver (alive, std::move (callback), makeError (ErrorKind::noAccess));
    if (transport == nullptr || ! isAllowedServer (url))
        return deliver (alive, std::move (callback), makeError (ErrorKind::unavailable));

    const auto headers = "Content-Type: application/json\r\nAuthorization: Bearer " + code;
    transport->post (url.getChildURL ("v1/turn"), headers, toJson (request),
                     [a = alive, cb = std::move (callback)] (int status, juce::String body) mutable
                     {
                         // Background thread: only the shared flag, the body and the shared callback holder.
                         if (a->load())
                             deliver (a, std::move (cb), parseReply (status, body));
                     });
}
} // namespace iaq8::assist
