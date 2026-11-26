// iaq8_ui_snapshot <dir>
//
// Renders the IAQ8 editor offscreen (no window, no host) and writes PNGs of
// the interface states to <dir>, to compare by eye with the approved mockup
// (docs/superpowers/specs/iaq8-mockup-v2):
//   1_inicio.png       the first question (which source)
//   2_escuchando.png   listening, 7 of 12 s
//   3_propuesta.png    a proposal: curve, explanation and keys
//   4_detalle.png      the same proposal with VER DETALLE (band table)
//   5_sin_conexion.png the next request finds the server unavailable
//   6_zoom_040.png     the proposal at 40 % (669 x 376)
//   7_zoom_140.png     the proposal at 140 % (2341 x 1317)
//   8_pensando.png     that next request in flight: "IAQ8> PENSANDO..."
// 1 to 5 and 8 are at 100 % (1672 x 941). Everything is rendered at one image
// pixel per editor pixel (a 1x display).
//
// The Session talks to a fake transport that answers with fixed replies: no
// network, no access code, no settings file. The zoom preference lives in
// memory (the user's IAQ8.ui is never read or written).
// Exit 0 on success, 1 if a state could not be reached or a file written, 2 on
// bad arguments.

#include "assist/AssistClient.h"
#include "assist/Session.h"
#include "plugin/Iaq8Processor.h"
#include "ui/Iaq8Editor.h"
#include "ui/Iaq8Preferences.h"
#include "ui/LevelMeterView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
using iaq8::assist::Session;
using Stage = Session::Stage;
using iaq8::ui::Iaq8Editor;

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

// The conversation of the mockup: a question about the low end, then the
// proposal (-4 dB at 250 Hz, +2.5 dB from 8 kHz) with its quick replies.
const char* questionReply =
    "{\"kind\":\"question\","
    "\"text\":\"Noto grave acumulado hacia 250 Hz. \\u00bfSuena como si hablara dentro de una caja?\","
    "\"quick_replies\":[\"S\\u00ed, y un poco apagada\",\"No\"]}";

const char* proposalReply =
    "{\"kind\":\"proposal\","
    "\"explanation\":\"Quito 4 dB en 250 Hz (menos caja) y subo 2.5 dB desde 8 kHz (m\\u00e1s aire). "
    "Comp\\u00e1ralo con antes/despu\\u00e9s.\","
    "\"bands\":[{\"type\":\"bell\",\"freq_hz\":250,\"gain_db\":-4,\"q\":1.2,\"reason\":\"Menos caja\"},"
    "{\"type\":\"high_shelf\",\"freq_hz\":8000,\"gain_db\":2.5,\"q\":0.7,\"reason\":\"M\\u00e1s aire\"}],"
    "\"quick_replies\":[\"Menos brillo\",\"M\\u00e1s claridad\",\"M\\u00e1s cuerpo\",\"M\\u00e1s suave\","
    "\"Menos cambio\",\"Otra idea\",\"\\u00bfPor qu\\u00e9?\"]}";

// Keeps every request; replies are given one by one with reply().
struct FakeTransport final : iaq8::assist::Transport
{
    struct State
    {
        std::mutex mutex;
        std::vector<std::function<void (int, juce::String)>> pending;
    };

    explicit FakeTransport (std::shared_ptr<State> s) : state (std::move (s)) {}

    void post (const juce::URL&, const juce::String&, const juce::String&,
               std::function<void (int, juce::String)> done) override
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        state->pending.push_back (std::move (done));
    }

    std::shared_ptr<State> state;
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

juce::String utf8 (const char* text) { return juce::String (juce::CharPointer_UTF8 (text)); }

class Snapshots
{
public:
    explicit Snapshots (juce::File dir) : folder (std::move (dir))
    {
        iaq8::ui::preferences::setStorageFolderForTesting (juce::File()); // in memory
        Iaq8Editor::setScreenAreaForTesting ({ 0, 0, 4000, 3000 });         // every zoom fits
        processor = std::make_unique<iaq8::Iaq8AudioProcessor> (std::make_unique<iaq8::assist::AssistClient> (
            std::make_unique<FakeTransport> (transport),
            [] { return juce::URL ("https://assist.example.test"); },
            [] { return juce::String ("snapshot"); }));
        processor->setRateAndBufferSizeDetails (sampleRate, blockSize);
        processor->prepareToPlay (sampleRate, blockSize);
        editor = std::make_unique<Iaq8Editor> (*processor);
        editor->setZoom (1.0f);
    }

    ~Snapshots()
    {
        editor.reset();
        processor.reset();
    }

    bool run()
    {
        // 1: the first question.
        if (! save ("1_inicio"))
            return false;

        // 2: the source on the first key ("Voz"), 7 s listened.
        click (editor->getQuickKey (0));
        if (! expect (session().getStage() == Stage::listening, "listening"))
            return false;
        capture (7.0);
        editor->refreshTick();
        if (! save ("2_escuchando"))
            return false;

        // 3: the rest of the listening, a goal, the server's question, the
        // user's answer and the proposal. The command line holds the next
        // request, as in the mockup.
        capture (5.5);
        session().poll();
        if (! expect (pumpUntil ([this] { return session().getStage() == Stage::askGoal; }, 20000), "analysed"))
            return false;
        click (editor->getQuickKey (0));
        if (! answer (200, questionReply, Stage::askGoal, "question"))
            return false;
        editor->getCommandLine().setText (utf8 ("S\xc3\xad, y un poco apagada"));
        editor->sendCommandLine();
        if (! answer (200, proposalReply, Stage::proposal, "proposal"))
            return false;
        editor->getCommandLine().setText (utf8 ("M\xc3\xa1s calidez, pero sin perder claridad"));
        play (0.25f); // lights the IN / OUT meters
        if (! save ("3_propuesta"))
            return false;

        // 6 and 7: the same state at the smallest and largest zooms.
        if (! (editor->setZoom (0.4f) && save ("6_zoom_040") && editor->setZoom (1.4f) && save ("7_zoom_140")
               && editor->setZoom (1.0f)))
            return false;

        // 4: VER DETALLE.
        click (editor->getQuickKey (7));
        if (! expect (editor->getScreen().isDetailVisible(), "detail view") || ! save ("4_detalle"))
            return false;
        click (editor->getQuickKey (7));

        // 8: the command line's request in flight: PENSANDO, three dots.
        editor->sendCommandLine();
        for (int tick = 0; tick < 2 * Iaq8Editor::thinkingTicksPerDot; ++tick)
            editor->refreshTick();
        if (! expect (session().getStage() == Stage::waiting, "waiting") || ! save ("8_pensando"))
            return false;

        // 5: that request finds the server unavailable.
        if (! answer (503, {}, Stage::offline, "offline"))
            return false;
        return save ("5_sin_conexion");
    }

private:
    Session& session() { return processor->getSession(); }

    static bool expect (bool ok, const char* what)
    {
        if (! ok)
            std::fprintf (stderr, "iaq8_ui_snapshot: could not reach: %s\n", what);
        return ok;
    }

    // A click as the user makes it (asynchronous).
    static void click (juce::Button& key)
    {
        key.triggerClick();
        pump (20);
    }

    // Answers the latest request on a background thread, like a real
    // transport, and waits for the stage.
    bool answer (int status, juce::String body, Stage stage, const char* what)
    {
        std::function<void (int, juce::String)> done;
        {
            std::lock_guard<std::mutex> lock (transport->mutex);
            if (transport->pending.size() <= answered)
                return expect (false, what);
            done = transport->pending[answered++];
        }
        std::thread ([done, status, body] { done (status, body); }).join();
        return expect (pumpUntil ([&] { return session().getStage() == stage; }, 2000), what);
    }

    // Seconds of stereo noise into the capture (what the listening hears).
    void capture (double seconds)
    {
        std::vector<float> l ((size_t) blockSize), r ((size_t) blockSize);
        const float* channels[2] { l.data(), r.data() };
        for (int b = 0; b < (int) std::ceil (seconds * sampleRate / blockSize); ++b)
        {
            for (size_t i = 0; i < (size_t) blockSize; ++i)
            {
                l[i] = 0.3f * (random.nextFloat() * 2.0f - 1.0f);
                r[i] = 0.3f * (random.nextFloat() * 2.0f - 1.0f);
            }
            processor->getCapture().write (channels, 2, blockSize);
        }
    }

    // A few blocks of noise at `peak` through the processor, then one meter frame.
    void play (float peak)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < 8; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (ch, i, peak * (random.nextFloat() * 2.0f - 1.0f));
            processor->processBlock (buffer, midi);
        }
        for (auto* child : editor->getContent().getChildren())
            if (auto* meters = dynamic_cast<labq8::ui::LevelMeterView*> (child))
                for (int frame = 0; frame < 4; ++frame)
                    meters->advance (1.0 / 60.0);
    }

    bool save (const char* name)
    {
        editor->refreshFromSession();
        editor->getScreen().timerCallback(); // flush the screen's pending repaint

        juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true, juce::SoftwareImageType());
        {
            juce::Graphics g (image);
            editor->paintEntireComponent (g, true);
        }

        const auto file = folder.getChildFile (juce::String (name) + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
        {
            std::fprintf (stderr, "iaq8_ui_snapshot: cannot write %s\n", file.getFullPathName().toRawUTF8());
            return false;
        }
        std::printf ("%s (%d x %d)\n", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
        return true;
    }

    juce::File folder;
    std::shared_ptr<FakeTransport::State> transport = std::make_shared<FakeTransport::State>();
    size_t answered = 0;
    juce::Random random { 7 };
    std::unique_ptr<iaq8::Iaq8AudioProcessor> processor;
    std::unique_ptr<Iaq8Editor> editor;
};
} // namespace

int main (int argc, char* argv[])
{
    if (argc != 2)
    {
        std::fprintf (stderr, "usage: iaq8_ui_snapshot <dir>\n");
        return 2;
    }

    const juce::File folder (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1])));
    if (! folder.createDirectory())
    {
        std::fprintf (stderr, "iaq8_ui_snapshot: cannot create %s\n", folder.getFullPathName().toRawUTF8());
        return 2;
    }

    const juce::ScopedJuceInitialiser_GUI juce;
    bool ok = false;
    {
        Snapshots snapshots (folder);
        ok = snapshots.run();
    }
    pump (50); // let pending callbacks run before shutdown
    return ok ? 0 : 1;
}
