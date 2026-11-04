// E1a host adapter tests: 8-band layout, audio path, neutral transparency,
// block-size determinism and multi-band automation through the processor.

#pragma clang diagnostic ignored "-Wfloat-equal" // bit-exactness is a tested property

#include "TestHarness.h"
#include "TestSignals.h"

#include "dsp/BandLimits.h"
#include "dsp/EqCore.h"
#include "parameters/ParameterIDs.h"
#include "plugin/PluginProcessor.h"

#include <set>

using labq8::LabQ8AudioProcessor;
using labq8::test::str;
namespace params = labq8::params;
namespace limits = labq8::dsp::limits;

namespace
{
void setParam (LabQ8AudioProcessor& p, const char* id, float value)
{
    auto* parameter = p.getValueTreeState().getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

float getParam (LabQ8AudioProcessor& p, const char* id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}

// Mirrors the adapter: what the DSP receives for a band.
labq8::dsp::BellSettings expectedSettings (LabQ8AudioProcessor& p, int band)
{
    const auto& ids = params::bandIDs[(size_t) band];
    double gain = getParam (p, ids.gain);
    if (std::abs (gain) < 0.005)
        gain = 0.0;
    return { getParam (p, ids.frequency), gain, getParam (p, ids.q), getParam (p, ids.bypass) >= 0.5f };
}

void setLayout (LabQ8AudioProcessor& p, int channels)
{
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    p.setBusesLayout (layout);
}

juce::AudioBuffer<float> noiseBuffer (int channels, int numSamples, unsigned seed)
{
    juce::AudioBuffer<float> b (channels, numSamples);
    for (int ch = 0; ch < channels; ++ch)
    {
        const auto n = labq8::test::signals::makeNoise ((size_t) numSamples, seed * 7 + (unsigned) ch);
        b.copyFrom (ch, 0, n.data(), numSamples);
    }
    return b;
}

// Distinct values per band, all within range.
void setAllBands (LabQ8AudioProcessor& p)
{
    const float freqs[] = { 3150.0f, 45.0f, 120.0f, 300.0f, 2200.0f, 4700.0f, 9000.0f, 15000.0f };
    for (int b = 0; b < params::numBands; ++b)
    {
        const auto& ids = params::bandIDs[(size_t) b];
        setParam (p, ids.frequency, freqs[b]);
        setParam (p, ids.gain, (b % 2 == 0 ? 1.0f : -1.0f) * (2.0f + (float) b));
        setParam (p, ids.q, 0.5f + 0.5f * (float) b);
        setParam (p, ids.bypass, b == 6 ? 1.0f : 0.0f);
    }
}
} // namespace

LQ_TEST ("processor: 8-band layout keeps E0 IDs and adds b2..b8 with version hint 2")
{
    LabQ8AudioProcessor p;
    const auto& all = p.getParameters();
    LQ_CHECK (all.size() == 80, str ("parameter count ", all.size())); // 32 in E1a; E1b appends 8 types, E2c.1 40 dynamics

    // E0 parameters keep their IDs, order (host indices 0..3) and hint.
    const char* e0[] = { "b1_freq", "b1_gain", "b1_q", "b1_bypass" };
    for (int i = 0; i < 4; ++i)
    {
        const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*> (all[i]);
        LQ_CHECK (ranged != nullptr && ranged->paramID == e0[i], str ("host index ", i, " is not ", e0[i]));
        LQ_CHECK (ranged != nullptr && ranged->getVersionHint() == params::versionHintE0, str (e0[i], " version hint"));
    }

    std::set<juce::String> seen;
    for (int b = 0; b < params::numBands; ++b)
    {
        const auto& ids = params::bandIDs[(size_t) b];
        const auto expectedHint = b == 0 ? params::versionHintE0 : params::versionHintE1a;
        const auto prefix = "b" + juce::String (b + 1) + "_";

        for (const auto* id : { ids.frequency, ids.gain, ids.q, ids.bypass })
        {
            auto* parameter = p.getValueTreeState().getParameter (id);
            LQ_CHECK (parameter != nullptr, str ("missing ", id));
            if (parameter == nullptr)
                continue;
            LQ_CHECK (juce::String (id).startsWith (prefix), str (id, " has the wrong band prefix"));
            LQ_CHECK (parameter->getVersionHint() == expectedHint, str (id, " version hint ", parameter->getVersionHint()));
            LQ_CHECK (parameter->isAutomatable(), str (id, " not automatable"));
            LQ_CHECK (seen.insert (id).second, str ("duplicate ID ", id));
        }

        auto& freq = *p.getValueTreeState().getParameter (ids.frequency);
        const auto defaultHz = freq.convertFrom0to1 (freq.getDefaultValue());
        LQ_CHECK (std::abs (defaultHz - (float) params::defaultFrequencyHz[(size_t) b]) < 1e-3f * defaultHz,
                  str ("band ", b + 1, " default frequency ", defaultHz));
        LQ_CHECK (freq.getNormalisableRange().start == (float) limits::minFrequencyHz
                      && freq.getNormalisableRange().end == (float) limits::maxFrequencyHz, "frequency range");

        auto& gain = *p.getValueTreeState().getParameter (ids.gain);
        LQ_CHECK (std::abs (gain.convertFrom0to1 (gain.getDefaultValue())) < 0.005f, str ("band ", b + 1, " default gain not 0"));
        auto& q = *p.getValueTreeState().getParameter (ids.q);
        LQ_CHECK (std::abs (q.convertFrom0to1 (q.getDefaultValue()) - 1.0f) < 1e-5f, str ("band ", b + 1, " default Q"));
        LQ_CHECK (p.getValueTreeState().getParameter (ids.bypass)->getDefaultValue() == 0.0f, "default bypass");
    }
}

LQ_TEST ("processor: default and all-0 dB states are bit-exact transparent")
{
    // In E0 the host's "0.00 dB" reached the DSP as about -5.4e-7 dB, so the
    // band never went idle. This test fails on dfcf25b.
    for (const auto channels : { 1, 2 })
        for (const auto fs : labq8::test::signals::sampleRates)
        {
            LabQ8AudioProcessor p;
            setLayout (p, channels);
            // arm64 (FMA) stores the default as -5.4e-7 dB; x86_64 as exactly 0.
            if (channels == 1 && fs == 44100.0)
                ctx.note (str ("default b1_gain on this architecture: ", getParam (p, params::id::band1Gain), " dB"));

            p.prepareToPlay (fs, 512);
            juce::MidiBuffer midi;
            bool identical = true;
            for (int block = 0; block < 50; ++block)
            {
                auto buffer = noiseBuffer (channels, 512, (unsigned) block);
                const auto input = buffer;
                p.processBlock (buffer, midi);
                for (int ch = 0; ch < channels; ++ch)
                    identical = identical && std::equal (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + 512, input.getReadPointer (ch));
            }
            LQ_CHECK (identical, str ("default state alters audio, channels=", channels, " fs=", fs));
        }
}

LQ_TEST ("processor: 8-band audio path matches the EQ core (mono and stereo, 44.1/48/96 kHz)")
{
    for (const auto channels : { 1, 2 })
        for (const auto fs : labq8::test::signals::sampleRates)
        {
            LabQ8AudioProcessor p;
            setLayout (p, channels);
            setAllBands (p);
            p.prepareToPlay (fs, 480);

            labq8::dsp::EqCore reference;
            for (int b = 0; b < params::numBands; ++b)
                reference.setBandTargets (b, expectedSettings (p, b));
            reference.prepare (fs, channels);

            juce::MidiBuffer midi;
            bool identical = true, finite = true;
            for (int block = 0; block < 200; ++block)
            {
                auto buffer = noiseBuffer (channels, 480, (unsigned) block);
                auto expected = buffer;
                p.processBlock (buffer, midi);
                reference.process (expected.getArrayOfWritePointers(), channels, 480);
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < 480; ++i)
                    {
                        identical = identical && buffer.getSample (ch, i) == expected.getSample (ch, i);
                        finite = finite && std::isfinite (buffer.getSample (ch, i));
                    }
            }
            LQ_CHECK (identical, str ("processor != EqCore, channels=", channels, " fs=", fs));
            LQ_CHECK (finite, "non-finite processor output");
        }
}

LQ_TEST ("processor: output is bit-identical for any host block size with 8-band automation")
{
    constexpr double fs = 48000.0;
    constexpr int total = 192000;  // 4 s
    constexpr int changeAt = 96000; // divisible by every block size below

    auto run = [&] (int blockSize)
    {
        LabQ8AudioProcessor p;
        setLayout (p, 2);
        setAllBands (p);
        p.prepareToPlay (fs, blockSize);
        juce::MidiBuffer midi;
        const auto input = noiseBuffer (2, total, 7);
        juce::AudioBuffer<float> out (2, total);

        for (int pos = 0; pos < total; pos += blockSize)
        {
            if (pos == changeAt)
                for (int b = 0; b < params::numBands; ++b)
                {
                    setParam (p, params::bandIDs[(size_t) b].gain, -getParam (p, params::bandIDs[(size_t) b].gain));
                    setParam (p, params::bandIDs[(size_t) b].bypass, b == 2 ? 1.0f : 0.0f);
                }

            juce::AudioBuffer<float> block (2, blockSize);
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, input, ch, pos, blockSize);
            p.processBlock (block, midi);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, block, ch, 0, blockSize);
        }
        return out;
    };

    const auto reference = run (480);
    for (const int size : { 1, 32, 64, 128, 4000 })
    {
        const auto y = run (size);
        bool same = true;
        for (int ch = 0; ch < 2; ++ch)
            same = same && std::equal (y.getReadPointer (ch), y.getReadPointer (ch) + total, reference.getReadPointer (ch));
        LQ_CHECK (same, str ("host block size ", size, " differs from 480"));
    }
}

LQ_TEST ("processor: simultaneous host automation of several bands")
{
    constexpr double fs = 48000.0;
    constexpr int blockSize = 256;
    LabQ8AudioProcessor p;
    p.prepareToPlay (fs, blockSize);
    juce::MidiBuffer midi;

    auto measure = [&] (double f)
    {
        // Steady state after 0.5 s, LS fit over the next 0.5 s.
        const auto n = (size_t) fs;
        const auto x = labq8::test::signals::makeSine (f, fs, n);
        labq8::test::signals::Signal y (n);
        for (size_t pos = 0; pos < n; pos += blockSize)
        {
            juce::AudioBuffer<float> b (2, blockSize);
            for (int ch = 0; ch < 2; ++ch)
                b.copyFrom (ch, 0, x.data() + pos, blockSize);
            p.processBlock (b, midi);
            std::copy (b.getReadPointer (0), b.getReadPointer (0) + blockSize, y.begin() + (long) pos);
        }
        using namespace labq8::test::signals;
        return toDb (fitAmplitude (y, n / 2, n, f, fs) / fitAmplitude (x, n / 2, n, f, fs));
    };

    // Three bands change in the same block.
    setParam (p, "b2_freq", 100.0f);  setParam (p, "b2_gain", 6.0f);  setParam (p, "b2_q", 1.0f);
    setParam (p, "b5_freq", 2500.0f); setParam (p, "b5_gain", -6.0f); setParam (p, "b5_q", 2.0f);
    setParam (p, "b8_freq", 12000.0f); setParam (p, "b8_gain", 9.0f); setParam (p, "b8_q", 0.7f);

    using labq8::test::signals::analogBellDb;
    for (const auto f : { 100.0, 2500.0, 12000.0, 700.0 })
    {
        const auto expected = analogBellDb (f, 100.0, 6.0, 1.0, fs) + analogBellDb (f, 2500.0, -6.0, 2.0, fs)
                            + analogBellDb (f, 12000.0, 9.0, 0.7, fs);
        const auto got = measure (f);
        LQ_CHECK (std::abs (got - expected) < labq8::test::signals::measureTolDb,
                  str ("measured ", got, " dB expected ", expected, " at ", f, " Hz"));
    }
}
