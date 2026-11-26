#pragma once

#include "assist/AssistClient.h"
#include "assist/Contract.h"
#include "dsp/AssistEngine.h"

#include <juce_events/juce_events.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace iaq8
{
class Iaq8AudioProcessor;
}

namespace iaq8::assist
{
class AnalysisWorker;    // Session.cpp
struct AnalysisOutcome; // Session.cpp

// The conversation (spec §5.1). Message-thread object: create, use and
// destroy it there (the processor owns it; a timer polls the capture and the
// processor's state-load counter).
//
//  askSource  "¿Qué tienes en esta pista?" with the 7 sources as quick replies
//             (local, no server call). Free text there means Source::other
//             with that text as the note.
//  listening  The capture is armed; progress 0..1 of targetSeconds. When it
//             is full (or has >= 6 s and playback stalls, or the user says
//             anything with >= 6 s), the analysis runs on a background
//             thread owned by the session. An analysis that throws listens
//             again with a notice; a capture a host re-prepare emptied listens
//             again silently. apply() here keeps listening.
//  askGoal    "¿Qué te gustaría mejorar?" with quick replies by source (local).
//             Also the stage after a server question and after a restore.
//  waiting    One request in flight (say() is ignored until it answers).
//  proposal   A sanitised proposal is in the preview path, Listen::preview.
//  applied    The proposal was written to the parameters (undo available).
//  offline    The server is unavailable: notice "Sin conexión: tu
//             ecualización sigue activa"; the applied EQ (and a pending
//             proposal) are untouched. say ("Reintentar") resends the same
//             conversation; any other text is sent as a new message.
//             apply() leaves it for applied with the keys from before the
//             request (the retry key no longer applies).
//
// Requests carry the analysis of the last listening until a request that
// carried it is answered (question or proposal), so the analysis reaches the
// server exactly once per listening even if the first attempt fails. A
// correction sends the current (sanitised) proposal. Audio never leaves the
// plugin. Every change of the conversation is written to the processor's
// ASSISTANT state (setAssistantState semantics).
//
// Persisted ASSISTANT tree (version 1):
//   ASSISTANT { version, session_id, source (contract string; absent before
//               the source is chosen), source_note, stage }
//     MESSAGE { role: "assistant" | "user", text }
// A restore (construction, or a host state load noticed through the
// processor's counter) never brings back a proposal or the undo history: the
// stage becomes askSource without a source, listening if it was listening
// (the capture is armed again), otherwise askGoal.
class Session : private juce::Timer
{
public:
    enum class Stage { askSource, listening, askGoal, waiting, proposal, applied, offline };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void sessionChanged() = 0;
    };

    static constexpr int pollIntervalMs = 50;
    // Each request carries only the latest maxRequestMessages messages (the whole
    // history stays in the session and its saved state): the server gives the model a
    // trimmed transcript and never rejects a long session. User text (the input field
    // and the source note) is capped at maxTextChars characters.
    static constexpr size_t maxRequestMessages = 190;
    static constexpr int maxTextChars = 4000;
    static constexpr int stallMs = 1500; // no new signal for this long with >= 6 s: analyse what is there

    // Reads the conversation from processor.getAssistantState() (a fresh one
    // when empty). Never touches the network.
    Session (Iaq8AudioProcessor& processor, AssistClient& client);
    ~Session() override;

    void addListener (Listener*);
    void removeListener (Listener*);

    Stage getStage() const;
    const std::vector<Message>& getMessages() const;
    std::vector<std::string> getQuickReplies() const;  // <= 8, shown on the 8 keys
    double getListenProgress() const;                  // 0..1 of CaptureBuffer::targetSeconds
    // Seconds listened: while listening, the progress so far (progress x
    // targetSeconds); otherwise the seconds of the last analysed listening.
    // None before the first analysis, and after a restore or startOver (the
    // analysis is never saved).
    std::optional<double> getListenedSeconds() const;
    const std::string& getNotice() const { return notice; } // "" or a Spanish notice (offline, errors)
    std::optional<Source> getSource() const { return source; }
    dsp::Listen getListen() const { return listen; }
    bool canApply() const;
    bool canUndo() const;
    // Whether the server answered the last finished request (a question, a
    // proposal, or an error reply such as no access or the limit): the ONLINE
    // lamp. False at the start, when it was unavailable (offline), after
    // startOver and after a restore. Kept while a new request is in flight.
    // Not saved.
    bool lastRequestAnswered() const { return requestAnswered; }
    // Lines of the last sanitize() report (Spanish), for the debug editor.
    const std::vector<std::string>& getLimitAdjustments() const { return adjustments; }

    void chooseSource (Source, const std::string& note = {});
    void say (const std::string& userText);            // free text or a quick reply
    void listenAgain();
    void apply();
    void undo();
    void setListen (dsp::Listen);
    void startOver();

    // What the timer does: notices state loads, follows the capture. Public
    // so tests can drive it deterministically.
    void poll();

    // Testing hook: the analysis the worker runs (default analysis::analyse).
    // Pure; called on the worker thread. Set it on the message thread.
    using AnalyseFunction = std::function<analysis::AnalysisSummary (const std::vector<std::vector<float>>&, double, Source)>;
    void setAnalyseFunctionForTesting (AnalyseFunction f) { analyseFunction = std::move (f); }

    // Spanish UTF-8 texts (fixed by the brief).
    static const char* sourceLabel (Source);
    static std::vector<std::string> goalReplies (Source);
    static const char* retryReply();

    Session (const Session&) = delete;
    Session& operator= (const Session&) = delete;

private:
    void timerCallback() override { poll(); }

    // State loads: true when the conversation was reloaded.
    bool syncWithStateLoad();
    void loadFromProcessor();
    void startFresh();
    void persist();
    void changed();

    void addMessage (Message::Role role, std::string text);
    void beginListening();
    void startAnalysis();
    void analysisFinished (std::uint64_t forGeneration, AnalysisOutcome outcome);
    void listenFromScratch(); // re-arms the capture of the current listening
    void sendTurn();
    void handleReply (std::uint64_t forGeneration, Reply reply);
    void cancelWork();

    Iaq8AudioProcessor& processor;
    AssistClient& client;
    std::unique_ptr<AnalysisWorker> worker;
    juce::ListenerList<Listener> listeners;

    std::string sessionId;
    std::optional<Source> source;
    std::string sourceNote;
    std::vector<Message> messages;
    std::vector<std::string> quickReplies;
    std::string notice;
    std::vector<std::string> adjustments;
    Stage stage = Stage::askSource;
    Stage stageBeforeWaiting = Stage::askGoal;
    dsp::Listen listen = dsp::Listen::applied;

    std::vector<std::string> quickRepliesBeforeWaiting;

    std::optional<analysis::AnalysisSummary> analysis; // of the last listening
    bool analysisDelivered = false;                    // answered by the server
    bool inFlightCarriesAnalysis = false;
    bool requestAnswered = false; // lastRequestAnswered()
    std::optional<std::vector<ProposedBand>> currentProposal; // as previewed (sanitised)

    // Listening.
    bool armed = false;         // our arm() succeeded for this listening
    bool analysing = false;     // the worker has the job
    bool analysisRetry = false; // the worker could not copy (busy): poll() retries
    double lastCapturedSeconds = 0.0;
    double lastGrowthMs = 0.0;

    std::uint32_t stateLoadsSeen = 0;
    // Bumped whenever pending work is abandoned (state load, startOver, new
    // listening): a reply or an analysis result of an older generation is
    // dropped even if it was already on its way.
    std::uint64_t generation = 0;
    AnalyseFunction analyseFunction;
};
} // namespace iaq8::assist
