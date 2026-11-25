#pragma once

#include "assist/Contract.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>

namespace iaq8::assist
{
// Sends one request and calls `done` exactly once, on a background thread,
// with the HTTP status (0 = network error, timeout or cancelled) and the
// body. Implementations may drop `done` (never call it) once they are being
// destroyed.
struct Transport
{
    virtual ~Transport() = default;
    virtual void post (const juce::URL& url, const juce::String& headers, const juce::String& body,
                       std::function<void (int status, juce::String body)> done) = 0;
};

// HTTPS via juce::URL on a 1-thread juce::ThreadPool owned by the transport
// (no detached threads). Lifetime, see the .cpp: the destructor cancels the
// pending jobs and waits a bounded time for the running one; replies after
// that are dropped.
std::unique_ptr<Transport> makeHttpsTransport (int timeoutMs = 30000);

// Message-thread object (create, send, cancel and destroy it there). Owner
// safe: a reply that arrives after cancelAll() or after destruction is
// dropped: the callback is never called and nothing freed is touched (the
// background side only holds a shared alive flag, the parsed reply and its
// own copy of the callback).
//
// send() reads `server` and `accessCode` on the calling thread, never logs
// the code, and posts to {server}/v1/turn. Without an access code it replies
// noAccess, and with a server that is not https (http is allowed only for
// localhost / 127.0.0.1, for development) it replies unavailable, both
// without touching the network. Every reply reaches the callback on the
// message thread, asynchronously (never inside send()).
class AssistClient
{
public:
    AssistClient (std::unique_ptr<Transport> transport, std::function<juce::URL()> server, std::function<juce::String()> accessCode);
    ~AssistClient();

    void send (const TurnRequest& request, std::function<void (Reply)> onReplyOnMessageThread);

    // Drops every reply still pending; later sends work normally.
    void cancelAll();

    AssistClient (const AssistClient&) = delete;
    AssistClient& operator= (const AssistClient&) = delete;

private:
    using Alive = std::shared_ptr<std::atomic<bool>>;
    static void deliver (Alive alive, std::function<void (Reply)> callback, Reply reply);

    std::unique_ptr<Transport> transport;
    std::function<juce::URL()> server;
    std::function<juce::String()> accessCode;
    Alive alive;
};

// True for https URLs, and for http on localhost / 127.0.0.1 / [::1].
bool isAllowedServer (const juce::URL& server);
} // namespace iaq8::assist
