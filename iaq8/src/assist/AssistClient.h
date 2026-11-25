#pragma once

#include "assist/Contract.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>

namespace iaq8::assist
{
// Sends one request and calls `done` at most once, on a background thread,
// with the HTTP status (0 = network error or timeout) and the body. A
// cancelled request (cancelAll() or destruction) never calls `done`.
struct Transport
{
    virtual ~Transport() = default;
    virtual void post (const juce::URL& url, const juce::String& headers, const juce::String& body,
                       std::function<void (int status, juce::String body)> done) = 0;
    // Cancels every pending request; later posts work normally.
    virtual void cancelAll() {}
};

// HTTP(S) via juce::WebInputStream on a 1-thread juce::ThreadPool owned by
// the transport (no detached threads). Every request is cancellable while it
// connects and while it reads; the destructor and cancelAll() cancel and the
// destructor then joins (milliseconds). timeoutMs is both the connection
// timeout and the overall deadline of a request (status 0 when exceeded).
// See the .cpp for the lifetime argument.
std::unique_ptr<Transport> makeHttpsTransport (int timeoutMs = 30000);

// Message-thread object (create, send, cancel and destroy it there). Owner
// safe: a reply that arrives after cancelAll() or after destruction is
// dropped: the callback is never called and nothing freed is touched (the
// background side only holds a shared alive flag, the parsed reply and a
// shared holder of the callback). The callback object itself is always
// destroyed on the message thread (when the last holder is released
// elsewhere, its deletion is posted there), so it may capture message-thread
// state such as a Component::SafePointer.
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

    // Drops every reply still pending and cancels the transport's requests;
    // later sends work normally.
    void cancelAll();

    AssistClient (const AssistClient&) = delete;
    AssistClient& operator= (const AssistClient&) = delete;

private:
    using Alive = std::shared_ptr<std::atomic<bool>>;
    using Callback = std::shared_ptr<std::function<void (Reply)>>; // deleted on the message thread
    static Callback holdOnMessageThread (std::function<void (Reply)> callback);
    static void deliver (Alive alive, Callback callback, Reply reply);

    std::unique_ptr<Transport> transport;
    std::function<juce::URL()> server;
    std::function<juce::String()> accessCode;
    Alive alive;
};

// True for https URLs with a host, and for http on localhost / 127.0.0.1 /
// [::1] (any port). URLs with user info ("user@host") are refused.
bool isAllowedServer (const juce::URL& server);
} // namespace iaq8::assist
