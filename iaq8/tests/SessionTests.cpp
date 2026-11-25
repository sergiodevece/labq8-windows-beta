// Tests of the conversation Session (spec §5.1, §5.4) with a fake transport
// and a capture filled by the test. No network, no real user settings: the
// client gets its server and a dummy access code from lambdas.

// Exact comparisons of applied values are intentional (untouched parameters).
#pragma clang diagnostic ignored "-Wfloat-equal"

#include "TestHarness.h"

#include "assist/Session.h"
#include "assist/Settings.h"
#include "plugin/DebugEditor.h"
#include "plugin/Iaq8Processor.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace iaq8;
using assist::Session;
using Stage = assist::Session::Stage;
using labq8::dsp::FilterType;
using labq8::test::str;

namespace
{
constexpr double kFs = 48000.0;
constexpr int kBlock = 512;

const std::string kAskSource = "¿Qué tienes en esta pista?";
const std::string kListen = "Dale al play en una parte representativa; escucho unos segundos";
const std::string kAskGoal = "¿Qué te gustaría mejorar?";
const std::string kOffline = "Sin conexión: tu ecualización sigue activa";

const char* kProposal1 =
    "{\"kind\":\"proposal\",\"explanation\":\"Bajo un poco la zona de 1 kHz y doy aire.\","
    "\"bands\":[{\"type\":\"bell\",\"freq_hz\":1000,\"gain_db\":-4,\"q\":1.4,\"bypass\":false,"
    "\"candidate_id\":null,\"reason\":\"Menos dureza\"},"
    "{\"type\":\"high_shelf\",\"freq_hz\":8000,\"gain_db\":2,\"q\":0.7,\"reason\":\"Aire\"}],"
    "\"quick_replies\":[\"Menos agudos\",\"M\\u00e1s suave\"]}";

const char* kProposal2 =
    "{\"kind\":\"proposal\",\"explanation\":\"Quito el realce de agudos.\","
    "\"bands\":[{\"type\":\"bell\",\"freq_hz\":1000,\"gain_db\":-4,\"q\":1.4,\"reason\":\"Menos dureza\"}],"
    "\"quick_replies\":[\"Vale\"]}";

const char* kQuestion = "{\"kind\":\"question\",\"text\":\"\\u00bfSuena como dentro de una caja?\",\"quick_replies\":[\"S\\u00ed\",\"No\"]}";

struct Pending
{
    juce::String body;
    std::function<void (int, juce::String)> done;
};

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

    void post (const juce::URL&, const juce::String&, const juce::String& body,
               std::function<void (int, juce::String)> done) override
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        state->pending.push_back ({ body, std::move (done) });
        ++state->posts;
    }

    std::shared_ptr<FakeState> state;
};

template <typename Condition>
bool pumpUntil (Condition condition, int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (! condition() && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    return condition();
}

void pump (int ms) { pumpUntil ([] { return false; }, ms); }

// Replies on a background thread, like a real transport.
void reply (FakeState& state, size_t i, int status, juce::String body)
{
    const auto p = state.take (i);
    std::thread ([done = p.done, status, body] { done (status, body); }).join();
}

struct Rig
{
    std::shared_ptr<FakeState> state = std::make_shared<FakeState>();
    std::unique_ptr<Iaq8AudioProcessor> p;

    Rig()
    {
        p = std::make_unique<Iaq8AudioProcessor> (std::make_unique<assist::AssistClient> (
            std::make_unique<FakeTransport> (state),
            [] { return juce::URL ("https://assist.example.test"); },
            [] { return juce::String ("dummy-code-7Q2"); }));
        p->setRateAndBufferSizeDetails (kFs, kBlock);
        p->prepareToPlay (kFs, kBlock);
    }

    Session& s() { return p->getSession(); }

    juce::var body (size_t i) { return juce::JSON::parse (state->take (i).body); }
};

// Writes `seconds` of stereo noise (about -15 dBFS RMS) straight into the capture.
void fillCapture (Iaq8AudioProcessor& p, double seconds, int seed = 7)
{
    juce::Random random (seed);
    std::vector<float> l ((size_t) kBlock), r ((size_t) kBlock);
    const float* channels[2] { l.data(), r.data() };
    const auto blocks = (int) std::ceil (seconds * kFs / kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            l[(size_t) i] = 0.3f * (random.nextFloat() * 2.0f - 1.0f);
            r[(size_t) i] = 0.3f * (random.nextFloat() * 2.0f - 1.0f);
        }
        p.getCapture().write (channels, 2, kBlock);
    }
}

std::vector<std::string> sourceLabels()
{
    return { "Voz", "Guitarra acústica", "Guitarra eléctrica", "Bajo", "Batería", "Mezcla completa", "Otro" };
}

bool sameMessages (const juce::var& array, const std::vector<assist::Message>& messages)
{
    if (! array.isArray() || array.size() != (int) messages.size())
        return false;
    for (size_t i = 0; i < messages.size(); ++i)
    {
        const auto& m = array[(int) i];
        const auto role = messages[i].role == assist::Message::Role::user ? "user" : "assistant";
        if (m["role"].toString() != role || m["text"].toString().toStdString() != messages[i].text)
            return false;
    }
    return true;
}

// askSource -> "Voz" -> 12.5 s captured -> askGoal (local, no post).
void listenAsVoice (labq8::test::Context& ctx, Rig& rig)
{
    rig.s().say ("Voz");
    LQ_CHECK (rig.s().getStage() == Stage::listening, "listening after choosing the source");
    fillCapture (*rig.p, 12.5);
    rig.s().poll();
    LQ_CHECK (pumpUntil ([&] { return rig.s().getStage() == Stage::askGoal; }, 20000), "analysis done: askGoal");
}

// ... -> "Que se entienda" -> proposal 1 in preview.
void reachProposal (labq8::test::Context& ctx, Rig& rig)
{
    listenAsVoice (ctx, rig);
    rig.s().say ("Que se entienda");
    const auto index = (size_t) rig.state->posts.load() - 1;
    reply (*rig.state, index, 200, kProposal1);
    LQ_CHECK (pumpUntil ([&] { return rig.s().getStage() == Stage::proposal; }, 2000), "proposal stage");
}
} // namespace

LQ_TEST ("session: full flow, local questions, analysis request, preview and apply")
{
    Rig rig;
    auto& s = rig.s();

    LQ_CHECK (s.getStage() == Stage::askSource, "opens asking for the source");
    LQ_CHECK (s.getMessages().size() == 1 && s.getMessages()[0].role == assist::Message::Role::assistant
                  && s.getMessages()[0].text == kAskSource,
              "first local question");
    LQ_CHECK (s.getQuickReplies() == sourceLabels(), "the 7 sources as quick replies");

    s.say ("Voz");
    LQ_CHECK (s.getStage() == Stage::listening, "listening");
    LQ_CHECK (s.getSource() == assist::Source::voice, "source voice");
    LQ_CHECK (rig.p->getCapture().getState() == analysis::CaptureBuffer::State::capturing, "capture armed");
    LQ_CHECK (s.getMessages().size() == 3 && s.getMessages()[1].text == "Voz" && s.getMessages()[2].text == kListen,
              "user source and listening prompt");

    fillCapture (*rig.p, 6.0);
    s.poll();
    LQ_CHECK (std::abs (s.getListenProgress() - 0.5) < 0.02, str ("progress at 6 s: ", s.getListenProgress()));
    fillCapture (*rig.p, 6.5, 11);
    s.poll();
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::askGoal; }, 20000), "analysis done: askGoal");
    LQ_CHECK (s.getMessages().back().text == kAskGoal, "second local question");
    LQ_CHECK ((s.getQuickReplies() == std::vector<std::string> { "Que se entienda", "Menos apagada", "Menos chillona", "Menos caja" }),
              "voice goals");
    LQ_CHECK (rig.state->posts == 0, "no server call before the goal");

    s.say ("Que se entienda");
    LQ_CHECK (s.getStage() == Stage::waiting, "waiting");
    LQ_CHECK (rig.state->posts == 1, "one request");
    const auto body = rig.body (0);
    LQ_CHECK (body["source"].toString() == "voice" && body["source_note"].toString().isEmpty(), "source");
    LQ_CHECK (body["session_id"].toString().length() >= 32, "session id");
    LQ_CHECK (sameMessages (body["messages"], s.getMessages()) && s.getMessages().size() == 5
                  && s.getMessages().back().text == "Que se entienda",
              "messages: the whole conversation");
    LQ_CHECK (body["analysis"].isObject() && (int) body["analysis"]["version"] == 1
                  && (bool) body["analysis"]["enough_signal"]
                  && std::abs ((double) body["analysis"]["seconds"] - 12.0) < 0.6,
              "analysis of the listening");
    LQ_CHECK (body["proposal"].isVoid(), "no proposal yet");
    LQ_CHECK (body["applied"]["bands"].size() == 8, "applied set");
    LQ_CHECK (rig.state->take (0).body.length() < 20000, "text only (no audio)");

    reply (*rig.state, 0, 200, kProposal1);
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::proposal; }, 2000), "proposal stage");
    const auto preview = rig.p->getProposal();
    LQ_CHECK (preview.has_value() && (*preview)[0].type == FilterType::bell
                  && std::abs ((*preview)[0].frequencyHz - 1000.0) < 1e-6 && std::abs ((*preview)[0].gainDb + 4.0) < 1e-6
                  && ! (*preview)[0].bypassed && (*preview)[1].type == FilterType::highShelf && (*preview)[2].bypassed,
              "sanitised proposal in the preview path");
    LQ_CHECK (rig.p->getListenRequest() == dsp::Listen::preview && s.getListen() == dsp::Listen::preview, "listening to the preview");
    LQ_CHECK (s.getMessages().back().role == assist::Message::Role::assistant
                  && s.getMessages().back().text == "Bajo un poco la zona de 1 kHz y doy aire.",
              "explanation in the conversation");
    LQ_CHECK ((s.getQuickReplies() == std::vector<std::string> { "Menos agudos", "Más suave" }), "server quick replies");
    LQ_CHECK (s.canApply(), "can apply");

    s.apply();
    LQ_CHECK (s.getStage() == Stage::applied, "applied");
    LQ_CHECK (! rig.p->hasProposal() && rig.p->getUndoDepth() == 1, "proposal written, one undo step");
    LQ_CHECK (std::abs (rig.p->getAppliedBands()[0].gainDb + 4.0) < 0.006
                  && std::abs (rig.p->getAppliedBands()[0].frequencyHz - 1000.0) < 0.5,
              "parameters hold the proposal");
    LQ_CHECK (rig.p->getListenRequest() == dsp::Listen::applied, "back to the applied path");

    const auto saved = rig.p->getAssistantState();
    LQ_CHECK (saved.getNumChildren() == 6 && saved.getProperty ("source").toString() == "voice"
                  && saved.getChild (5).getProperty ("text").toString() == "Bajo un poco la zona de 1 kHz y doy aire.",
              "conversation saved in ASSISTANT");

    s.undo();
    LQ_CHECK (rig.p->getUndoDepth() == 0 && std::abs (rig.p->getAppliedBands()[0].gainDb) < 0.006, "undo restores");
}

LQ_TEST ("session: a correction sends the current proposal and no analysis; a new listening sends it again")
{
    Rig rig;
    reachProposal (ctx, rig);
    auto& s = rig.s();

    s.say ("menos agudos");
    LQ_CHECK (rig.state->posts == 2, "second request");
    const auto body = rig.body (1);
    LQ_CHECK (body["analysis"].isVoid(), "no analysis in a correction");
    const auto& bands = body["proposal"]["bands"];
    LQ_CHECK (bands.size() == 2 && bands[0]["type"].toString() == "bell" && std::abs ((double) bands[0]["gain_db"] + 4.0) < 1e-6
                  && bands[0]["reason"].toString() == "Menos dureza" && bands[1]["type"].toString() == "high_shelf",
              "current proposal sent");
    LQ_CHECK (sameMessages (body["messages"], s.getMessages()) && s.getMessages().back().text == "menos agudos", "messages");

    reply (*rig.state, 1, 200, kProposal2);
    LQ_CHECK (pumpUntil ([&] { return s.getMessages().back().text == "Quito el realce de agudos."; }, 2000), "second proposal");
    LQ_CHECK (rig.p->getProposal().has_value() && (*rig.p->getProposal())[1].bypassed, "preview updated");

    s.listenAgain();
    LQ_CHECK (s.getStage() == Stage::listening, "listening again");
    fillCapture (*rig.p, 12.5, 3);
    s.poll();
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::askGoal; }, 20000), "analysed again");
    s.say ("Menos caja");
    LQ_CHECK (rig.state->posts == 3 && rig.body (2)["analysis"].isObject(), "the first request after a listening carries it");
    reply (*rig.state, 2, 200, kQuestion);
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::askGoal; }, 2000), "question: back to askGoal");
    LQ_CHECK ((s.getQuickReplies() == std::vector<std::string> { "Sí", "No" }), "question quick replies");
    s.say ("No");
    LQ_CHECK (rig.state->posts == 4 && rig.body (3)["analysis"].isVoid(), "only the first one");
}

LQ_TEST ("session: 503 goes offline without touching the applied EQ; retry resends")
{
    Rig rig;
    listenAsVoice (ctx, rig);
    auto& s = rig.s();

    // The first request fails: the retry still carries the analysis.
    s.say ("Menos caja");
    reply (*rig.state, 0, 503, "{\"error\":\"unavailable\"}");
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::offline; }, 2000), "offline");
    LQ_CHECK (s.getNotice() == kOffline, "offline notice");
    LQ_CHECK ((s.getQuickReplies() == std::vector<std::string> { Session::retryReply() }), "retry key");
    const auto messagesBefore = s.getMessages();
    s.say (Session::retryReply());
    LQ_CHECK (rig.state->posts == 2 && s.getStage() == Stage::waiting, "retry sent");
    LQ_CHECK (s.getMessages().size() == messagesBefore.size(), "retry adds no message");
    LQ_CHECK (rig.body (1)["analysis"].isObject() && sameMessages (rig.body (1)["messages"], messagesBefore),
              "same conversation and the analysis");

    reply (*rig.state, 1, 200, kProposal1);
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::proposal; }, 2000), "proposal after retry");
    LQ_CHECK (s.getNotice().empty(), "notice cleared");
    s.apply();
    const auto applied = rig.p->getAppliedBands();

    // Offline with something applied and a new proposal pending.
    s.say ("Menos agudos");
    reply (*rig.state, 2, 200, kProposal2);
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::proposal; }, 2000), "second proposal");
    s.say ("Más suave");
    reply (*rig.state, 3, 503, {});
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::offline; }, 2000), "offline again");
    const auto now = rig.p->getAppliedBands();
    bool same = true;
    for (size_t b = 0; b < now.size(); ++b)
        same = same && now[b].gainDb == applied[b].gainDb && now[b].frequencyHz == applied[b].frequencyHz
               && now[b].type == applied[b].type && now[b].bypassed == applied[b].bypassed;
    LQ_CHECK (same, "applied EQ untouched");
    LQ_CHECK (rig.p->getUndoDepth() == 1 && rig.p->hasProposal() && s.canApply(), "undo history and pending proposal kept");

    // Other errors: notice, back to where it was.
    s.say ("Más suave");
    reply (*rig.state, 4, 401, {});
    LQ_CHECK (pumpUntil ([&] { return ! s.getNotice().empty() && s.getStage() != Stage::waiting; }, 2000), "401 answered");
    LQ_CHECK (s.getStage() == Stage::proposal && s.getNotice() == assist::userMessage (assist::ErrorKind::noAccess),
              "noAccess: notice, proposal still there");
}

LQ_TEST ("session: startOver clears the conversation but not the applied EQ")
{
    Rig rig;
    reachProposal (ctx, rig);
    auto& s = rig.s();
    s.apply();
    s.say ("Menos agudos");
    reply (*rig.state, 1, 200, kProposal2);
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::proposal; }, 2000), "proposal pending");
    const auto applied = rig.p->getAppliedBands();
    const auto oldId = rig.body (0)["session_id"].toString();

    s.startOver();
    LQ_CHECK (s.getStage() == Stage::askSource && s.getMessages().size() == 1 && s.getMessages()[0].text == kAskSource,
              "fresh conversation");
    LQ_CHECK (! s.getSource().has_value() && s.getQuickReplies() == sourceLabels(), "no source");
    LQ_CHECK (! rig.p->hasProposal() && rig.p->getListenRequest() == dsp::Listen::applied, "proposal dropped");
    LQ_CHECK (std::abs (rig.p->getAppliedBands()[0].gainDb - applied[0].gainDb) < 1e-9 && rig.p->getUndoDepth() == 1,
              "applied EQ and its undo kept");
    LQ_CHECK (rig.p->getAssistantState().getNumChildren() == 1 && ! rig.p->getAssistantState().hasProperty ("source"),
              "saved conversation cleared");

    s.say ("Bajo");
    fillCapture (*rig.p, 12.5);
    s.poll();
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::askGoal; }, 20000), "listened");
    LQ_CHECK ((s.getQuickReplies() == std::vector<std::string> { "Más definición", "Más fondo", "Menos retumbe" }), "bass goals");
    s.say ("Más fondo");
    const auto body = rig.body ((size_t) rig.state->posts - 1);
    LQ_CHECK (body["session_id"].toString() != oldId && body["proposal"].isVoid() && body["messages"].size() == 5,
              "new session id, nothing from before");
}

LQ_TEST ("session: restoring the state brings the conversation back")
{
    Rig a;
    reachProposal (ctx, a);
    a.s().apply();
    juce::MemoryBlock saved;
    a.p->getStateInformation (saved);

    Rig b;
    b.p->setStateInformation (saved.getData(), (int) saved.getSize());
    b.s().poll();
    const auto& ma = a.s().getMessages();
    const auto& mb = b.s().getMessages();
    bool same = ma.size() == mb.size();
    for (size_t i = 0; same && i < ma.size(); ++i)
        same = ma[i].role == mb[i].role && ma[i].text == mb[i].text;
    LQ_CHECK (same && mb.size() == 6, "messages restored");
    LQ_CHECK (b.s().getSource() == assist::Source::voice && b.s().getStage() == Stage::askGoal, "source and stage");
    LQ_CHECK (! b.p->hasProposal() && b.p->getUndoDepth() == 0, "no proposal, no undo");
    LQ_CHECK (std::abs (b.p->getAppliedBands()[0].gainDb + 4.0) < 0.006, "applied EQ restored");

    // It continues the same conversation (same session id, no analysis).
    b.s().say ("Más brillo");
    const auto body = b.body (0);
    LQ_CHECK (body["session_id"].toString() == a.body (0)["session_id"].toString(), "same session id");
    LQ_CHECK (body["analysis"].isVoid() && body["messages"].size() == 7, "continues without an analysis");

    // Other sources and the source note survive too.
    Rig c;
    c.s().chooseSource (assist::Source::other, "Ukelele");
    LQ_CHECK (c.s().getSource() == assist::Source::other && c.s().getStage() == Stage::listening, "other with a note");
    juce::MemoryBlock savedC;
    c.p->getStateInformation (savedC);
    Rig d;
    d.p->setStateInformation (savedC.getData(), (int) savedC.getSize());
    d.s().poll();
    LQ_CHECK (d.s().getStage() == Stage::listening && d.p->getCapture().getState() == analysis::CaptureBuffer::State::capturing,
              "restored while listening: listens again");
    fillCapture (*d.p, 12.5);
    d.s().poll();
    LQ_CHECK (pumpUntil ([&] { return d.s().getStage() == Stage::askGoal; }, 20000), "listened");
    LQ_CHECK ((d.s().getQuickReplies() == std::vector<std::string> { "Más claridad", "Menos dureza", "Más cuerpo" }), "other goals");
    d.s().say ("Más cuerpo");
    LQ_CHECK (d.body (0)["source"].toString() == "other" && d.body (0)["source_note"].toString() == "Ukelele", "note sent");
}

LQ_TEST ("session: an in-instance state load drops the proposal, the undo history and stale replies")
{
    Rig rig;
    listenAsVoice (ctx, rig);
    auto& s = rig.s();
    juce::MemoryBlock early; // askGoal, nothing applied
    rig.p->getStateInformation (early);
    const auto earlyMessages = s.getMessages();

    s.say ("Que se entienda");
    reply (*rig.state, 0, 200, kProposal1);
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::proposal; }, 2000), "proposal");
    s.apply();
    s.say ("Menos agudos");
    reply (*rig.state, 1, 200, kProposal2);
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::proposal; }, 2000), "second proposal");
    s.setListen (dsp::Listen::preview);
    s.say ("Más suave"); // in flight during the load
    LQ_CHECK (s.getStage() == Stage::waiting && rig.state->posts == 3, "request in flight");

    // Host A/B compare: the old state, off the message thread.
    std::thread ([&] { rig.p->setStateInformation (early.getData(), (int) early.getSize()); }).join();
    LQ_CHECK (rig.p->getListenRequest() == dsp::Listen::applied, "the proposal is silenced at once");

    s.poll();
    LQ_CHECK (! rig.p->hasProposal() && rig.p->getUndoDepth() == 0 && ! rig.p->undoApply(), "proposal and undo history dropped");
    bool same = s.getMessages().size() == earlyMessages.size();
    for (size_t i = 0; same && i < earlyMessages.size(); ++i)
        same = s.getMessages()[i].text == earlyMessages[i].text;
    LQ_CHECK (same, "the restored conversation");
    LQ_CHECK (s.getStage() == Stage::askGoal && s.getListen() == dsp::Listen::applied && ! s.canApply() && ! s.canUndo(),
              "stage from the restored conversation, nothing pending");
    LQ_CHECK (std::abs (rig.p->getAppliedBands()[0].gainDb) < 0.006, "applied set of the loaded state");

    reply (*rig.state, 2, 200, kProposal1); // the stale reply
    pump (150);
    LQ_CHECK (s.getStage() == Stage::askGoal && ! rig.p->hasProposal() && s.getMessages().size() == earlyMessages.size(),
              "stale reply dropped");

    // The same happens without poll(): every entry point notices the load first.
    s.say ("Que se entienda");
    reply (*rig.state, 3, 200, kProposal1);
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::proposal; }, 2000), "proposal again");
    s.apply();
    std::thread ([&] { rig.p->setStateInformation (early.getData(), (int) early.getSize()); }).join();
    s.undo(); // must not restore the pre-load history
    LQ_CHECK (rig.p->getUndoDepth() == 0 && std::abs (rig.p->getAppliedBands()[0].gainDb) < 0.006
                  && s.getMessages().size() == earlyMessages.size(),
              "undo after a load does nothing");

    // And the timer notices it on its own.
    s.say ("Que se entienda");
    reply (*rig.state, 4, 200, kProposal1);
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::proposal; }, 2000), "proposal once more");
    std::thread ([&] { rig.p->setStateInformation (early.getData(), (int) early.getSize()); }).join();
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::askGoal; }, 2000) && ! rig.p->hasProposal(), "timer reload");
}

LQ_TEST ("session: stopping early analyses what was captured if it is enough")
{
    Rig rig;
    auto& s = rig.s();
    s.say ("Mezcla completa");
    LQ_CHECK (s.getSource() == assist::Source::mix, "mix");

    fillCapture (*rig.p, 3.0);
    s.say ("ya");
    LQ_CHECK (s.getStage() == Stage::listening && ! s.getNotice().empty(), "not enough yet: keeps listening, says so");

    fillCapture (*rig.p, 5.0, 5); // 8 s, then playback stops
    s.poll();
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::askGoal; }, Session::stallMs + 20000), "stalled with 8 s: analysed");
    LQ_CHECK ((s.getQuickReplies() == std::vector<std::string> { "Más brillo", "Más claridad", "Menos barro" }), "mix goals");
    s.say ("Más brillo");
    LQ_CHECK (std::abs ((double) rig.body (0)["analysis"]["seconds"] - 8.0) < 0.2, "8 s analysed");
}

LQ_TEST ("session: destroying the plugin while analysing or waiting is safe")
{
    {
        Rig rig;
        rig.s().say ("Bajo");
        fillCapture (*rig.p, 12.5);
        rig.s().poll(); // analysis starts on the worker
        rig.p.reset();  // joins the worker; its result is dropped
    }
    pump (300);

    {
        Rig rig;
        listenAsVoice (ctx, rig);
        rig.s().say ("Más fondo");
        auto state = rig.state;
        rig.p.reset();
        reply (*state, 0, 200, kProposal1); // after destruction: dropped
    }
    pump (150);
    LQ_CHECK (true, "no crash");
}

LQ_TEST ("session: the debug editor follows the session and saves the access code only in Settings")
{
    // Never the real user settings: a temporary file for this test.
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("iaq8-editor-test", "", false);
    juce::PropertiesFile::Options o;
    o.applicationName = "IAQ8";
    o.filenameSuffix = "settings";
    o.osxLibrarySubFolder = "Application Support";
    o.folderName = dir.getFullPathName();
    assist::Settings::setFileOptionsForTesting (o);

    Rig rig;
    // DebugEditor is no longer what createEditor gives (Plan 3); built directly until Task 8.
    std::unique_ptr<juce::AudioProcessorEditor> editor = std::make_unique<DebugEditor> (*rig.p);
    const auto find = [&] (const char* id) { return editor->findChildWithID (id); };

    auto* quick0 = dynamic_cast<juce::TextButton*> (find ("quick0"));
    auto* quick7 = dynamic_cast<juce::TextButton*> (find ("quick7"));
    LQ_CHECK (quick0 != nullptr && quick0->isVisible() && quick0->getButtonText() == "Voz", "first source on key 1");
    LQ_CHECK (quick7 != nullptr && ! quick7->isVisible(), "7 sources: key 8 hidden");
    auto* log = dynamic_cast<juce::TextEditor*> (find ("log"));
    LQ_CHECK (log != nullptr && log->getText().contains (juce::String (juce::CharPointer_UTF8 ("\xc2\xbfQu\xc3\xa9 tienes en esta pista?"))),
              "conversation in the log");

    if (quick0 != nullptr)
        quick0->triggerClick();
    LQ_CHECK (pumpUntil ([&] { return rig.s().getStage() == Stage::listening; }, 1000), "a quick reply key answers");

    auto* code = dynamic_cast<juce::TextEditor*> (find ("accessCode"));
    auto* save = dynamic_cast<juce::TextButton*> (find ("saveCode"));
    LQ_CHECK (code != nullptr && save != nullptr, "access code field");
    if (code != nullptr && save != nullptr)
    {
        code->setText ("dummy-code-9X4");
        save->triggerClick();
        pumpUntil ([&] { return assist::Settings::accessCode().isNotEmpty(); }, 1000);
        LQ_CHECK (assist::Settings::accessCode() == "dummy-code-9X4", "saved through Settings"); // value never printed
        LQ_CHECK (code->getText().isEmpty(), "field cleared");
        juce::MemoryBlock state;
        rig.p->getStateInformation (state);
        const std::string bytes (static_cast<const char*> (state.getData()), state.getSize());
        LQ_CHECK (state.getSize() > 0 && bytes.find ("dummy-code-9X4") == std::string::npos, "never in the DAW state");
    }

    rig.s().startOver();
    pump (100);
    editor.reset();
    rig.s().say ("Bajo"); // the editor is no longer a listener
    LQ_CHECK (rig.s().getStage() == Stage::listening, "session keeps working without the editor");

    assist::Settings::setFileOptionsForTesting (std::nullopt);
    dir.deleteRecursively();
}

LQ_TEST ("session: a load whose counter another call consumed still drops the stale proposal")
{
    Rig rig;
    listenAsVoice (ctx, rig);
    auto& s = rig.s();
    juce::MemoryBlock early;
    rig.p->getStateInformation (early);

    // The race, made deterministic: the load lands, a processor call consumes
    // the counter, then the old conversation's proposal is published and
    // previewed (what a reply handler running across the load would do).
    std::thread ([&] { rig.p->setStateInformation (early.getData(), (int) early.getSize()); }).join();
    LQ_CHECK (! rig.p->hasProposal(), "the counter is consumed here");
    dsp::BandSet stale {};
    stale[3] = { 1000.0, -12.0, 4.0, false, FilterType::bell };
    rig.p->setProposal (stale);
    rig.p->setListen (dsp::Listen::preview);

    s.poll();
    LQ_CHECK (! rig.p->hasProposal() && ! s.canApply(), "stale proposal dropped");
    LQ_CHECK (rig.p->getListenRequest() == dsp::Listen::applied && s.getListen() == dsp::Listen::applied, "listening to applied");
}

LQ_TEST ("session: an analysis result that arrives after a state load is dropped")
{
    Rig rig;
    auto& s = rig.s();
    juce::MemoryBlock fresh; // askSource
    rig.p->getStateInformation (fresh);

    auto calls = std::make_shared<std::atomic<int>> (0);
    s.setAnalyseFunctionForTesting ([calls] (const std::vector<std::vector<float>>& ch, double fs, assist::Source src)
    {
        ++*calls;
        std::this_thread::sleep_for (std::chrono::milliseconds (300)); // the load lands meanwhile
        return analysis::analyse (ch, fs, src);
    });
    s.say ("Voz");
    fillCapture (*rig.p, 12.5);
    s.poll();
    LQ_CHECK (s.getStage() == Stage::listening && s.getListenProgress() == 1.0, "analysing");
    std::thread ([&] { rig.p->setStateInformation (fresh.getData(), (int) fresh.getSize()); }).join();
    pump (800);
    LQ_CHECK (calls->load() == 1, "the analysis ran");
    LQ_CHECK (s.getStage() == Stage::askSource && s.getMessages().size() == 1 && s.getMessages()[0].text == kAskSource,
              "its result was dropped: the loaded conversation stays");
    LQ_CHECK (rig.state->posts == 0, "nothing sent");
}

LQ_TEST ("session: an analysis without enough signal says so and listens again")
{
    Rig rig;
    auto& s = rig.s();
    auto calls = std::make_shared<std::atomic<int>> (0);
    s.setAnalyseFunctionForTesting ([calls] (const std::vector<std::vector<float>>& ch, double fs, assist::Source src)
    {
        auto summary = analysis::analyse (ch, fs, src);
        if (++*calls == 1)
            summary.enoughSignal = false; // e.g. too quiet
        return summary;
    });
    s.say ("Voz");
    fillCapture (*rig.p, 12.5);
    s.poll();
    LQ_CHECK (pumpUntil ([&] { return calls->load() == 1 && ! s.getNotice().empty(); }, 5000), "first analysis answered");
    LQ_CHECK (s.getStage() == Stage::listening && juce::String::fromUTF8 (s.getNotice().c_str()).contains (juce::String (juce::CharPointer_UTF8 ("se\xc3\xb1" "al"))),
              "not enough signal: notice, still listening");
    LQ_CHECK (rig.p->getCapture().getState() == analysis::CaptureBuffer::State::capturing
                  && rig.p->getCapture().getCapturedSeconds() == 0.0,
              "re-armed from empty");
    const auto before = s.getMessages().size();
    fillCapture (*rig.p, 12.5, 9);
    s.poll();
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::askGoal; }, 20000), "second listening analysed");
    LQ_CHECK (s.getMessages().size() == before + 1 && s.getNotice().empty(), "goal question, notice cleared");
}

LQ_TEST ("session: a refused arm() is retried and never re-analyses the previous capture")
{
    Rig rig;
    auto& s = rig.s();
    s.say ("Voz");
    auto& capture = rig.p->getCapture();

    // A reader that keeps the capture busy (copyOut sets the reading flag,
    // during which arm() refuses).
    std::atomic<bool> stop { false };
    std::thread reader ([&]
    {
        std::vector<std::vector<float>> copy;
        while (! stop.load())
            capture.copyOut (copy);
    });

    bool refused = false;
    for (int attempt = 0; attempt < 200 && ! refused; ++attempt)
    {
        fillCapture (*rig.p, 12.5, attempt); // a full previous capture (done)
        s.listenAgain();
        refused = capture.getState() != analysis::CaptureBuffer::State::capturing;
    }

    if (refused)
    {
        LQ_CHECK (s.getStage() == Stage::listening && s.getListenProgress() == 0.0, "listening, nothing captured yet");
        LQ_CHECK (capture.hasEnough(), "the buffer still holds the previous capture");
        s.say ("ya");
        LQ_CHECK (s.getListenProgress() == 0.0 && ! s.getNotice().empty(), "not analysed: asks for more");
    }
    stop = true;
    reader.join();

    if (! refused)
    {
        ctx.skip ("arm() was never refused in 200 attempts");
        return;
    }
    s.poll();
    LQ_CHECK (capture.getState() == analysis::CaptureBuffer::State::capturing && capture.getCapturedSeconds() == 0.0,
              "poll() arms it");
    fillCapture (*rig.p, 12.5, 4);
    s.poll();
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::askGoal; }, 20000), "then listens normally");
}

LQ_TEST ("session: an analysis that throws does not crash; the session listens again")
{
    Rig rig;
    auto& s = rig.s();
    auto calls = std::make_shared<std::atomic<int>> (0);
    s.setAnalyseFunctionForTesting ([calls] (const std::vector<std::vector<float>>& ch, double fs, assist::Source src)
    {
        if (++*calls == 1)
            throw std::runtime_error ("analysis failed"); // e.g. std::bad_alloc
        return analysis::analyse (ch, fs, src);
    });
    s.say ("Voz");
    fillCapture (*rig.p, 12.5);
    s.poll();
    LQ_CHECK (pumpUntil ([&] { return calls->load() == 1 && ! s.getNotice().empty(); }, 5000), "the failure is answered");
    LQ_CHECK (s.getStage() == Stage::listening && s.getListenProgress() == 0.0, "still listening, nothing captured");
    LQ_CHECK (rig.p->getCapture().getState() == analysis::CaptureBuffer::State::capturing
                  && rig.p->getCapture().getCapturedSeconds() == 0.0,
              "re-armed from empty");
    ctx.note ("notice: " + s.getNotice());

    fillCapture (*rig.p, 12.5, 9);
    s.poll();
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::askGoal; }, 20000), "the next listening is analysed");
    LQ_CHECK (s.getNotice().empty() && rig.state->posts == 0, "notice cleared, nothing sent");
}

LQ_TEST ("session: a capture emptied by a re-prepare is listened to again without a notice")
{
    // The worker's first copy must find a capture running again (busy: poll()
    // retries) before the host re-prepares, which empties the buffer. The test
    // re-arms the capture right after the analysis starts; if the worker
    // copied first, the attempt is repeated with a fresh plugin.
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        Rig rig;
        auto& s = rig.s();
        auto withData = std::make_shared<std::atomic<int>> (0);
        auto empty = std::make_shared<std::atomic<int>> (0);
        s.setAnalyseFunctionForTesting ([withData, empty] (const std::vector<std::vector<float>>& ch, double fs, assist::Source src)
        {
            ++*(ch.empty() || ch[0].empty() ? empty : withData);
            return analysis::analyse (ch, fs, src);
        });
        s.say ("Voz");
        fillCapture (*rig.p, 7.0); // enough, still capturing
        auto& capture = rig.p->getCapture();

        s.say ("ya");
        const bool rearmed = capture.arm();
        if (! pumpUntil ([&] { return s.getListenProgress() < 1.0; }, 5000) || ! rearmed || withData->load() > 0)
            continue; // the worker copied first

        ctx.note (str ("scenario reached at attempt ", attempt + 1));
        rig.p->prepareToPlay (kFs, kBlock);
        s.poll(); // retries the analysis: the copy has 0 samples
        LQ_CHECK (pumpUntil ([&] { return s.getListenProgress() < 1.0 && capture.getState() == analysis::CaptureBuffer::State::capturing; }, 5000),
                  "listening again");
        LQ_CHECK (empty->load() == 0, str ("an empty capture is never analysed (", empty->load(), " times)"));
        LQ_CHECK (s.getStage() == Stage::listening && s.getNotice().empty(), "no misleading notice: " + s.getNotice());

        fillCapture (*rig.p, 12.5, 3);
        s.poll();
        LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::askGoal; }, 20000), "then listens normally");
        return;
    }
    ctx.skip ("the worker always copied before the test re-armed the capture");
}

LQ_TEST ("session: applying while listening keeps listening")
{
    Rig rig;
    reachProposal (ctx, rig);
    auto& s = rig.s();
    s.listenAgain();
    LQ_CHECK (s.getStage() == Stage::listening && s.canApply(), "listening with the proposal still pending");
    s.apply();
    LQ_CHECK (rig.p->getUndoDepth() == 1 && ! rig.p->hasProposal() && s.getListen() == dsp::Listen::applied, "applied");
    LQ_CHECK (s.getStage() == Stage::listening, "the listening goes on");
    LQ_CHECK (rig.p->getCapture().getState() == analysis::CaptureBuffer::State::capturing, "capture still armed");
    fillCapture (*rig.p, 12.5, 5);
    s.poll();
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::askGoal; }, 20000), "the listening ends normally");
    LQ_CHECK (s.canUndo(), "undo still available");
}

LQ_TEST ("session: applying while offline drops the retry key")
{
    Rig rig;
    reachProposal (ctx, rig);
    auto& s = rig.s();
    const auto proposalReplies = s.getQuickReplies();
    s.say ("Más suave");
    reply (*rig.state, (size_t) rig.state->posts.load() - 1, 503, {});
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::offline; }, 2000), "offline");
    LQ_CHECK ((s.getQuickReplies() == std::vector<std::string> { Session::retryReply() }), "retry key while offline");

    s.apply();
    LQ_CHECK (s.getStage() == Stage::applied, "applied");
    LQ_CHECK (s.getQuickReplies() == proposalReplies, "the keys from before the request come back");
    const auto posts = rig.state->posts.load();
    s.undo();
    LQ_CHECK (s.getStage() == Stage::askGoal, "undone");
    for (const auto& q : s.getQuickReplies())
        LQ_CHECK (q != Session::retryReply(), "no retry key after leaving offline");
    LQ_CHECK (rig.state->posts.load() == posts, "nothing sent");
}

LQ_TEST ("session: a long conversation sends only the latest 190 messages and keeps them all")
{
    Rig a;
    listenAsVoice (ctx, a);
    auto tree = a.p->getAssistantState().createCopy();
    for (int i = 0; i < 250; ++i)
    {
        juce::ValueTree m ("MESSAGE");
        m.setProperty ("role", i % 2 == 0 ? "user" : "assistant", nullptr);
        m.setProperty ("text", "m" + juce::String (i), nullptr);
        tree.appendChild (m, nullptr);
    }
    a.p->setAssistantState (tree);
    juce::MemoryBlock saved;
    a.p->getStateInformation (saved);

    Rig b;
    b.p->setStateInformation (saved.getData(), (int) saved.getSize());
    b.s().poll();
    LQ_CHECK (b.s().getMessages().size() == 254, str ("restored: ", b.s().getMessages().size()));
    b.s().say ("Más brillo");
    LQ_CHECK (b.state->posts == 1, "one request");
    const auto messages = b.body (0)["messages"];
    LQ_CHECK (messages.size() == (int) Session::maxRequestMessages && Session::maxRequestMessages == 190,
              str ("190 messages sent: ", messages.size()));
    LQ_CHECK (messages[0]["text"].toString() == "m61" && messages[189]["text"].toString() == "Más brillo",
              "the latest ones, in order");
    LQ_CHECK (b.s().getMessages().size() == 255 && b.s().getMessages()[0].text == kAskSource, "the session keeps the full history");
    LQ_CHECK (b.p->getAssistantState().getNumChildren() == 255, "and saves it");
}

LQ_TEST ("session: the source note and the user text are capped at 4000 characters")
{
    Rig rig;
    std::string longNote;
    for (int i = 0; i < 4500; ++i)
        longNote += "ü"; // 2 UTF-8 bytes: the cap counts characters and never splits one
    rig.s().say (longNote);
    LQ_CHECK (rig.s().getSource() == assist::Source::other, "free text: other");
    const auto note = rig.p->getAssistantState().getProperty ("source_note").toString();
    LQ_CHECK (note.length() == Session::maxTextChars && Session::maxTextChars == 4000, str ("note capped: ", note.length()));
    LQ_CHECK (note.containsOnly (juce::String (juce::CharPointer_UTF8 ("\xc3\xbc"))), "whole characters");
    const auto& m = rig.s().getMessages();
    LQ_CHECK (m.size() >= 2 && juce::String::fromUTF8 (m[1].text.c_str()).length() <= Session::maxTextChars + 6, "message: label + capped note");
}

LQ_TEST ("session: the debug editor input field takes at most 4000 characters")
{
    Rig rig;
    std::unique_ptr<juce::AudioProcessorEditor> editor = std::make_unique<DebugEditor> (*rig.p);
    auto* input = dynamic_cast<juce::TextEditor*> (editor->findChildWithID ("input"));
    LQ_CHECK (input != nullptr, "input field");
    if (input != nullptr)
    {
        input->insertTextAtCaret (juce::String::repeatedString ("a", 5000));
        LQ_CHECK (input->getText().length() == 4000, str ("capped: ", input->getText().length()));
    }
}

LQ_TEST ("session: listened seconds follow the listening and stay with its analysis")
{
    Rig rig;
    auto& s = rig.s();
    LQ_CHECK (! s.getListenedSeconds().has_value(), "nothing listened at first");

    s.say ("Voz");
    LQ_CHECK (s.getListenedSeconds().has_value() && *s.getListenedSeconds() < 0.01, "listening: 0 s");
    fillCapture (*rig.p, 6.0);
    s.poll();
    LQ_CHECK (std::abs (s.getListenedSeconds().value_or (-1.0) - 6.0) < 0.3, str ("6 s: ", s.getListenedSeconds().value_or (-1.0)));
    fillCapture (*rig.p, 6.5, 11);
    s.poll();
    LQ_CHECK (pumpUntil ([&] { return s.getStage() == Stage::askGoal; }, 20000), "analysed");
    LQ_CHECK (std::abs (s.getListenedSeconds().value_or (-1.0) - 12.0) < 0.6,
              str ("the analysed listening: ", s.getListenedSeconds().value_or (-1.0)));

    // A new listening counts from zero; startOver forgets it.
    s.listenAgain();
    LQ_CHECK (s.getListenedSeconds().value_or (-1.0) < 0.01, "a new listening starts at 0 s");
    s.startOver();
    LQ_CHECK (! s.getListenedSeconds().has_value(), "startOver: nothing listened");
}
