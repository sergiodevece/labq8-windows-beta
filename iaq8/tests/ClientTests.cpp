// Tests of AssistClient (fake transport: threading, lifetime, headers) and of
// the HTTPS transport's lifetime, plus Settings on a temporary file.
// The access code used here is a dummy and is never printed.

#include "TestHarness.h"

#include "assist/AssistClient.h"
#include "assist/Settings.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <sys/socket.h>

using namespace iaq8;
using labq8::test::str;

namespace
{
const char* kQuestion = "{\"kind\":\"question\",\"text\":\"\\u00bfY ahora?\",\"quick_replies\":[\"Vale\"]}";

struct Pending
{
    juce::URL url;
    juce::String headers, body;
    std::function<void (int, juce::String)> done;
};

// Shared with the test so it outlives the client (and the transport it owns).
struct FakeState
{
    std::mutex mutex;
    std::vector<Pending> pending;
    std::atomic<int> posts { 0 };

    Pending take (size_t i)
    {
        std::lock_guard<std::mutex> lock (mutex);
        return pending.at (i);
    }

    // Moves the request out, so the caller holds the only copy of `done`.
    Pending takeOut (size_t i)
    {
        std::lock_guard<std::mutex> lock (mutex);
        auto p = std::move (pending.at (i));
        pending.at (i).done = nullptr;
        return p;
    }
};

// A local HTTP server on 127.0.0.1 (no network needed) that misbehaves on
// purpose. One connection at a time, served on its own (joined) thread.
class LocalServer
{
public:
    enum class Mode
    {
        silent,     // accepts and never answers
        stallBody,  // sends the headers and part of the body, then nothing
        drip,       // sends the headers, then one body byte every 100 ms, forever
        reply       // answers 200 with a question
    };

    explicit LocalServer (Mode m) : mode (m)
    {
        ok = listener.createListener (0, "127.0.0.1");
        port = listener.getBoundPort();
        if (ok)
            thread = std::thread ([this] { run(); });
    }

    ~LocalServer()
    {
        stop = true;
        if (thread.joinable())
            thread.join();
        listener.close();
    }

    juce::URL url() const { return juce::URL ("http://127.0.0.1:" + juce::String (port) + "/v1/turn"); }
    juce::URL base() const { return juce::URL ("http://127.0.0.1:" + juce::String (port)); }

    bool ok = false;
    std::atomic<int> connections { 0 };
    std::atomic<bool> headersSent { false };
    std::atomic<int> bodyBytesSent { 0 };

private:
    void run()
    {
        while (! stop)
        {
            if (listener.waitUntilReady (true, 20) != 1)
                continue;
            std::unique_ptr<juce::StreamingSocket> client (listener.waitForNextConnection());
            if (client == nullptr)
                continue;
            ++connections;
            // Writing to a socket the client closed must fail, not raise
            // SIGPIPE (JUCE does not set this on accepted sockets).
            int one = 1;
            setsockopt (client->getRawSocketHandle(), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof (one));
            serve (*client);
        }
    }

    void serve (juce::StreamingSocket& client)
    {
        // Read the request (headers and the small body) without parsing it.
        char buffer[8192];
        for (int i = 0; i < 10 && ! stop; ++i)
            if (client.waitUntilReady (true, 20) == 1 && client.read (buffer, (int) sizeof (buffer), false) <= 0)
                break;

        const auto send = [&client] (const juce::String& text)
        {
            return client.write (text.toRawUTF8(), (int) text.getNumBytesAsUTF8()) > 0;
        };
        const juce::String headers = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nX-Content-Type-Options: nosniff\r\n"
                                     "Cache-Control: no-store\r\nContent-Length: ";
        if (mode == Mode::reply)
        {
            const juce::String body = "{\"kind\":\"question\",\"text\":\"Hola\",\"quick_replies\":[]}";
            send (headers + juce::String ((int) body.getNumBytesAsUTF8()) + "\r\n\r\n" + body);
            headersSent = true;
        }
        else if (mode == Mode::stallBody)
        {
            send (headers + "100000\r\n\r\n" + juce::String::repeatedString (" ", 600) + "{");
            headersSent = true;
            bodyBytesSent = 601;
        }
        else if (mode == Mode::drip)
        {
            send (headers + "100000\r\n\r\n" + juce::String::repeatedString (" ", 600));
            headersSent = true;
            bodyBytesSent = 600;
        }

        while (! stop)
        {
            if (mode == Mode::drip)
            {
                if (! send (" "))
                    break; // the client went away
                ++bodyBytesSent;
            }
            juce::Thread::sleep (100);
        }
    }

    const Mode mode;
    juce::StreamingSocket listener;
    int port = 0;
    std::atomic<bool> stop { false };
    std::thread thread;
};

struct FakeTransport final : assist::Transport
{
    explicit FakeTransport (std::shared_ptr<FakeState> s) : state (std::move (s)) {}

    void post (const juce::URL& url, const juce::String& headers, const juce::String& body,
               std::function<void (int, juce::String)> done) override
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        state->pending.push_back ({ url, headers, body, std::move (done) });
        ++state->posts;
    }

    std::shared_ptr<FakeState> state;
};

std::unique_ptr<assist::AssistClient> makeClient (std::shared_ptr<FakeState> state, juce::String server = "https://assist.example.test",
                                                  juce::String code = "dummy-code-7Q2")
{
    return std::make_unique<assist::AssistClient> (std::make_unique<FakeTransport> (std::move (state)),
                                                   [server] { return juce::URL (server); },
                                                   [code] { return code; });
}

assist::TurnRequest request()
{
    assist::TurnRequest r;
    r.sessionId = "s-1";
    r.source = assist::Source::voice;
    r.messages = { { assist::Message::Role::user, "hola" } };
    return r;
}

// Replies on a background thread, like a real transport.
void replyFromThread (const Pending& p, int status, juce::String body)
{
    std::thread ([done = p.done, status, body] { done (status, body); }).join();
}

// Pumps the message loop until `condition` or `ms` elapse.
template <typename Condition>
bool pumpUntil (Condition condition, int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (! condition() && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    return condition();
}

void pump (int ms) { pumpUntil ([] { return false; }, ms); }

struct Received
{
    std::atomic<int> calls { 0 };
    std::atomic<bool> onMessageThread { false };
    std::optional<assist::Reply> reply;
};

std::function<void (assist::Reply)> recorder (std::shared_ptr<Received> r)
{
    return [r] (assist::Reply reply)
    {
        r->onMessageThread = juce::MessageManager::getInstance()->isThisTheMessageThread();
        r->reply = std::move (reply);
        ++r->calls;
    };
}

std::optional<assist::ErrorKind> errorOf (const Received& r)
{
    if (! r.reply.has_value())
        return std::nullopt;
    const auto* e = std::get_if<assist::Error> (&*r.reply);
    return e != nullptr ? std::optional<assist::ErrorKind> (e->kind) : std::nullopt;
}
} // namespace

LQ_TEST ("client: reply arrives on the message thread, request and headers")
{
    auto state = std::make_shared<FakeState>();
    auto client = makeClient (state);
    auto got = std::make_shared<Received>();
    client->send (request(), recorder (got));

    LQ_CHECK (state->posts == 1, "one post");
    const auto p = state->take (0);
    LQ_CHECK (p.url.toString (false) == "https://assist.example.test/v1/turn", ("url " + p.url.toString (false)).toStdString());
    LQ_CHECK (p.headers.contains ("Authorization: Bearer dummy-code-7Q2"), "Authorization carries accessCode()"); // value never printed
    LQ_CHECK (p.headers.contains ("Content-Type: application/json"), "Content-Type");
    const auto body = juce::JSON::parse (p.body);
    LQ_CHECK (body["plugin_id"].toString() == "iaq8" && body["session_id"].toString() == "s-1", "body is the request JSON");
    LQ_CHECK (! p.body.contains ("dummy-code"), "the access code is not in the body");

    replyFromThread (p, 200, kQuestion);
    LQ_CHECK (got->calls == 0, "never delivered synchronously from the transport thread");
    LQ_CHECK (pumpUntil ([&] { return got->calls > 0; }, 2000), "reply delivered");
    LQ_CHECK (got->calls == 1 && got->onMessageThread, "once, on the message thread");
    LQ_CHECK (got->reply.has_value() && std::holds_alternative<assist::Question> (*got->reply), "parsed question");

    // Status errors go through the same path.
    auto got2 = std::make_shared<Received>();
    client->send (request(), recorder (got2));
    replyFromThread (state->take (1), 401, "{\"error\":\"no_access\"}");
    pumpUntil ([&] { return got2->calls > 0; }, 2000);
    LQ_CHECK (errorOf (*got2) == assist::ErrorKind::noAccess && got2->onMessageThread, "401 -> noAccess on the message thread");
}

LQ_TEST ("client: destroyed before the transport replies, the callback is never called")
{
    // 1. Destroyed before the transport answers.
    {
        auto state = std::make_shared<FakeState>();
        auto client = makeClient (state);
        auto got = std::make_shared<Received>();
        client->send (request(), recorder (got));
        const auto p = state->take (0);
        client.reset();
        replyFromThread (p, 200, kQuestion);
        pump (100);
        LQ_CHECK (got->calls == 0, "late reply dropped");
    }
    // 2. The transport answered (delivery queued) but the client dies first.
    {
        auto state = std::make_shared<FakeState>();
        auto client = makeClient (state);
        auto got = std::make_shared<Received>();
        client->send (request(), recorder (got));
        replyFromThread (state->take (0), 200, kQuestion);
        client.reset();
        pump (100);
        LQ_CHECK (got->calls == 0, "queued reply dropped");
    }
    // 3. Race: the transport answers on its thread while the client is destroyed
    //    on this one (also run under ThreadSanitizer, see the task report).
    int delivered = 0;
    for (int i = 0; i < 200; ++i)
    {
        auto state = std::make_shared<FakeState>();
        auto client = makeClient (state);
        auto got = std::make_shared<Received>();
        client->send (request(), recorder (got));
        std::thread t ([p = state->take (0)] { p.done (200, kQuestion); });
        if (i % 2 == 0)
            std::this_thread::yield();
        client.reset();
        t.join();
        pump (1);
        delivered += got->calls;
    }
    pump (50);
    LQ_CHECK (delivered == 0, str ("no callback after destruction, got ", delivered));
}

LQ_TEST ("client: a dropped callback is destroyed on the message thread")
{
    struct Sentinel
    {
        std::shared_ptr<std::atomic<int>> where; // 0 alive, 1 message thread, 2 other thread
        ~Sentinel() { where->store (juce::MessageManager::getInstance()->isThisTheMessageThread() ? 1 : 2); }
    };
    auto where = std::make_shared<std::atomic<int>> (0);
    auto state = std::make_shared<FakeState>();
    auto client = makeClient (state);
    {
        auto sentinel = std::make_shared<Sentinel>();
        sentinel->where = where;
        client->send (request(), [sentinel] (assist::Reply) {});
    }
    auto p = state->takeOut (0); // the transport's only copy of `done`
    client.reset();
    std::thread ([pending = std::move (p)]() mutable
    {
        pending.done (200, kQuestion);   // dropped: the client is gone
        pending.done = nullptr;          // last reference released on this thread
    }).join();
    LQ_CHECK (where->load() == 0, "not destroyed on the transport thread");
    pumpUntil ([&] { return where->load() != 0; }, 2000);
    LQ_CHECK (where->load() == 1, str ("destroyed on the message thread (", where->load(), ")"));
}

LQ_TEST ("client: cancelAll drops pending replies, later sends work")
{
    auto state = std::make_shared<FakeState>();
    auto client = makeClient (state);
    auto first = std::make_shared<Received>(), second = std::make_shared<Received>();
    client->send (request(), recorder (first));
    client->cancelAll();
    client->send (request(), recorder (second));
    replyFromThread (state->take (0), 200, kQuestion);
    replyFromThread (state->take (1), 200, kQuestion);
    pumpUntil ([&] { return second->calls > 0; }, 2000);
    pump (50);
    LQ_CHECK (first->calls == 0, "cancelled reply dropped");
    LQ_CHECK (second->calls == 1, "later send delivered");
}

LQ_TEST ("client: no access code or unsafe server, no network")
{
    {
        auto state = std::make_shared<FakeState>();
        auto client = makeClient (state, "https://assist.example.test", "   ");
        auto got = std::make_shared<Received>();
        client->send (request(), recorder (got));
        LQ_CHECK (got->calls == 0, "not delivered inside send()");
        pumpUntil ([&] { return got->calls > 0; }, 2000);
        LQ_CHECK (state->posts == 0 && errorOf (*got) == assist::ErrorKind::noAccess && got->onMessageThread, "empty code -> noAccess, nothing sent");
    }
    {
        auto state = std::make_shared<FakeState>();
        auto client = makeClient (state, "http://assist.example.test");
        auto got = std::make_shared<Received>();
        client->send (request(), recorder (got));
        pumpUntil ([&] { return got->calls > 0; }, 2000);
        LQ_CHECK (state->posts == 0 && errorOf (*got) == assist::ErrorKind::unavailable, "plain http to a remote host is refused");
    }
    LQ_CHECK (assist::isAllowedServer (juce::URL ("https://example.invalid")), "https allowed");
    LQ_CHECK (assist::isAllowedServer (juce::URL ("http://localhost:8787")) && assist::isAllowedServer (juce::URL ("http://127.0.0.1:8787/")),
              "http on localhost allowed (development)");
    LQ_CHECK (assist::isAllowedServer (juce::URL ("http://[::1]:8787/")) && assist::isAllowedServer (juce::URL ("https://assist.example.test:8443/x")),
              "[::1] with a port, https with a port and path");
    for (auto bad : { "http://localhost.example.com", "ftp://x", "", "http://[::1]@evil.com/", "http://localhost@evil.com",
                      "http://127.0.0.1:80@evil.com", "https://user@assist.example.test", "http://127.0.0.1.evil.com",
                      "http://[::1].evil.com/", "http://localhost:80x", "https://", "http://evil.com/?x=http://localhost",
                      "http://localhost\\@evil.com/", "http://127.0.0.1\\@evil.com/", "http://[::1]\\@evil.com/",
                      "https://example.com\\@evil.com/", "http://localhost/\\evil", "http://local%68ost/", "http://localhost:80:81" })
        LQ_CHECK (! assist::isAllowedServer (juce::URL (bad)), str ("refused: ", bad));
}

LQ_TEST ("client: https transport reports network errors as status 0 on a background thread")
{
    if (juce::SystemStats::getEnvironmentVariable ("IAQ8_NETWORK_TESTS", {}) != "1")
    {
        ctx.skip ("needs DNS; set IAQ8_NETWORK_TESTS=1");
        return;
    }
    juce::WaitableEvent finished;     // declared before the transport: it outlives any callback
    std::atomic<int> status { -1 };
    std::atomic<bool> onMessageThread { true };
    auto transport = assist::makeHttpsTransport (5000);
    transport->post (juce::URL ("https://example.invalid/v1/turn"), "Content-Type: application/json", "{}",
                     [&] (int s, juce::String)
                     {
                         onMessageThread = juce::MessageManager::getInstance()->isThisTheMessageThread();
                         status = s;
                         finished.signal();
                     });
    LQ_CHECK (finished.wait (15000), "done called");
    LQ_CHECK (status == 0, str ("status 0 for an unresolvable host, got ", status.load()));
    LQ_CHECK (! onMessageThread, "done runs on a background thread");
}

LQ_TEST ("client: transport end to end against a local server")
{
    LocalServer server (LocalServer::Mode::reply);
    LQ_CHECK (server.ok, "local server listening");
    auto client = std::make_unique<assist::AssistClient> (assist::makeHttpsTransport (5000), [&] { return server.base(); },
                                                          [] { return juce::String ("dummy-code-7Q2"); });
    auto got = std::make_shared<Received>();
    client->send (request(), recorder (got));
    LQ_CHECK (pumpUntil ([&] { return got->calls > 0; }, 5000), "reply delivered");
    LQ_CHECK (got->onMessageThread && got->reply.has_value() && std::holds_alternative<assist::Question> (*got->reply),
              "question parsed from the wire, on the message thread");
}

namespace
{
// Destroys a transport whose request is stuck against `mode`; returns ms.
double destroyStuck (labq8::test::Context& ctx, LocalServer::Mode mode, const char* what)
{
    LocalServer server (mode);
    LQ_CHECK (server.ok, "local server listening");
    std::atomic<int> calls { 0 };     // declared before the transport: it outlives any callback
    auto transport = assist::makeHttpsTransport (30000);
    transport->post (server.url(), "Content-Type: application/json", "{}", [&] (int, juce::String) { ++calls; });

    for (int i = 0; i < 300 && server.connections == 0; ++i)
        juce::Thread::sleep (10);
    LQ_CHECK (server.connections == 1, str (what, ": request reached the server"));
    juce::Thread::sleep (300); // the client is now waiting for the response or the rest of the body
    if (mode != LocalServer::Mode::silent)
        LQ_CHECK (server.headersSent.load(), str (what, ": headers sent"));

    const auto start = std::chrono::steady_clock::now();
    transport.reset();
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
    ctx.note (str (what, ": destructor took ", ms, " ms"));
    LQ_CHECK (ms < 1000.0, str (what, ": destroyed quickly, took ", ms, " ms"));
    LQ_CHECK (calls == 0, str (what, ": no callback once destroyed"));
    return ms;
}
} // namespace

LQ_TEST ("client: transport destroyed while waiting for the response returns quickly")
{
    destroyStuck (ctx, LocalServer::Mode::silent, "no response");
}

LQ_TEST ("client: transport destroyed while the body is stalled returns quickly")
{
    destroyStuck (ctx, LocalServer::Mode::stallBody, "stalled body");
}

LQ_TEST ("client: transport destroyed while the body drips returns quickly")
{
    destroyStuck (ctx, LocalServer::Mode::drip, "dripping body");
}

LQ_TEST ("client: a dripping body ends at the overall deadline as unavailable")
{
    LocalServer server (LocalServer::Mode::drip);
    LQ_CHECK (server.ok, "local server listening");
    juce::WaitableEvent finished;     // declared before the transport: it outlives any callback
    std::atomic<int> status { -1 };
    auto transport = assist::makeHttpsTransport (1500); // stands in for the 30 s deadline
    const auto start = std::chrono::steady_clock::now();
    transport->post (server.url(), "Content-Type: application/json", "{}", [&] (int s, juce::String)
    {
        status = s;
        finished.signal();
    });
    LQ_CHECK (finished.wait (10000), "done called");
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
    ctx.note (str ("dripping body ended after ", ms, " ms, ", server.bodyBytesSent.load(), " body bytes sent"));
    LQ_CHECK (status == 0 && assist::parseReply (status, {}).index() == 2, str ("status 0 (unavailable), got ", status.load()));
    LQ_CHECK (ms >= 1400.0 && ms < 4000.0, str ("ended at the deadline, ", ms, " ms"));
    LQ_CHECK (server.bodyBytesSent > 600, "the server kept sending (the idle timeout never fired)");
}

LQ_TEST ("client: cancelAll cancels a request in flight, the next one works")
{
    LocalServer silent (LocalServer::Mode::silent), replying (LocalServer::Mode::reply);
    juce::WaitableEvent finished;     // declared before the transport: it outlives any callback
    std::atomic<int> status { -1 }, cancelledCalls { 0 };
    auto transport = assist::makeHttpsTransport (30000);
    transport->post (silent.url(), "", "{}", [&] (int, juce::String) { ++cancelledCalls; });
    for (int i = 0; i < 300 && silent.connections == 0; ++i)
        juce::Thread::sleep (10);
    transport->cancelAll();

    const auto start = std::chrono::steady_clock::now();
    transport->post (replying.url(), "", "{}", [&] (int s, juce::String) { status = s; finished.signal(); });
    LQ_CHECK (finished.wait (5000) && status == 200, str ("next request answered, status ", status.load()));
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
    LQ_CHECK (ms < 2000.0, str ("not stuck behind the cancelled one, ", ms, " ms"));
    LQ_CHECK (cancelledCalls == 0, "cancelled request never calls done");
}

LQ_TEST ("settings: temporary file, defaults and access code")
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("iaq8-settings-test", "", false);
    juce::PropertiesFile::Options o;
    o.applicationName = "IAQ8";
    o.filenameSuffix = "settings";
    o.osxLibrarySubFolder = "Application Support";
    o.folderName = dir.getFullPathName();
    assist::Settings::setFileOptionsForTesting (o);
    const auto file = assist::Settings::fileOptions().getDefaultFile();
    LQ_CHECK (file.isAChildOf (dir), ("test file is in the temporary folder: " + file.getFullPathName()).toStdString());

    LQ_CHECK (assist::Settings::serverUrl().toString (false) == IAQ8_DEFAULT_SERVER_URL, "default server from CMake");
    LQ_CHECK (assist::Settings::accessCode().isEmpty(), "no access code by default");
    assist::Settings::setAccessCode ("dummy-code-7Q2");
    LQ_CHECK (assist::Settings::accessCode() == "dummy-code-7Q2", "access code stored"); // value never printed
    LQ_CHECK (file.existsAsFile(), "written to the temporary file");

    juce::PropertiesFile props (o);
    props.setValue ("server_url", "https://assist.example.test");
    props.saveIfNeeded();
    LQ_CHECK (assist::Settings::serverUrl().toString (false) == "https://assist.example.test", "server_url overrides the default");

    assist::Settings::setFileOptionsForTesting (std::nullopt);
    const auto real = assist::Settings::fileOptions();
    LQ_CHECK (real.folderName == "VitaPlug/IAQ8" && real.applicationName == "IAQ8", "real settings live in VitaPlug/IAQ8");
    dir.deleteRecursively();
}
