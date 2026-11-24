#include "TestHarness.h"

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
// HTTPS path works. No connectivity -> skipped (never a false PASS).
LQ_TEST ("iaq8 smoke: HTTPS probe reaches api.anthropic.com (401 without key)")
{
    const auto result = probeHttpStatus ("https://api.anthropic.com/v1/models", 10000);

    if (result.statusCode == 0)
    {
        ctx.skip ("no network or HTTPS failed: " + result.error.toStdString());
        return;
    }

    ctx.note ("HTTP status: " + std::to_string (result.statusCode));
    LQ_CHECK (result.statusCode == 401, "expected 401 without key, got " + std::to_string (result.statusCode));
}
