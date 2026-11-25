#include "TestHarness.h"

#include "assist/Settings.h"
#include "plugin/DebugEditor.h"
#include "plugin/Iaq8Processor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <random>

using namespace iaq8;

LQ_TEST ("iaq8 smoke: processor passes audio through")
{
    Iaq8AudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> dist (-0.5f, 0.5f);

    juce::AudioBuffer<float> buffer (2, 512);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample (ch, i, dist (rng));

    const juce::AudioBuffer<float> original (buffer);
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    bool identical = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            identical = identical && ! (buffer.getSample (ch, i) < original.getSample (ch, i))
                                  && ! (buffer.getSample (ch, i) > original.getSample (ch, i));

    LQ_CHECK (identical, "output must equal input sample by sample");
    LQ_CHECK (processor.hasEditor(), "debug editor available");
}

LQ_TEST ("iaq8 smoke: buses are mono->mono and stereo->stereo only")
{
    Iaq8AudioProcessor processor;
    using Layout = juce::AudioProcessor::BusesLayout;

    auto layout = [] (juce::AudioChannelSet in, juce::AudioChannelSet out)
    {
        Layout l;
        l.inputBuses.add (in);
        l.outputBuses.add (out);
        return l;
    };

    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    LQ_CHECK (processor.checkBusesLayoutSupported (layout (mono, mono)), "mono->mono");
    LQ_CHECK (processor.checkBusesLayoutSupported (layout (stereo, stereo)), "stereo->stereo");
    LQ_CHECK (! processor.checkBusesLayoutSupported (layout (mono, stereo)), "mono->stereo rejected");
    LQ_CHECK (! processor.checkBusesLayoutSupported (layout (juce::AudioChannelSet::create5point1(),
                                                             juce::AudioChannelSet::create5point1())),
              "5.1 rejected");
}

// Network spike, headless half: the same GET the debug editor's button does.
// Without an API key api.anthropic.com answers 401; any HTTP status proves the
// HTTPS path works. Only with IAQ8_NETWORK_TESTS=1 (the suite never touches
// the network otherwise); no connectivity -> skipped (never a false PASS).
LQ_TEST ("iaq8 smoke: HTTPS probe reaches api.anthropic.com (401 without key)")
{
    if (juce::SystemStats::getEnvironmentVariable ("IAQ8_NETWORK_TESTS", {}) != "1")
    {
        ctx.skip ("needs the network; set IAQ8_NETWORK_TESTS=1");
        return;
    }
    const auto result = probeHttpStatus ("https://api.anthropic.com/v1/models", 10000);

    if (result.statusCode == 0)
    {
        ctx.skip ("no network or HTTPS failed: " + result.error.toStdString());
        return;
    }

    ctx.note ("HTTP status: " + std::to_string (result.statusCode));
    LQ_CHECK (result.statusCode == 401, "expected 401 without key, got " + std::to_string (result.statusCode));
}

LQ_TEST ("iaq8 smoke: debug editor constructs, shows UTF-8 text and closes promptly")
{
    // The editor reads the access code through Settings: never the real user
    // settings, a temporary folder for this test.
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("iaq8-smoke-editor", "", false);
    juce::PropertiesFile::Options o;
    o.applicationName = "IAQ8";
    o.filenameSuffix = "settings";
    o.osxLibrarySubFolder = "Application Support";
    o.folderName = dir.getFullPathName();
    assist::Settings::setFileOptionsForTesting (o);
    const struct Restore
    {
        juce::File folder;
        ~Restore()
        {
            assist::Settings::setFileOptionsForTesting (std::nullopt);
            folder.deleteRecursively();
        }
    } restore { dir };

    Iaq8AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor = std::make_unique<DebugEditor> (processor); // not createEditor's since Plan 3
    LQ_CHECK (editor != nullptr, "editor created");

    if (editor == nullptr)
        return;

    const auto expected = juce::String (juce::CharPointer_UTF8 ("Probar conexi\xc3\xb3n"));
    juce::String found;
    for (auto* child : editor->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
            found = button->getButtonText();

    LQ_CHECK (found == expected, "button text is 'Probar conexión' (UTF-8)");

    const auto start = juce::Time::getMillisecondCounter();
    editor.reset();
    LQ_CHECK (juce::Time::getMillisecondCounter() - start < 1000, "closing the editor does not wait");
}
