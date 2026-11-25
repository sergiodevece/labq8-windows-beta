#pragma once

#include "assist/AssistClient.h"
#include "assist/Contract.h"
#include "dsp/AssistEngine.h"

#include <juce_events/juce_events.h>

#include <cstdint>
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
class AnalysisWorker; // Session.cpp

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
//             thread owned by the session.
//  askGoal    "¿Qué te gustaría mejorar?" with quick replies by source (local).
//             Also the stage after a server question and after a restore.
//  waiting    One request in flight (say() is ignored until it answers).
//  proposal   A sanitised proposal is in the preview path, Listen::preview.
//  applied    The proposal was written to the parameters (undo available).
//  offline    The server is unavailable: notice "Sin conexión: tu
//             ecualización sigue activa"; the applied EQ (and a pending
//             proposal) are untouched. say ("Reintentar") resends the same
//             conversation; any other text is sent as a new message.
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
    const std::string& getNotice() const { return notice; } // "" or a Spanish notice (offline, errors)
    std::optional<Source> getSource() const { return source; }
    dsp::Listen getListen() const { return listen; }
    bool canApply() const;
    bool canUndo() const;
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
    void analysisFinished (std::optional<analysis::AnalysisSummary> summary);
    void sendTurn();
    void handleReply (Reply reply);
    void cancelWork();
    Stage restingStage() const; // where a non-network error returns to

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
    std::optional<std::vector<ProposedBand>> currentProposal; // as previewed (sanitised)

    // Listening.
    bool armed = false;         // our arm() succeeded for this listening
    bool analysing = false;     // the worker has the job
    bool analysisRetry = false; // the worker could not copy (busy): poll() retries
    double lastCapturedSeconds = 0.0;
    double lastGrowthMs = 0.0;

    std::uint32_t stateLoadsSeen = 0;
};
} // namespace iaq8::assist
