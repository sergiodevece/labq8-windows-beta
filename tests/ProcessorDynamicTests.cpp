// E2c.1 public dynamic parameters (numbers refer to the E2c.1 test list):
// layout, IDs, order, hints and defaults (1, 2, 3); v4 round-trip (4);
// migration of every earlier state (5); eight bands automated independently,
// mono and stereo (6, 12); enable during playback (7); fast changes (8);
// Bell only (9); transparency with dynamics off (10); NaN / Inf and
// allocations (13); processor CPU (14).

#pragma clang diagnostic ignored "-Wfloat-equal" // bit-exactness is a tested property

#include "AllocationGuard.h"
#include "TestHarness.h"
#include "TestSignals.h"

#include "dsp/EqCore.h"
#include "parameters/ParameterIDs.h"
#include "plugin/PluginProcessor.h"

#include <chrono>
#include <limits>
#include <random>

using labq8::LabQ8AudioProcessor;
using labq8::test::ScopedAllocationCounter;
using labq8::test::str;
using namespace labq8::test::signals;
namespace params = labq8::params;
namespace dsp = labq8::dsp;

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

juce::MemoryBlock save (LabQ8AudioProcessor& p)
{
    juce::MemoryBlock block;
    p.getStateInformation (block);
    return block;
}

void load (LabQ8AudioProcessor& p, const juce::MemoryBlock& block)
{
    p.setStateInformation (block.getData(), (int) block.getSize());
}

juce::MemoryBlock loadFixture (const char* name)
{
    juce::MemoryBlock block;
    juce::File (LABQ8_TEST_FIXTURES_DIR).getChildFile (name).loadFileAsData (block);
    return block;
}

void setLayout (LabQ8AudioProcessor& p, int channels)
{
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    p.setBusesLayout (layout);
}

// What the DSP should receive for a band (mirrors the adapter).
dsp::BandSettings expectedBand (LabQ8AudioProcessor& p, int band)
{
    const auto& ids = params::bandIDs[(size_t) band];
    double gain = getParam (p, ids.gain);
    if (std::abs (gain) < 0.005)
        gain = 0.0;
    return { getParam (p, ids.frequency), gain, getParam (p, ids.q), getParam (p, ids.bypass) >= 0.5f,
             dsp::filterTypeFromIndex ((int) std::lround (getParam (p, ids.type))) };
}

dsp::DynamicSettings expectedDynamics (LabQ8AudioProcessor& p, int band)
{
    const auto& ids = params::dynamicIDs[(size_t) band];
    return { getParam (p, ids.enabled) >= 0.5f, getParam (p, ids.threshold), getParam (p, ids.range),
             getParam (p, ids.attack), getParam (p, ids.release) };
}

void pushExpected (LabQ8AudioProcessor& p, dsp::EqCore& core)
{
    for (int b = 0; b < params::numBands; ++b)
    {
        core.setBandTargets (b, expectedBand (p, b));
        core.setBandDynamics (b, expectedDynamics (p, b));
    }
}

constexpr float bandFreqs[params::numBands] = { 60.0f, 150.0f, 400.0f, 1000.0f, 2500.0f, 5000.0f, 9000.0f, 15000.0f };

// Eight Bells on separate frequencies; dynamics on with a different setting per band.
void setEightDynamicBells (LabQ8AudioProcessor& p)
{
    for (int b = 0; b < params::numBands; ++b)
    {
        const auto& ids = params::bandIDs[(size_t) b];
        const auto& dyn = params::dynamicIDs[(size_t) b];
        setParam (p, ids.frequency, bandFreqs[b]);
        setParam (p, ids.gain, b % 2 ? 2.0f : -1.0f);
        setParam (p, ids.q, 3.0f);
        setParam (p, dyn.enabled, 1.0f);
        setParam (p, dyn.threshold, -36.0f + 2.0f * (float) b);
        setParam (p, dyn.range, 4.0f + 2.0f * (float) b);
        setParam (p, dyn.attack, 1.0f + 3.0f * (float) b);
        setParam (p, dyn.release, 20.0f + 40.0f * (float) b);
    }
}

// Eight amplitude-modulated tones, one per band, plus noise.
juce::AudioBuffer<float> program (int channels, int numSamples, double fs, unsigned seed)
{
    juce::AudioBuffer<float> b (channels, numSamples);
    const auto noise = makeNoise ((size_t) numSamples * (size_t) channels, seed, 0.03f);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < numSamples; ++i)
        {
            double v = noise[(size_t) (ch * numSamples + i)];
            for (int k = 0; k < params::numBands; ++k)
            {
                const auto env = std::pow (10.0, (-40.0 + 34.0 * (0.5 + 0.5 * std::sin (2.0 * std::numbers::pi * (0.9 + k) * i / fs))) / 20.0);
                v += env * std::sin (2.0 * std::numbers::pi * bandFreqs[k] * i / fs + ch);
            }
            b.setSample (ch, i, (float) v);
        }
    return b;
}

bool equalBuffers (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (! std::equal (a.getReadPointer (ch), a.getReadPointer (ch) + a.getNumSamples(), b.getReadPointer (ch)))
            return false;
    return true;
}

double maxFourthDifference (const float* y, int n)
{
    double m = 0.0;
    for (int i = 4; i < n; ++i)
        m = std::max (m, std::abs ((double) y[i] - 4.0 * y[i - 1] + 6.0 * y[i - 2] - 4.0 * y[i - 3] + y[i - 4]));
    return m;
}
} // namespace

//==============================================================================
LQ_TEST ("e2c processor: 80 parameters; the 40 dynamic ones appended as indices 40..79 with hint 4 and their defaults (1, 2, 3)")
{
    LabQ8AudioProcessor p;
    const auto& all = p.getParameters();
    LQ_CHECK (all.size() == 80 && params::numParameters == 80, str ("parameter count ", all.size()));

    // 0..39 unchanged (checked in detail by the E1a/E1b tests): spot-check ends.
    const auto idAt = [&] (int i)
    {
        const auto* r = dynamic_cast<const juce::RangedAudioParameter*> (all[i]);
        return r != nullptr ? r->paramID : juce::String();
    };
    LQ_CHECK (idAt (0) == "b1_freq" && idAt (31) == "b8_bypass" && idAt (32) == "b1_type" && idAt (39) == "b8_type",
              "indices 0..39 must keep the E1b order");

    namespace dl = dsp::dynamics::limits;
    int index = 40;
    for (int b = 0; b < params::numBands; ++b)
    {
        const auto& ids = params::dynamicIDs[(size_t) b];
        const auto prefix = "b" + std::to_string (b + 1) + "_";
        const std::string expectedIds[] = { prefix + "dyn_on", prefix + "threshold", prefix + "range", prefix + "attack", prefix + "release" };
        const char* actual[] = { ids.enabled, ids.threshold, ids.range, ids.attack, ids.release };
        for (int k = 0; k < 5; ++k, ++index)
        {
            const auto* r = dynamic_cast<const juce::RangedAudioParameter*> (all[index]);
            LQ_CHECK (r != nullptr && r->paramID == juce::String (expectedIds[k]) && expectedIds[k] == actual[k],
                      str ("host index ", index, " is not ", expectedIds[k]));
            LQ_CHECK (r != nullptr && r->getVersionHint() == params::versionHintE2c && params::versionHintE2c == 4,
                      str (expectedIds[k], " version hint"));
            LQ_CHECK (r != nullptr && r->isAutomatable(), str (expectedIds[k], " must be automatable"));
        }

        // Defaults and ranges (the validated DSP ranges, unchanged).
        const auto* on = dynamic_cast<const juce::AudioParameterBool*> (p.getValueTreeState().getParameter (ids.enabled));
        LQ_CHECK (on != nullptr && on->get() == false
                  && static_cast<const juce::AudioProcessorParameter*> (on)->getDefaultValue() == 0.0f, "dynamics off by default");
        struct Expect { const char* id; double lo, hi, def; };
        for (const auto& e : { Expect { ids.threshold, dl::minThresholdDb, dl::maxThresholdDb, -20.0 },
                               Expect { ids.range, dl::minRangeDb, dl::maxRangeDb, 6.0 },
                               Expect { ids.attack, dl::minAttackMs, dl::maxAttackMs, 10.0 },
                               Expect { ids.release, dl::minReleaseMs, dl::maxReleaseMs, 100.0 } })
        {
            const auto* f = dynamic_cast<const juce::AudioParameterFloat*> (p.getValueTreeState().getParameter (e.id));
            LQ_CHECK (f != nullptr && f->range.start == (float) e.lo && f->range.end == (float) e.hi,
                      str (e.id, " range ", f->range.start, " .. ", f->range.end));
            LQ_CHECK (f != nullptr && std::abs (f->get() - (float) e.def) < 1.0e-4f * std::max (1.0, std::abs (e.def)),
                      str (e.id, " default ", f->get()));
        }
    }
    LQ_CHECK (dl::minThresholdDb == -80.0 && dl::maxThresholdDb == 24.0 && dl::minRangeDb == 0.0 && dl::maxRangeDb == 24.0
                  && dl::minAttackMs == 0.5 && dl::maxAttackMs == 500.0 && dl::minReleaseMs == 5.0 && dl::maxReleaseMs == 5000.0,
              "public ranges are the validated DSP ranges");
}

LQ_TEST ("e2c state: v4 round-trip of the eight bands' dynamic parameters (4)")
{
    LabQ8AudioProcessor a;
    setEightDynamicBells (a);
    setParam (a, "b3_dyn_on", 0.0f);
    setParam (a, "b6_threshold", 17.3f);
    setParam (a, "b7_attack", 0.5f);
    setParam (a, "b8_release", 5000.0f);
    const auto block = save (a);

    const auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    LQ_CHECK (xml != nullptr && xml->getIntAttribute (params::stateVersionProperty) == 4 && xml->getNumChildElements() == 80,
              "saved state is v4 with 80 parameters");

    LabQ8AudioProcessor b;
    load (b, block);
    for (const auto* parameter : b.getParameters())
    {
        const auto* r = dynamic_cast<const juce::RangedAudioParameter*> (parameter);
        const auto id = r->paramID.toRawUTF8();
        LQ_CHECK (getParam (a, id) == getParam (b, id), str ("round-trip changed ", id, ": ", getParam (a, id), " -> ", getParam (b, id)));
    }
    LQ_CHECK (save (b) == block, "save after load differs");
    LQ_CHECK (getParam (b, "b3_dyn_on") == 0.0f && getParam (b, "b4_dyn_on") == 1.0f, "per-band enable restored individually");

    // Save / load is a fixed point for any value of the dynamic parameters
    // (attack and release are snapped to a grid). Existing log parameters
    // (frequency, Q: frozen since E0) are probed and reported, not changed.
    std::mt19937 rng (5);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    int dynamicDrift = 0, frozenDrift = 0;
    for (int trial = 0; trial < 200; ++trial)
    {
        LabQ8AudioProcessor c;
        for (int band = 0; band < params::numBands; ++band)
            for (const auto* id : { params::dynamicIDs[(size_t) band].threshold, params::dynamicIDs[(size_t) band].range,
                                    params::dynamicIDs[(size_t) band].attack, params::dynamicIDs[(size_t) band].release,
                                    params::bandIDs[(size_t) band].frequency, params::bandIDs[(size_t) band].q })
                c.getValueTreeState().getParameter (id)->setValueNotifyingHost (u (rng));
        const auto first = save (c);
        LabQ8AudioProcessor d;
        load (d, first);
        for (int band = 0; band < params::numBands; ++band)
        {
            for (const auto* id : { params::dynamicIDs[(size_t) band].threshold, params::dynamicIDs[(size_t) band].range,
                                    params::dynamicIDs[(size_t) band].attack, params::dynamicIDs[(size_t) band].release })
                dynamicDrift += getParam (c, id) != getParam (d, id) ? 1 : 0;
            for (const auto* id : { params::bandIDs[(size_t) band].frequency, params::bandIDs[(size_t) band].q })
                frozenDrift += getParam (c, id) != getParam (d, id) ? 1 : 0;
        }
    }
    LQ_CHECK (dynamicDrift == 0, str (dynamicDrift, " dynamic values changed in a save / load cycle"));
    ctx.note (str ("frozen log parameters (frequency, Q) that moved by an ulp in one save / load cycle: ", frozenDrift, " of 3200"));
}

LQ_TEST ("e2c state: every earlier state (E0, E1a, E1b, E1b.1/E2a/E2b) loads with dynamics off, deterministically (5)")
{
    // E1b.1, E2a and E2b save the same v3 format as E1b; e2b_custom is a
    // genuine E2b (5623dc5) state.
    for (const auto* file : { "e0_default.bin", "e0_custom.bin", "e0_bypassed.bin", "e1a_default.bin", "e1a_custom.bin",
                              "e1a_from_e0_custom.bin", "e1b_high_q.bin", "e1b_within_limits.bin", "e2b_custom.bin" })
    {
        const auto old = loadFixture (file);
        LQ_CHECK (old.getSize() > 0, str ("fixture missing: ", file));
        const auto oldXml = juce::AudioProcessor::getXmlFromBinary (old.getData(), (int) old.getSize());

        LabQ8AudioProcessor fresh;
        load (fresh, old);

        LabQ8AudioProcessor used; // history: every dynamic parameter moved and enabled
        setEightDynamicBells (used);
        load (used, old);

        for (auto* p : { &fresh, &used })
            for (int b = 0; b < params::numBands; ++b)
            {
                const auto d = expectedDynamics (*p, b);
                LQ_CHECK (! d.enabled && std::abs (d.thresholdDb + 20.0) < 1e-4 && std::abs (d.rangeDb - 6.0) < 1e-4
                              && std::abs (d.attackMs - 10.0) < 1e-3 && std::abs (d.releaseMs - 100.0) < 1e-2,
                          str (file, ": band ", b + 1, " dynamics not at their defaults"));
            }

        // Stored static values come back unchanged (same tolerance as the E1a test).
        if (oldXml != nullptr)
            for (auto* param : oldXml->getChildWithTagNameIterator ("PARAM"))
            {
                const auto stored = (float) param->getDoubleAttribute ("value");
                const auto got = getParam (fresh, param->getStringAttribute ("id").toRawUTF8());
                LQ_CHECK (std::abs (got - stored) <= 5.0e-7f * std::max (24.0f, std::abs (stored)),
                          str (file, ": ", param->getStringAttribute ("id"), " = ", got, " stored ", stored));
            }

        const auto a = save (fresh), b = save (used);
        LQ_CHECK (a == b, str (file, ": migrated state depends on instance history"));
        const auto migrated = juce::AudioProcessor::getXmlFromBinary (a.getData(), (int) a.getSize());
        bool allValues = migrated != nullptr && migrated->getIntAttribute (params::stateVersionProperty) == 4
                      && migrated->getNumChildElements() == 80;
        if (migrated != nullptr)
            for (auto* child : migrated->getChildIterator())
                allValues = allValues && child->hasAttribute ("value");
        LQ_CHECK (allValues, str (file, ": re-saved as v4 with 80 explicit values"));

        // Old sessions play exactly the static E2b path: no section active.
        LabQ8AudioProcessor player;
        setLayout (player, 2);
        load (player, old);
        player.prepareToPlay (48000.0, 480);
        dsp::EqCore reference;
        for (int band = 0; band < params::numBands; ++band)
            reference.setBandTargets (band, expectedBand (player, band));
        reference.prepare (48000.0, 2);
        juce::MidiBuffer midi;
        bool identical = true;
        for (int block = 0; block < 50; ++block)
        {
            auto buffer = program (2, 480, 48000.0, 900u + (unsigned) block);
            auto expected = buffer;
            player.processBlock (buffer, midi);
            reference.process (expected.getArrayOfWritePointers(), 2, 480);
            identical = identical && equalBuffers (buffer, expected);
        }
        LQ_CHECK (identical, str (file, ": does not play exactly the static path"));
    }
}

LQ_TEST ("e2c processor: eight bands automated independently equal the core fed band by band, mono and stereo (6, 8, 12)")
{
    for (const int channels : { 1, 2 })
        for (const auto fs : { 44100.0, 96000.0 })
        {
            LabQ8AudioProcessor p;
            setLayout (p, channels);
            setEightDynamicBells (p);
            p.prepareToPlay (fs, 256);
            dsp::EqCore reference;
            pushExpected (p, reference);
            reference.prepare (fs, channels);

            std::mt19937 rng (77u + (unsigned) channels);
            std::uniform_real_distribution<float> u (0.0f, 1.0f);
            juce::MidiBuffer midi;
            bool identical = true, finite = true;
            const auto input = program (channels, 256 * 200, fs, 31);
            for (int block = 0; block < 200; ++block)
            {
                // Fast changes: every block, one band's threshold, range, attack
                // and release jump to random values across their whole range.
                const auto& dyn = params::dynamicIDs[(size_t) (block % 8)];
                setParam (p, dyn.threshold, -80.0f + 104.0f * u (rng));
                setParam (p, dyn.range, 24.0f * u (rng));
                setParam (p, dyn.attack, 0.5f * std::pow (1000.0f, u (rng)));
                setParam (p, dyn.release, 5.0f * std::pow (1000.0f, u (rng)));
                pushExpected (p, reference);

                juce::AudioBuffer<float> buffer (channels, 256);
                for (int ch = 0; ch < channels; ++ch)
                    buffer.copyFrom (ch, 0, input, ch, block * 256, 256);
                auto expected = buffer;
                p.processBlock (buffer, midi);
                reference.process (expected.getArrayOfWritePointers(), channels, 256);
                identical = identical && equalBuffers (buffer, expected);
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < 256; ++i)
                        finite = finite && std::isfinite (buffer.getSample (ch, i));
            }
            LQ_CHECK (identical, str ("processor != core with per-band dynamics, channels=", channels, " fs=", fs));
            LQ_CHECK (finite, "finite output under fast changes");
        }
}

LQ_TEST ("e2c processor: host automation is independent of the host block size (12)")
{
    constexpr double fs = 48000.0;
    constexpr int total = 96000, changeAt = 48000; // 48000 is a multiple of every block size used
    auto run = [&] (int blockSize)
    {
        LabQ8AudioProcessor p;
        setLayout (p, 2);
        setEightDynamicBells (p);
        p.prepareToPlay (fs, blockSize);
        juce::MidiBuffer midi;
        const auto input = program (2, total, fs, 41);
        juce::AudioBuffer<float> out (2, total);
        for (int pos = 0; pos < total; pos += blockSize)
        {
            if (pos == changeAt)
                for (int b = 0; b < params::numBands; ++b)
                {
                    const auto& dyn = params::dynamicIDs[(size_t) b];
                    setParam (p, dyn.enabled, b % 3 == 0 ? 0.0f : 1.0f);
                    setParam (p, dyn.threshold, -30.0f + (float) b);
                    setParam (p, dyn.attack, 0.5f + (float) b);
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
    for (const int block : { 32, 64, 160, 960 })
        LQ_CHECK (equalBuffers (run (block), reference), str ("host block ", block, " differs"));
}

LQ_TEST ("e2c processor: Dynamic Enable during playback is smooth; dynamics only on Bell (7, 9)")
{
    constexpr double fs = 48000.0;
    // (7) Toggle b4_dyn_on every 50 ms on a loud 1 kHz tone 30 dB over the threshold.
    {
        LabQ8AudioProcessor p;
        setLayout (p, 1);
        setParam (p, "b4_freq", 1000.0f);
        setParam (p, "b4_q", 2.0f);
        setParam (p, "b4_threshold", -36.0f);
        setParam (p, "b4_range", 12.0f);
        setParam (p, "b4_attack", 2.0f);
        p.prepareToPlay (fs, 240);
        juce::MidiBuffer midi;
        const auto tone = makeSine (1000.0, fs, (size_t) fs, 0.1);
        juce::AudioBuffer<float> out (1, (int) fs);
        for (int pos = 0; pos < (int) fs; pos += 240)
        {
            if (pos % 2400 == 0)
                setParam (p, "b4_dyn_on", (pos / 2400) % 2 == 0 ? 1.0f : 0.0f);
            juce::AudioBuffer<float> block (1, 240);
            block.copyFrom (0, 0, tone.data() + pos, 240);
            p.processBlock (block, midi);
            out.copyFrom (0, pos, block, 0, 0, 240);
        }
        // Steady 4th difference of the tone (amplitude between 0.1 and 0.025)
        // plus the -60 dBFS-step bound used since E1b.
        const auto steady = 0.1 * std::pow (2.0 * std::numbers::pi * 1000.0 / fs, 4.0);
        const auto measured = maxFourthDifference (out.getReadPointer (0), out.getNumSamples());
        LQ_CHECK (measured <= steady + 3e-3, str ("toggling Dynamic EQ: 4th difference ", measured, " vs steady ", steady));
        const auto onLevel = toDb (fitAmplitude (Signal (out.getReadPointer (0) + 1500, out.getReadPointer (0) + 2400), 0, 900, 1000.0, fs) / 0.1);
        LQ_CHECK (onLevel < -10.0, str ("reduction while on: ", onLevel, " dB"));
    }

    // (9) Dynamics on a non-Bell band do nothing: same output as dynamics off.
    for (const auto type : { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f })
    {
        auto render = [&] (bool dynOn)
        {
            LabQ8AudioProcessor p;
            setLayout (p, 2);
            setParam (p, "b4_type", type);
            setParam (p, "b4_freq", 1000.0f);
            setParam (p, "b4_gain", 6.0f);
            setParam (p, "b4_dyn_on", dynOn ? 1.0f : 0.0f);
            setParam (p, "b4_threshold", -60.0f);
            setParam (p, "b4_range", 24.0f);
            p.prepareToPlay (fs, 480);
            juce::MidiBuffer midi;
            auto buffer = program (2, 48000, fs, 51);
            for (int pos = 0; pos < 48000; pos += 480)
            {
                juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, pos, 480);
                p.processBlock (view, midi);
            }
            return buffer;
        };
        LQ_CHECK (equalBuffers (render (true), render (false)), str ("type ", type, ": dynamics must not act on non-Bell types"));
    }
}

LQ_TEST ("e2c processor: dynamics off is transparent and equals the static path, whatever the dynamic settings (10, 11)")
{
    constexpr double fs = 44100.0;
    auto render = [&] (bool changeDynamicSettings)
    {
        LabQ8AudioProcessor p;
        setLayout (p, 2);
        for (int b = 0; b < params::numBands; ++b)
        {
            setParam (p, params::bandIDs[(size_t) b].frequency, bandFreqs[b]);
            setParam (p, params::bandIDs[(size_t) b].gain, (float) (b - 4));
            if (changeDynamicSettings)
            {
                const auto& dyn = params::dynamicIDs[(size_t) b];
                setParam (p, dyn.threshold, -70.0f);
                setParam (p, dyn.range, 24.0f);
                setParam (p, dyn.attack, 0.5f);
                setParam (p, dyn.release, 5.0f);
            }
        }
        p.prepareToPlay (fs, 512);
        juce::MidiBuffer midi;
        auto buffer = program (2, 44100, fs, 61);
        for (int pos = 0; pos < 44100; pos += 441)
        {
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, pos, 441);
            p.processBlock (view, midi);
        }
        return buffer;
    };
    LQ_CHECK (equalBuffers (render (true), render (false)), "dynamic settings must not matter while Dynamic EQ is off");

    // Default plugin: bit-exact pass-through (all bands at 0 dB, dynamics off).
    LabQ8AudioProcessor p;
    setLayout (p, 2);
    p.prepareToPlay (fs, 512);
    juce::MidiBuffer midi;
    auto buffer = program (2, 512, fs, 62);
    const auto input = buffer;
    p.processBlock (buffer, midi);
    LQ_CHECK (equalBuffers (buffer, input), "default plugin must be a bit-exact pass-through");
}

LQ_TEST ("e2c rt: no allocation and finite output with eight dynamic bands, automation and NaN/Inf input (13)")
{
    constexpr double fs = 48000.0;
    LabQ8AudioProcessor p;
    setLayout (p, 2);
    setEightDynamicBells (p);
    p.prepareToPlay (fs, 128);
    juce::MidiBuffer midi;
    auto input = program (2, 128 * 300, fs, 71);
    const auto nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    for (int i = 0; i < 64; ++i)
    {
        input.setSample (0, 10000 + i, nan);
        input.setSample (1, 20000 + i, inf);
        input.setSample (0, 30000 + i, -1.0e30f);
    }
    // Automation values computed before the measured region (the host writes
    // parameters from its own thread; only processBlock is measured).
    std::vector<float> values (300 * 5);
    std::mt19937 rng (72);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    for (auto& v : values)
        v = u (rng);

    long allocations = 0;
    bool finite = true;
    for (int block = 0; block < 300; ++block)
    {
        const auto& dyn = params::dynamicIDs[(size_t) (block % 8)];
        setParam (p, dyn.enabled, values[(size_t) block * 5] < 0.8f ? 1.0f : 0.0f);
        setParam (p, dyn.threshold, -80.0f + 104.0f * values[(size_t) block * 5 + 1]);
        setParam (p, dyn.range, 24.0f * values[(size_t) block * 5 + 2]);
        juce::AudioBuffer<float> view (input.getArrayOfWritePointers(), 2, block * 128, 128);
        {
            ScopedAllocationCounter counter;
            p.processBlock (view, midi);
            allocations += counter.count();
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 128; ++i)
                finite = finite && std::isfinite (view.getSample (ch, i));
    }
    LQ_CHECK (allocations == 0, str (allocations, " allocations in processBlock"));
    LQ_CHECK (finite, "finite output");
}

LQ_TEST ("e2c perf: processor with eight dynamic bands off and on (stereo, Release) (14)")
{
#ifndef NDEBUG
    ctx.skip ("Debug build: timings are not meaningful; run the Release test binary");
    return;
#else
    constexpr double fs = 96000.0;
    constexpr int blockSize = 64, seconds = 4;
    const auto input = program (2, (int) fs * seconds, fs, 81);
    auto measure = [&] (bool dynOn)
    {
        std::vector<double> loads;
        for (int r = 0; r < 6; ++r)
        {
            LabQ8AudioProcessor p;
            setLayout (p, 2);
            setEightDynamicBells (p);
            for (int b = 0; b < params::numBands; ++b)
                setParam (p, params::dynamicIDs[(size_t) b].enabled, dynOn ? 1.0f : 0.0f);
            p.prepareToPlay (fs, blockSize);
            juce::MidiBuffer midi;
            auto buffer = input;
            const auto start = std::chrono::steady_clock::now();
            for (int pos = 0; pos < buffer.getNumSamples(); pos += blockSize)
            {
                juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, pos, blockSize);
                p.processBlock (view, midi);
            }
            const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            if (r > 0)
                loads.push_back (elapsed / seconds);
        }
        std::sort (loads.begin(), loads.end());
        return loads[loads.size() / 2];
    };
    const auto off = measure (false), on = measure (true);
    ctx.note (str ("processor, 96 kHz stereo, block 64, % of one core: 8 bands dynamics off ", off * 100.0, ", 8 dynamic bands on ", on * 100.0));
    LQ_CHECK (on < 0.05, str (on * 100.0, " % of one core"));
#endif
}
