#include "TestHarness.h"

#include "plugin/Iaq8Processor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <random>

using namespace iaq8;

namespace
{
// The network probe of Plan 1's spike (docs/NETWORK_SPIKE.md), kept for the
// test below: a blocking HTTPS GET with no body and no credentials.
// statusCode is the HTTP status that arrived (any status proves the HTTPS
// path works); 0 means no HTTP response (see error).
struct HttpProbeResult
{
    int statusCode = 0;
    std::string error;
};

HttpProbeResult probeHttpStatus (const juce::String& url, int timeoutMs)
{
    HttpProbeResult result;
    int status = 0;
    auto stream = juce::URL (url).createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs (timeoutMs)
            .withStatusCode (&status));

    // A 4xx/5xx still yields a stream (or none) plus the status code.
    result.statusCode = status;
    if (status == 0)
        result.error = stream == nullptr ? "no response (network blocked or offline)" : "no HTTP status";
    return result;
}
} // namespace

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
    LQ_CHECK (processor.hasEditor(), "editor available");
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

// Network spike, headless half: the GET the debug editor's button did (Plan 1).
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
        ctx.skip ("no network or HTTPS failed: " + result.error);
        return;
    }

    ctx.note ("HTTP status: " + std::to_string (result.statusCode));
    LQ_CHECK (result.statusCode == 401, "expected 401 without key, got " + std::to_string (result.statusCode));
}
