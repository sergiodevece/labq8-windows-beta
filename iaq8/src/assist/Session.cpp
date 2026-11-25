#include "assist/Session.h"

#include "analysis/TrackAnalyzer.h"
#include "assist/ProposalLimits.h"
#include "plugin/Iaq8Processor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace iaq8::assist
{
namespace
{
// Spanish UTF-8 texts of the local steps (spec §5.1).
constexpr const char* kAskSource = "¿Qué tienes en esta pista?";
constexpr const char* kListen = "Dale al play en una parte representativa; escucho unos segundos";
constexpr const char* kAskGoal = "¿Qué te gustaría mejorar?";
constexpr const char* kNeedMore = "Aún no he escuchado bastante: dale al play unos segundos más.";
constexpr const char* kNoSignal = "No he oído bastante señal: dale al play en una parte con sonido.";
constexpr const char* kAnalysisFailed = "No he podido analizar lo escuchado: dale al play otra vez.";
constexpr const char* kRetry = "Reintentar";

constexpr std::array<Source, 7> kSources { Source::voice, Source::acousticGuitar, Source::electricGuitar, Source::bass,
                                           Source::drums, Source::mix, Source::other };

constexpr const char* kMessageType = "MESSAGE";

const char* stageName (Session::Stage stage)
{
    switch (stage)
    {
        case Session::Stage::askSource: return "askSource";
        case Session::Stage::listening: return "listening";
        case Session::Stage::askGoal:   return "askGoal";
        case Session::Stage::waiting:   return "waiting";
        case Session::Stage::proposal:  return "proposal";
        case Session::Stage::applied:   return "applied";
        case Session::Stage::offline:   return "offline";
    }
    return "askGoal";
}

juce::String utf8 (const std::string& text) { return juce::String::fromUTF8 (text.data(), (int) text.size()); }

// Trimmed, and cut to Session::maxTextChars characters (whole code points).
std::string cappedText (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str()).trim().substring (0, Session::maxTextChars).trim().toStdString();
}

double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
} // namespace

//==============================================================================
// What one analysis job ended with.
struct AnalysisOutcome
{
    enum class Kind
    {
        busy,   // no copy could be taken (another reader, a prepare, re-armed, cancelled): retry
        empty,  // the copy had 0 samples (a host re-prepare emptied the capture): listen again
        failed, // the analysis threw (e.g. std::bad_alloc): listen again with a notice
        done
    };
    Kind kind = Kind::busy;
    std::optional<analysis::AnalysisSummary> summary; // with `done`
};

//==============================================================================
// Runs the analysis of one listening on its own std::thread (never detached).
//
// Lifetime: each job gets a shared `alive` flag. cancel() (message thread)
// clears it; the worker checks it between copy attempts and before posting,
// and the posted lambda checks it again on the message thread before calling
// `done`, so a result reaches the session only if the job was not cancelled
// by then. Cancelling and destroying happen on the message thread too, so
// that last check cannot race with the session going away. The destructor
// cancels and joins: a copy retry stops within one retry period, analyse()
// itself is not interruptible but bounded (at most 20 s of audio, about a
// second in Debug). `done` only captures the session pointer, which is never
// dereferenced off the message thread. Nothing thrown on the thread escapes
// it (an escaping exception would terminate the host): the copy and the
// analysis end as `failed`, and a result that cannot even be posted is
// dropped (the session then stays analysing until the user listens again or
// starts over).
class AnalysisWorker
{
public:
    using Done = std::function<void (AnalysisOutcome)>;

    static constexpr int retryMs = 10, maxAttempts = 100; // copyOut "busy" retried for up to ~1 s

    ~AnalysisWorker()
    {
        cancel();
        join();
    }

    // Message thread. Cancels any previous job and joins its thread first:
    // the session only starts a job after a new capture (>= 6 s), so the
    // previous one has finished by then.
    void start (analysis::CaptureBuffer& capture, Source source, Session::AnalyseFunction analyse, Done onDone)
    {
        cancel();
        join();
        auto flag = std::make_shared<std::atomic<bool>> (true);
        alive = flag;
        thread = std::thread ([&capture, source, flag, fn = std::move (analyse), done = std::move (onDone)]() noexcept
        {
            try
            {
                AnalysisOutcome result;
                try
                {
                    result = run (capture, source, fn, *flag);
                }
                catch (...)
                {
                    result = { AnalysisOutcome::Kind::failed, std::nullopt };
                }
                if (! flag->load())
                    return;
                juce::MessageManager::callAsync ([flag, done, outcome = std::move (result)]
                {
                    if (flag->load()) // message thread, where cancel() runs
                        done (outcome);
                });
            }
            catch (...)
            {
                // Could not post (out of memory): the result is lost, the host lives.
            }
        });
    }

    // Message thread: the pending result (if any) is dropped.
    void cancel()
    {
        if (alive != nullptr)
            alive->store (false);
        alive.reset();
    }

private:
    void join()
    {
        if (thread.joinable())
            thread.join();
    }

    static AnalysisOutcome run (analysis::CaptureBuffer& capture, Source source, const Session::AnalyseFunction& analyse,
                                const std::atomic<bool>& alive)
    {
        std::vector<std::vector<float>> channels;
        for (int attempt = 0; attempt < maxAttempts && alive.load(); ++attempt)
        {
            const auto sampleRate = capture.getSampleRate();
            const auto samples = capture.copyOut (channels);
            // Empty `channels` is "busy" (another copy or a prepare), not
            // "nothing captured"; a rate change means a prepare() slipped in.
            if (! channels.empty() && juce::exactlyEqual (capture.getSampleRate(), sampleRate) && sampleRate > 0.0)
            {
                if (samples == 0) // a prepare() emptied it
                    return { AnalysisOutcome::Kind::empty, std::nullopt };
                return { AnalysisOutcome::Kind::done,
                         analyse ? analyse (channels, sampleRate, source) : analysis::analyse (channels, sampleRate, source) };
            }
            if (capture.getState() == analysis::CaptureBuffer::State::capturing)
                return {};
            std::this_thread::sleep_for (std::chrono::milliseconds (retryMs));
        }
        return {};
    }

    std::thread thread;
    std::shared_ptr<std::atomic<bool>> alive;
};

//==============================================================================
const char* Session::sourceLabel (Source source)
{
    switch (source)
    {
        case Source::voice:          return "Voz";
        case Source::acousticGuitar: return "Guitarra acústica";
        case Source::electricGuitar: return "Guitarra eléctrica";
        case Source::bass:           return "Bajo";
        case Source::drums:          return "Batería";
        case Source::mix:            return "Mezcla completa";
        case Source::other:          return "Otro";
    }
    return "Otro";
}

std::vector<std::string> Session::goalReplies (Source source)
{
    switch (source)
    {
        case Source::voice: return { "Que se entienda", "Menos apagada", "Menos chillona", "Menos caja" };
        case Source::bass:  return { "Más definición", "Más fondo", "Menos retumbe" };
        case Source::mix:   return { "Más brillo", "Más claridad", "Menos barro" };
        case Source::acousticGuitar:
        case Source::electricGuitar:
        case Source::drums:
        case Source::other: break;
    }
    return { "Más claridad", "Menos dureza", "Más cuerpo" };
}

const char* Session::retryReply() { return kRetry; }

//==============================================================================
Session::Session (Iaq8AudioProcessor& p, AssistClient& c)
    : processor (p), client (c), worker (std::make_unique<AnalysisWorker>())
{
    loadFromProcessor();
    startTimer (pollIntervalMs);
}

Session::~Session()
{
    stopTimer();
    worker.reset();     // cancels and joins the analysis
    client.cancelAll(); // replies still pending never reach this object
}

void Session::addListener (Listener* l) { listeners.add (l); }
void Session::removeListener (Listener* l) { listeners.remove (l); }

Session::Stage Session::getStage() const { return stage; }
const std::vector<Message>& Session::getMessages() const { return messages; }
std::vector<std::string> Session::getQuickReplies() const { return quickReplies; }

double Session::getListenProgress() const
{
    if (stage != Stage::listening)
        return 0.0;
    if (analysing)
        return 1.0;
    if (! armed)
        return 0.0;
    const auto seconds = processor.getCapture().getCapturedSeconds();
    return std::clamp (seconds / analysis::CaptureBuffer::targetSeconds, 0.0, 1.0);
}

bool Session::canApply() const { return processor.hasProposal(); }
bool Session::canUndo() const { return processor.getUndoDepth() > 0; }

void Session::changed()
{
    listeners.call ([] (Listener& l) { l.sessionChanged(); });
}

//==============================================================================
bool Session::syncWithStateLoad()
{
    if (processor.getStateLoadCount() == stateLoadsSeen)
        return false;

    processor.invalidateIfStateLoaded(); // proposal and undo history of the old state
    // Unconditionally too: the counter may already have been consumed by a
    // processor call made between our last check and this one (e.g. a reply
    // handler's setProposal right after a load), which then published the
    // OLD conversation's proposal. Nothing of the old conversation survives.
    processor.clearProposal();
    processor.setListen (dsp::Listen::applied);
    cancelWork();
    loadFromProcessor();
    changed();
    return true;
}

void Session::cancelWork()
{
    ++generation;
    worker->cancel();
    client.cancelAll();
    processor.getCapture().cancel();
    analysing = false;
    armed = false;
    analysisRetry = false;
}

void Session::loadFromProcessor()
{
    std::uint32_t loads = 0;
    const auto tree = processor.getAssistantState (loads);
    stateLoadsSeen = loads;

    messages.clear();
    quickReplies.clear();
    notice.clear();
    adjustments.clear();
    source.reset();
    sourceNote.clear();
    analysis.reset();
    analysisDelivered = false;
    inFlightCarriesAnalysis = false;
    currentProposal.reset();
    analysing = false;
    armed = false;
    analysisRetry = false;
    listen = dsp::Listen::applied; // setStateInformation forced it; the proposal is gone

    if (tree.getNumChildren() == 0 && ! tree.hasProperty ("source") && ! tree.hasProperty ("session_id"))
    {
        startFresh();
        return;
    }

    sessionId = tree.getProperty ("session_id").toString().toStdString();
    if (sessionId.empty())
        sessionId = juce::Uuid().toDashedString().toStdString();
    source = sourceFromString (tree.getProperty ("source").toString().toStdString());
    if (source == Source::other)
        sourceNote = cappedText (tree.getProperty ("source_note").toString().toStdString());

    for (const auto& child : tree)
    {
        if (! child.hasType (kMessageType))
            continue;
        const auto role = child.getProperty ("role").toString();
        if (role != "assistant" && role != "user")
            continue;
        messages.push_back ({ role == "user" ? Message::Role::user : Message::Role::assistant,
                              child.getProperty ("text").toString().toStdString() });
    }

    if (! source.has_value())
    {
        stage = Stage::askSource;
        quickReplies.assign (kSources.size(), {});
        std::transform (kSources.begin(), kSources.end(), quickReplies.begin(), sourceLabel);
        if (messages.empty())
            addMessage (Message::Role::assistant, kAskSource);
    }
    else if (tree.getProperty ("stage").toString() == stageName (Stage::listening))
    {
        stage = Stage::listening; // listen again from scratch (the capture is not saved)
        armed = processor.getCapture().arm();
        lastCapturedSeconds = 0.0;
        lastGrowthMs = nowMs();
    }
    else
    {
        stage = Stage::askGoal;
        quickReplies = goalReplies (*source);
    }
    persist();
}

void Session::startFresh()
{
    sessionId = juce::Uuid().toDashedString().toStdString();
    source.reset();
    sourceNote.clear();
    messages.clear();
    addMessage (Message::Role::assistant, kAskSource);
    stage = Stage::askSource;
    quickReplies.assign (kSources.size(), {});
    std::transform (kSources.begin(), kSources.end(), quickReplies.begin(), sourceLabel);
    persist();
}

void Session::persist()
{
    juce::ValueTree tree (Iaq8AudioProcessor::assistantType);
    tree.setProperty ("version", 1, nullptr);
    tree.setProperty ("session_id", utf8 (sessionId), nullptr);
    if (source.has_value())
        tree.setProperty ("source", toString (*source), nullptr);
    tree.setProperty ("source_note", utf8 (sourceNote), nullptr);
    tree.setProperty ("stage", stageName (stage), nullptr);
    for (const auto& m : messages)
    {
        juce::ValueTree child (kMessageType);
        child.setProperty ("role", m.role == Message::Role::user ? "user" : "assistant", nullptr);
        child.setProperty ("text", utf8 (m.text), nullptr);
        tree.appendChild (child, nullptr);
    }
    // Refused when a state was loaded since we read ours: the loaded
    // conversation wins and the next poll() reloads it.
    processor.setAssistantStateUnlessLoaded (tree, stateLoadsSeen);
}

void Session::addMessage (Message::Role role, std::string text)
{
    messages.push_back ({ role, std::move (text) });
}

//==============================================================================
void Session::chooseSource (Source newSource, const std::string& note)
{
    syncWithStateLoad();
    if (stage == Stage::waiting)
        client.cancelAll(); // the source changed: that answer no longer applies

    source = newSource;
    const auto trimmed = cappedText (note);
    sourceNote = newSource == Source::other ? trimmed : std::string();
    addMessage (Message::Role::user, sourceNote.empty() ? std::string (sourceLabel (newSource))
                                                        : std::string (sourceLabel (newSource)) + ": " + sourceNote);
    beginListening();
    persist();
    changed();
}

void Session::beginListening()
{
    ++generation; // an analysis or a reply still on its way belongs to before
    worker->cancel();
    analysing = false;
    analysisRetry = false;
    auto& capture = processor.getCapture();
    capture.cancel();
    armed = capture.arm(); // refused while a copy/prepare runs: poll() retries
    lastCapturedSeconds = 0.0;
    lastGrowthMs = nowMs();

    addMessage (Message::Role::assistant, kListen);
    stage = Stage::listening;
    quickReplies.clear();
    notice.clear();
}

void Session::listenAgain()
{
    syncWithStateLoad();
    if (! source.has_value())
        return;
    if (stage == Stage::waiting)
        client.cancelAll();
    beginListening();
    persist();
    changed();
}

void Session::poll()
{
    if (syncWithStateLoad() || stage != Stage::listening)
        return;

    auto& capture = processor.getCapture();
    if (analysisRetry && capture.getState() != analysis::CaptureBuffer::State::capturing)
    {
        analysisRetry = false;
        startAnalysis();
        return;
    }
    if (analysing)
        return;

    if (! armed)
    {
        armed = capture.arm();
        lastCapturedSeconds = 0.0;
        lastGrowthMs = nowMs();
        changed();
        return;
    }

    switch (capture.getState())
    {
        case analysis::CaptureBuffer::State::done:
            startAnalysis();
            return;

        case analysis::CaptureBuffer::State::idle: // prepare() reset it: start over
            armed = capture.arm();
            lastCapturedSeconds = 0.0;
            lastGrowthMs = nowMs();
            break;

        case analysis::CaptureBuffer::State::capturing:
        {
            const auto seconds = capture.getCapturedSeconds();
            if (seconds > lastCapturedSeconds)
            {
                lastCapturedSeconds = seconds;
                lastGrowthMs = nowMs();
            }
            else if (capture.hasEnough() && nowMs() - lastGrowthMs >= stallMs)
            {
                startAnalysis(); // playback stopped with enough signal
                return;
            }
            break;
        }
    }
    changed(); // progress
}

void Session::startAnalysis()
{
    auto& capture = processor.getCapture();
    capture.cancel(); // capturing -> idle; what was captured stays readable
    analysing = true;
    worker->start (capture, source.value_or (Source::other), analyseFunction,
                   [this, g = generation] (AnalysisOutcome outcome) { analysisFinished (g, std::move (outcome)); });
    changed();
}

void Session::listenFromScratch()
{
    armed = processor.getCapture().arm();
    lastCapturedSeconds = 0.0;
    lastGrowthMs = nowMs();
}

void Session::analysisFinished (std::uint64_t forGeneration, AnalysisOutcome outcome)
{
    if (syncWithStateLoad() || forGeneration != generation || stage != Stage::listening || ! analysing)
        return;
    analysing = false;

    switch (outcome.kind)
    {
        case AnalysisOutcome::Kind::busy:
            analysisRetry = true; // the capture was busy: poll() tries again
            return;

        case AnalysisOutcome::Kind::empty: // nothing to analyse, and nothing the user did wrong
            listenFromScratch();
            changed();
            return;

        case AnalysisOutcome::Kind::failed:
            notice = kAnalysisFailed;
            listenFromScratch();
            changed();
            return;

        case AnalysisOutcome::Kind::done:
            break;
    }

    if (! outcome.summary.has_value() || ! outcome.summary->enoughSignal)
    {
        notice = kNoSignal;
        listenFromScratch();
        changed();
        return;
    }

    analysis = std::move (*outcome.summary);
    analysisDelivered = false;
    notice.clear();
    addMessage (Message::Role::assistant, kAskGoal);
    stage = Stage::askGoal;
    quickReplies = goalReplies (source.value_or (Source::other));
    persist();
    changed();
}

//==============================================================================
void Session::say (const std::string& userText)
{
    syncWithStateLoad();
    const auto text = cappedText (userText);
    if (text.empty())
        return;

    switch (stage)
    {
        case Stage::askSource:
        {
            for (const auto s : kSources)
                if (text == sourceLabel (s))
                    return chooseSource (s);
            return chooseSource (Source::other, text);
        }

        case Stage::listening:
            if (analysing)
                return;
            // Only this listening's capture: while arm() has not succeeded
            // the buffer still holds the previous one.
            if (armed && processor.getCapture().hasEnough())
                return startAnalysis();
            notice = kNeedMore;
            changed();
            return;

        case Stage::waiting:
            return; // one request at a time

        case Stage::offline:
            if (text == kRetry && ! messages.empty() && messages.back().role == Message::Role::user)
                return sendTurn(); // the same conversation again
            break;

        case Stage::askGoal:
        case Stage::proposal:
        case Stage::applied:
            break;
    }

    addMessage (Message::Role::user, text);
    sendTurn();
}

void Session::sendTurn()
{
    TurnRequest request;
    request.sessionId = sessionId;
    request.source = source.value_or (Source::other);
    request.sourceNote = sourceNote;
    // Only the latest messages: the server's transcript limit (the history stays here).
    const auto first = messages.size() > maxRequestMessages ? messages.size() - maxRequestMessages : 0;
    request.messages.assign (messages.begin() + (std::ptrdiff_t) first, messages.end());
    if (analysis.has_value() && ! analysisDelivered)
        request.analysis = analysis;
    request.applied = processor.getAppliedBands();
    if (currentProposal.has_value() && processor.hasProposal())
        request.proposal = currentProposal;
    inFlightCarriesAnalysis = request.analysis.has_value();

    if (stage != Stage::offline) // offline keeps where the first attempt came from
    {
        stageBeforeWaiting = stage == Stage::proposal || stage == Stage::applied ? stage : Stage::askGoal;
        quickRepliesBeforeWaiting = quickReplies;
    }
    stage = Stage::waiting;
    quickReplies.clear();
    notice.clear();

    // The callback captures `this`: every path that could outlive it
    // (destruction, startOver, a state load, a new listening) calls
    // client.cancelAll(), after which it is never called.
    client.send (request, [this, g = generation] (Reply reply) { handleReply (g, std::move (reply)); });
    persist();
    changed();
}

void Session::handleReply (std::uint64_t forGeneration, Reply reply)
{
    if (syncWithStateLoad() || forGeneration != generation || stage != Stage::waiting)
        return;

    if (const auto* error = std::get_if<Error> (&reply))
    {
        notice = error->userMessage;
        if (error->kind == ErrorKind::unavailable)
        {
            stage = Stage::offline;
            quickReplies = { kRetry };
        }
        else
        {
            stage = stageBeforeWaiting;
            if (stage == Stage::proposal && ! processor.hasProposal())
                stage = Stage::askGoal;
            quickReplies = quickRepliesBeforeWaiting;
        }
        persist();
        changed();
        return;
    }

    if (inFlightCarriesAnalysis)
        analysisDelivered = true; // the server has it: later requests go without
    notice.clear();

    if (const auto* question = std::get_if<Question> (&reply))
    {
        addMessage (Message::Role::assistant, question->text);
        quickReplies = question->quickReplies;
        stage = Stage::askGoal;
    }
    else if (const auto* proposal = std::get_if<Proposal> (&reply))
    {
        LimitReport report;
        const auto bands = sanitize (*proposal, source.value_or (Source::other), analysis ? &*analysis : nullptr, report);
        adjustments = report.adjustments;

        // What the user hears, as the server will see it in a correction.
        std::vector<ProposedBand> previewed;
        for (size_t k = 0; k < std::min (proposal->bands.size(), bands.size()); ++k)
        {
            ProposedBand band = proposal->bands[k];
            band.settings = bands[k];
            if (bands[k].bypassed && ! proposal->bands[k].settings.bypassed) // discarded by the limits
            {
                band.candidateId.clear();
                band.reason.clear();
            }
            previewed.push_back (std::move (band));
        }
        currentProposal = std::move (previewed);

        processor.setProposal (bands);
        listen = dsp::Listen::preview;
        processor.setListen (listen);
        addMessage (Message::Role::assistant, proposal->explanation);
        quickReplies = proposal->quickReplies;
        stage = Stage::proposal;
    }
    persist();
    changed();
}

//==============================================================================
void Session::apply()
{
    syncWithStateLoad();
    if (! processor.applyProposal())
        return;

    currentProposal.reset();
    listen = dsp::Listen::applied;
    processor.setListen (listen);
    switch (stage)
    {
        case Stage::waiting:   // the reply decides the next stage; an error comes back here
            stageBeforeWaiting = Stage::applied;
            break;
        case Stage::listening: // the listening goes on and ends in askGoal as usual
            break;
        case Stage::offline:   // leaving it: its retry key no longer applies
            quickReplies = quickRepliesBeforeWaiting;
            stage = Stage::applied;
            break;
        case Stage::askSource:
        case Stage::askGoal:
        case Stage::proposal:
        case Stage::applied:
            stage = Stage::applied;
            break;
    }
    persist();
    changed();
}

void Session::undo()
{
    syncWithStateLoad();
    if (! processor.undoApply())
        return;

    if (stage == Stage::applied && processor.getUndoDepth() == 0)
        stage = Stage::askGoal;
    persist();
    changed();
}

void Session::setListen (dsp::Listen newListen)
{
    syncWithStateLoad();
    if (newListen == dsp::Listen::preview && ! processor.hasProposal())
        newListen = dsp::Listen::applied;
    listen = newListen;
    processor.setListen (listen);
    changed();
}

void Session::startOver()
{
    syncWithStateLoad();
    cancelWork();
    processor.clearProposal(); // the applied EQ and its undo history stay
    listen = dsp::Listen::applied;
    processor.setListen (listen);
    analysis.reset();
    analysisDelivered = false;
    inFlightCarriesAnalysis = false;
    currentProposal.reset();
    notice.clear();
    adjustments.clear();
    startFresh();
    changed();
}
} // namespace iaq8::assist
