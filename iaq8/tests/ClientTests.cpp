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
    LQ_CHECK (! assist::isAllowedServer (juce::URL ("http://localhost.example.com")) && ! assist::isAllowedServer (juce::URL ("ftp://x"))
                  && ! assist::isAllowedServer (juce::URL ("")), "other schemes and hosts refused");
}

LQ_TEST ("client: https transport reports network errors as status 0 on a background thread")
{
    auto transport = assist::makeHttpsTransport (5000);
    juce::WaitableEvent finished;
    std::atomic<int> status { -1 };
    std::atomic<bool> onMessageThread { true };
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

LQ_TEST ("client: https transport destroyed with a request in flight returns quickly")
{
    // 10.255.255.1 is a non-routable private address: the connection hangs
    // until its timeout (30 s) unless cancelled. Offline, it fails at once;
    // both must end in a bounded destructor and no callback.
    auto transport = assist::makeHttpsTransport (30000);
    std::atomic<int> calls { 0 };
    transport->post (juce::URL ("https://10.255.255.1/v1/turn"), "Content-Type: application/json", "{}",
                     [&] (int, juce::String) { ++calls; });
    juce::Thread::sleep (200);
    const auto start = std::chrono::steady_clock::now();
    transport.reset();
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
    ctx.note (str ("destructor took ", (int) ms, " ms"));
    LQ_CHECK (ms < 2500.0, str ("bounded destructor, took ", ms, " ms"));
    LQ_CHECK (calls == 0, "no callback once destroyed");
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
