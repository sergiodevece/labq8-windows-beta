// Host adapter tests: parameters, state, bus layouts and the processor's
// audio path, exercised headless (no DAW, no plug-in loading).

// Exact float comparison is intentional: bit-exactness is a tested property.
#pragma clang diagnostic ignored "-Wfloat-equal"

#include "TestHarness.h"

#include "dsp/BandLimits.h"
#include "dsp/BellBand.h"
#include "parameters/ParameterIDs.h"
#include "plugin/PluginProcessor.h"

#include <cmath>
#include <random>

using labq8::LabQ8AudioProcessor;
using labq8::test::str;
namespace id = labq8::params::id;
namespace limits = labq8::dsp::limits;

namespace
{
juce::RangedAudioParameter& param (LabQ8AudioProcessor& p, const char* parameterID)
{
    auto* parameter = p.getValueTreeState().getParameter (parameterID);
    jassert (parameter != nullptr);
    return *parameter;
}

void setParam (LabQ8AudioProcessor& p, const char* parameterID, float value)
{
    auto& parameter = param (p, parameterID);
    parameter.setValueNotifyingHost (parameter.convertTo0to1 (value));
}

float getParam (LabQ8AudioProcessor& p, const char* parameterID)
{
    return p.getValueTreeState().getRawParameterValue (parameterID)->load();
}

// Relative tolerance for values that go through the 0..1 normalisation:
// float32 round-trip through log mappings loses ~1e-6 relative.
bool closeTo (float a, float b, float relTol = 1.0e-5f)
{
    return std::abs (a - b) <= relTol * std::max (1.0f, std::abs (b));
}

juce::AudioBuffer<float> makeNoiseBuffer (int channels, int numSamples, unsigned seed)
{
    juce::AudioBuffer<float> buffer (channels, numSamples);
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample (ch, i, dist (rng));
    return buffer;
}

bool setLayout (LabQ8AudioProcessor& p, const juce::AudioChannelSet& in, const juce::AudioChannelSet& out)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (in);
    layout.outputBuses.add (out);
    return p.setBusesLayout (layout);
}
} // namespace

LQ_TEST ("processor: parameter layout exposes stable IDs, ranges and defaults")
{
    LabQ8AudioProcessor p;
    // Was 4 in E0 (band 1 only), 32 in E1a (4 per band), 40 in E1b (+ b1..b8_type),
    // 80 in E2c.1 (+ 5 dynamic parameters per band).
    // Band 1 checks below are unchanged.
    LQ_CHECK (p.getParameters().size() == 80, str ("expected 80 parameters, got ", p.getParameters().size()));

    struct Expected { const char* id; float lo, hi, def; };
    const Expected expected[] = {
        { id::band1Frequency, (float) limits::minFrequencyHz, (float) limits::maxFrequencyHz, (float) limits::defaultFrequencyHz },
        { id::band1Gain, (float) limits::minGainDb, (float) limits::maxGainDb, (float) limits::defaultGainDb },
        { id::band1Q, (float) limits::minQ, (float) limits::maxQ, (float) limits::defaultQ },
        { id::band1Bypass, 0.0f, 1.0f, 0.0f },
    };

    for (const auto& e : expected)
    {
        auto* parameter = p.getValueTreeState().getParameter (e.id);
        LQ_CHECK (parameter != nullptr, str ("missing parameter ", e.id));
        if (parameter == nullptr)
            continue;

        const auto& range = parameter->getNormalisableRange();
        LQ_CHECK (range.start == e.lo && range.end == e.hi, str ("range of ", e.id));
        LQ_CHECK (closeTo (parameter->convertFrom0to1 (parameter->getDefaultValue()), e.def), str ("default of ", e.id));
        LQ_CHECK (closeTo (getParam (p, e.id), e.def), str ("initial value of ", e.id));
        LQ_CHECK (parameter->isAutomatable(), str (e.id, " must be automatable"));
    }

    // Log mapping: 1 kHz should sit near the middle of the 20 Hz..20 kHz knob.
    auto& freq = param (p, id::band1Frequency);
    const auto mid = freq.convertTo0to1 (632.456f); // geometric centre
    LQ_CHECK (std::abs (mid - 0.5f) < 1e-4f, str ("frequency mapping not logarithmic: ", mid));
}

LQ_TEST ("processor: state round-trip restores every parameter")
{
    LabQ8AudioProcessor a;
    setParam (a, id::band1Frequency, 3150.0f);
    setParam (a, id::band1Gain, -7.25f);
    setParam (a, id::band1Q, 4.5f);
    setParam (a, id::band1Bypass, 1.0f);

    juce::MemoryBlock block;
    a.getStateInformation (block);
    LQ_CHECK (block.getSize() > 0, "empty state");

    const auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    LQ_CHECK (xml != nullptr && xml->hasTagName (LabQ8AudioProcessor::stateType), "state root tag");
    LQ_CHECK (xml != nullptr && xml->getIntAttribute (LabQ8AudioProcessor::stateVersionProperty) == LabQ8AudioProcessor::stateVersion,
              "state version attribute");

    LabQ8AudioProcessor b;
    setParam (b, id::band1Gain, 18.0f); // must be overwritten
    b.setStateInformation (block.getData(), (int) block.getSize());

    LQ_CHECK (closeTo (getParam (b, id::band1Frequency), 3150.0f), str ("freq ", getParam (b, id::band1Frequency)));
    LQ_CHECK (closeTo (getParam (b, id::band1Gain), -7.25f), str ("gain ", getParam (b, id::band1Gain)));
    LQ_CHECK (closeTo (getParam (b, id::band1Q), 4.5f), str ("q ", getParam (b, id::band1Q)));
    LQ_CHECK (getParam (b, id::band1Bypass) >= 0.5f, "bypass");

    // Round-trip is idempotent.
    juce::MemoryBlock block2;
    b.getStateInformation (block2);
    LQ_CHECK (block == block2, "second save differs from the first");
}

LQ_TEST ("processor: corrupt or foreign state is ignored")
{
    LabQ8AudioProcessor p;
    setParam (p, id::band1Frequency, 440.0f);
    setParam (p, id::band1Gain, 3.0f);

    const char garbage[] = "definitely not a plugin state";
    p.setStateInformation (garbage, (int) sizeof (garbage));
    p.setStateInformation (nullptr, 0);

    juce::MemoryBlock foreign;
    juce::AudioProcessor::copyXmlToBinary (juce::XmlElement ("LAB2A"), foreign);
    p.setStateInformation (foreign.getData(), (int) foreign.getSize());

    LQ_CHECK (closeTo (getParam (p, id::band1Frequency), 440.0f), "frequency changed by bad state");
    LQ_CHECK (closeTo (getParam (p, id::band1Gain), 3.0f), "gain changed by bad state");
}

LQ_TEST ("processor: only matching mono or stereo layouts are supported")
{
    using Set = juce::AudioChannelSet;
    LabQ8AudioProcessor p;

    LQ_CHECK (setLayout (p, Set::mono(), Set::mono()), "mono -> mono");
    LQ_CHECK (setLayout (p, Set::stereo(), Set::stereo()), "stereo -> stereo");
    LQ_CHECK (! setLayout (p, Set::mono(), Set::stereo()), "mono -> stereo must be rejected");
    LQ_CHECK (! setLayout (p, Set::stereo(), Set::mono()), "stereo -> mono must be rejected");
    LQ_CHECK (! setLayout (p, Set::create5point1(), Set::create5point1()), "5.1 must be rejected");
    LQ_CHECK (! setLayout (p, Set::disabled(), Set::disabled()), "disabled must be rejected");
}

LQ_TEST ("processor: audio path matches the DSP core (mono and stereo, 44.1/48/96 kHz)")
{
    using Set = juce::AudioChannelSet;

    for (const auto channels : { 1, 2 })
        for (const auto fs : { 44100.0, 48000.0, 96000.0 })
        {
            LabQ8AudioProcessor p;
            const auto set = channels == 1 ? Set::mono() : Set::stereo();
            LQ_CHECK (setLayout (p, set, set), "layout");

            setParam (p, id::band1Frequency, 3150.0f);
            setParam (p, id::band1Gain, -7.25f);
            setParam (p, id::band1Q, 4.5f);

            constexpr int blockSize = 480;
            p.prepareToPlay (fs, blockSize);

            labq8::dsp::BellBand reference;
            reference.setTargets ({ getParam (p, id::band1Frequency), getParam (p, id::band1Gain),
                                    getParam (p, id::band1Q), false });
            reference.prepare (fs, channels);

            juce::MidiBuffer midi;
            bool identical = true, finite = true;

            for (int block = 0; block < 200; ++block)
            {
                auto buffer = makeNoiseBuffer (channels, blockSize, (unsigned) block);
                auto expected = buffer;
                p.processBlock (buffer, midi);
                reference.process (expected.getArrayOfWritePointers(), channels, blockSize);

                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < blockSize; ++i)
                    {
                        identical = identical && buffer.getSample (ch, i) == expected.getSample (ch, i);
                        finite = finite && std::isfinite (buffer.getSample (ch, i));
                    }
            }

            LQ_CHECK (identical, str ("processor output differs from DSP core, channels=", channels, " fs=", fs));
            LQ_CHECK (finite, "non-finite processor output");
            p.releaseResources();
        }
}

LQ_TEST ("processor: follows host automation between blocks")
{
    LabQ8AudioProcessor p;
    constexpr double fs = 48000.0;
    constexpr int blockSize = 256;
    setParam (p, id::band1Frequency, 1000.0f);
    setParam (p, id::band1Gain, 0.0f);
    p.prepareToPlay (fs, blockSize);

    juce::MidiBuffer midi;
    int sampleIndex = 0;

    // Returns the gain (dB) of a 1 kHz sine over `blocks` blocks, measured in the last half.
    auto runSine = [&] (int blocks)
    {
        double inSq = 0.0, outSq = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int i = 0; i < blockSize; ++i, ++sampleIndex)
            {
                const auto v = static_cast<float> (0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * sampleIndex / fs));
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
                if (b >= blocks / 2)
                    inSq += (double) v * v;
            }
            p.processBlock (buffer, midi);
            if (b >= blocks / 2)
                for (int i = 0; i < blockSize; ++i)
                    outSq += (double) buffer.getSample (0, i) * buffer.getSample (0, i);
        }
        return 10.0 * std::log10 (outSq / inSq);
    };

    // RMS over whole periods (48 samples per 1 kHz period, 256*100 = 25600 = 533.3 periods):
    // the partial period bounds the RMS error to ~0.01 dB, hence 0.02 dB here.
    const auto g0 = runSine (200);
    LQ_CHECK (std::abs (g0) < 0.02, str ("0 dB gain measured ", g0));

    setParam (p, id::band1Gain, 12.0f);
    const auto g12 = runSine (200);
    LQ_CHECK (std::abs (g12 - 12.0) < 0.02, str ("+12 dB gain measured ", g12));

    setParam (p, id::band1Bypass, 1.0f);
    const auto gBypass = runSine (200);
    LQ_CHECK (std::abs (gBypass) < 0.02, str ("bypass measured ", gBypass));
}

LQ_TEST ("processor: development editor can be created")
{
    LabQ8AudioProcessor p;
    LQ_CHECK (p.hasEditor(), "hasEditor");
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    LQ_CHECK (editor != nullptr, "createEditor returned null");
    editor.reset();
}
