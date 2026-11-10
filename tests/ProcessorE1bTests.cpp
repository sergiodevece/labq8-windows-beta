// E1b host adapter, state and real-time tests: type parameters, compatibility
// with genuine E0 / E1a states (N), processor audio path with mixed types,
// no allocation with type changes (M) and the crossfade CPU budget.

#pragma clang diagnostic ignored "-Wfloat-equal" // bit-exactness is a tested property

#include "AllocationGuard.h"
#include "FilterReference.h"
#include "TestHarness.h"
#include "TestSignals.h"

#include "dsp/BellBand.h"
#include "dsp/EqCore.h"
#include "parameters/ParameterIDs.h"
#include "plugin/PluginProcessor.h"

#include <chrono>
#include <set>

using labq8::LabQ8AudioProcessor;
using labq8::test::ScopedAllocationCounter;
using labq8::test::str;
using namespace labq8::test::signals;
using labq8::dsp::FilterType;
namespace params = labq8::params;

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

juce::MemoryBlock loadFixture (const char* name)
{
    juce::MemoryBlock block;
    juce::File (LABQ8_TEST_FIXTURES_DIR).getChildFile (name).loadFileAsData (block);
    return block;
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

void setLayout (LabQ8AudioProcessor& p, int channels)
{
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    p.setBusesLayout (layout);
}

// What the DSP receives for a band (mirrors the adapter).
labq8::dsp::BandSettings expectedSettings (LabQ8AudioProcessor& p, int band)
{
    const auto& ids = params::bandIDs[(size_t) band];
    double gain = getParam (p, ids.gain);
    if (std::abs (gain) < 0.005)
        gain = 0.0;
    return { getParam (p, ids.frequency), gain, getParam (p, ids.q), getParam (p, ids.bypass) >= 0.5f,
             labq8::dsp::filterTypeFromIndex ((int) std::lround (getParam (p, ids.type))) };
}

bool nearlyEqual (float a, float b, float scale = 0.0f)
{
    return std::abs (a - b) <= 5.0e-7f * std::max ({ 1.0f, std::abs (b), scale });
}

void setMixedTypes (LabQ8AudioProcessor& p)
{
    const float freqs[] = { 40.0f, 120.0f, 400.0f, 1000.0f, 2500.0f, 6000.0f, 10000.0f, 16000.0f };
    const int types[] = { 3, 1, 0, 5, 0, 2, 0, 4 };
    for (int b = 0; b < params::numBands; ++b)
    {
        const auto& ids = params::bandIDs[(size_t) b];
        setParam (p, ids.frequency, freqs[b]);
        setParam (p, ids.gain, b % 2 ? -4.0f : 5.0f);
        setParam (p, ids.q, 0.7071f + 0.3f * (float) b);
        setParam (p, ids.type, (float) types[b]);
    }
}

juce::AudioBuffer<float> noiseBuffer (int channels, int numSamples, unsigned seed)
{
    juce::AudioBuffer<float> b (channels, numSamples);
    for (int ch = 0; ch < channels; ++ch)
    {
        const auto n = makeNoise ((size_t) numSamples, seed * 7 + (unsigned) ch);
        b.copyFrom (ch, 0, n.data(), numSamples);
    }
    return b;
}
} // namespace

//==============================================================================
LQ_TEST ("e1b processor: b1..b8_type appended after the 32 E1a parameters, hint 3, frozen indices")
{
    LabQ8AudioProcessor p;
    const auto& all = p.getParameters();
    // 80 until E2c.1; E4a appends 161 after them (indices 0..79 unchanged).
    LQ_CHECK (all.size() == params::numParameters && params::numParameters == 241, str ("parameter count ", all.size()));

    // Host indices 0..31: exactly the E1a order and hints.
    int index = 0;
    for (int b = 0; b < params::numBands; ++b)
    {
        const auto& ids = params::bandIDs[(size_t) b];
        for (const auto* id : { ids.frequency, ids.gain, ids.q, ids.bypass })
        {
            const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*> (all[index]);
            LQ_CHECK (ranged != nullptr && ranged->paramID == id, str ("host index ", index, " is not ", id));
            LQ_CHECK (ranged != nullptr && ranged->getVersionHint() == (b == 0 ? params::versionHintE0 : params::versionHintE1a),
                      str (id, " version hint changed"));
            ++index;
        }
    }

    // Host indices 32..39: the types.
    for (int b = 0; b < params::numBands; ++b, ++index)
    {
        const auto* choice = dynamic_cast<const juce::AudioParameterChoice*> (all[index]);
        LQ_CHECK (choice != nullptr && choice->paramID == params::bandIDs[(size_t) b].type, str ("host index ", index, " is not b", b + 1, "_type"));
        if (choice == nullptr)
            continue;
        LQ_CHECK (choice->getVersionHint() == params::versionHintE1b, "type version hint");
        LQ_CHECK (choice->getIndex() == 0 && choice->choices.size() == 6, "type default must be Bell, 6 choices");
        LQ_CHECK (choice->isAutomatable(), "type must be automatable");
        const char* expected[] = { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" };
        for (int i = 0; i < 6; ++i)
            LQ_CHECK (choice->choices[i] == expected[i], str ("choice ", i, " is ", choice->choices[i].toStdString()));
    }

    // Raw value of a choice is its index; FilterType uses the same numbers.
    for (int i = 0; i < 6; ++i)
    {
        setParam (p, "b4_type", (float) i);
        LQ_CHECK (getParam (p, "b4_type") == (float) i, str ("raw value of choice ", i));
        LQ_CHECK ((int) labq8::dsp::filterTypeFromIndex (i) == i, "FilterType index");
    }
}

LQ_TEST ("e1b processor: audio path equals the EQ core with mixed types (mono and stereo, 44.1/48/96 kHz)")
{
    for (const auto channels : { 1, 2 })
        for (const auto fs : sampleRates)
        {
            LabQ8AudioProcessor p;
            setLayout (p, channels);
            setMixedTypes (p);
            p.prepareToPlay (fs, 480);

            labq8::dsp::EqCore reference;
            for (int b = 0; b < params::numBands; ++b)
                reference.setBandTargets (b, expectedSettings (p, b));
            reference.prepare (fs, channels);

            juce::MidiBuffer midi;
            bool identical = true;
            for (int block = 0; block < 200; ++block)
            {
                if (block == 50) // type and bypass automation mid-stream
                {
                    setParam (p, "b3_type", 4.0f);
                    setParam (p, "b4_bypass", 1.0f);
                    setParam (p, "b8_type", 1.0f);
                    for (int b = 0; b < params::numBands; ++b)
                        reference.setBandTargets (b, expectedSettings (p, b));
                }
                auto buffer = noiseBuffer (channels, 480, (unsigned) block);
                auto expected = buffer;
                p.processBlock (buffer, midi);
                reference.process (expected.getArrayOfWritePointers(), channels, 480);
                for (int ch = 0; ch < channels; ++ch)
                    identical = identical && std::equal (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + 480, expected.getReadPointer (ch));
            }
            LQ_CHECK (identical, str ("processor != EqCore, channels=", channels, " fs=", fs));
        }
}

LQ_TEST ("e1b processor: type automation is block-size independent and gain survives type changes")
{
    constexpr double fs = 48000.0;
    constexpr int total = 96000, changeAt = 48000; // 48000 divisible by every size below

    auto run = [&] (int blockSize)
    {
        LabQ8AudioProcessor p;
        setLayout (p, 2);
        setMixedTypes (p);
        p.prepareToPlay (fs, blockSize);
        juce::MidiBuffer midi;
        const auto input = noiseBuffer (2, total, 9);
        juce::AudioBuffer<float> out (2, total);
        for (int pos = 0; pos < total; pos += blockSize)
        {
            if (pos == changeAt)
                for (int b = 0; b < params::numBands; ++b)
                    setParam (p, params::bandIDs[(size_t) b].type, (float) ((b + 2) % 6));
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
    for (const int size : { 1, 64, 128, 4000 })
    {
        const auto y = run (size);
        bool same = true;
        for (int ch = 0; ch < 2; ++ch)
            same = same && std::equal (y.getReadPointer (ch), y.getReadPointer (ch) + total, reference.getReadPointer (ch));
        LQ_CHECK (same, str ("host block size ", size, " differs"));
    }

    // Gain is kept (and saved) while a cut is selected, and comes back with Bell.
    LabQ8AudioProcessor p;
    setParam (p, "b2_gain", 7.0f);
    setParam (p, "b2_type", 3.0f); // Low Cut ignores gain
    const auto state = save (p);
    LabQ8AudioProcessor q;
    load (q, state);
    LQ_CHECK (std::abs (getParam (q, "b2_gain") - 7.0f) < 1e-5f && getParam (q, "b2_type") == 3.0f, "gain lost while a cut is selected");
    setParam (q, "b2_type", 0.0f);
    LQ_CHECK (std::abs (getParam (q, "b2_gain") - 7.0f) < 1e-5f, "gain lost when returning to Bell");
}

//==============================================================================
LQ_TEST ("state: genuine E1a states load as eight Bells with their values, deterministically (N)")
{
    for (const auto* file : { "e1a_default.bin", "e1a_custom.bin", "e1a_from_e0_custom.bin" })
    {
        const auto e1a = loadFixture (file);
        LQ_CHECK (e1a.getSize() > 0, str ("fixture missing: ", file));
        const auto xml = juce::AudioProcessor::getXmlFromBinary (e1a.getData(), (int) e1a.getSize());
        LQ_CHECK (xml != nullptr && xml->getIntAttribute (params::stateVersionProperty) == 2 && xml->getNumChildElements() == 32,
                  str (file, " is not a 32-parameter v2 state"));
        if (xml == nullptr)
            continue;

        LabQ8AudioProcessor fresh;
        load (fresh, e1a);

        LabQ8AudioProcessor used; // history: every parameter moved, types included
        for (int b = 0; b < params::numBands; ++b)
        {
            const auto& ids = params::bandIDs[(size_t) b];
            setParam (used, ids.type, (float) (b % 6));
            setParam (used, ids.gain, -11.0f);
            setParam (used, ids.bypass, 1.0f);
        }
        load (used, e1a);

        for (auto* p : { &fresh, &used })
            for (int b = 0; b < params::numBands; ++b)
            {
                const auto& ids = params::bandIDs[(size_t) b];
                LQ_CHECK (getParam (*p, ids.type) == 0.0f, str (file, ": ", ids.type, " is not Bell"));
                for (const auto* id : { ids.frequency, ids.gain, ids.q, ids.bypass })
                {
                    const auto stored = (float) xml->getChildByAttribute ("id", id)->getDoubleAttribute ("value");
                    LQ_CHECK (nearlyEqual (getParam (*p, id), stored, 24.0f), str (file, ": ", id, " = ", getParam (*p, id), " stored ", stored));
                }
            }

        const auto a = save (fresh), b = save (used);
        LQ_CHECK (a == b, str (file, ": migrated state depends on instance history"));
        const auto migrated = juce::AudioProcessor::getXmlFromBinary (a.getData(), (int) a.getSize());
        LQ_CHECK (migrated != nullptr && migrated->getIntAttribute (params::stateVersionProperty) == params::currentStateVersion
                  && migrated->getNumChildElements() == params::numParameters,
                  "re-saved state is the current version with every parameter");
        bool allValues = migrated != nullptr;
        if (migrated != nullptr)
            for (auto* child : migrated->getChildIterator())
                allValues = allValues && child->hasAttribute ("value");
        LQ_CHECK (allValues, "re-saved state has PARAMs without a value");
    }

    // E0 states also arrive with every type = Bell.
    for (const auto* file : { "e0_default.bin", "e0_custom.bin", "e0_bypassed.bin" })
    {
        LabQ8AudioProcessor p;
        for (int b = 0; b < params::numBands; ++b)
            setParam (p, params::bandIDs[(size_t) b].type, 5.0f);
        load (p, loadFixture (file));
        for (int b = 0; b < params::numBands; ++b)
            LQ_CHECK (getParam (p, params::bandIDs[(size_t) b].type) == 0.0f, str (file, ": band ", b + 1, " not Bell"));
    }
}

LQ_TEST ("state: E1a sessions sound exactly like E1a (eight frozen BellBands) (N)")
{
    for (const auto* file : { "e1a_custom.bin", "e1a_from_e0_custom.bin", "e1a_default.bin" })
        for (const auto channels : { 1, 2 })
            for (const auto fs : sampleRates)
            {
                LabQ8AudioProcessor p;
                setLayout (p, channels);
                load (p, loadFixture (file));
                p.prepareToPlay (fs, 512);

                // E1a DSP: eight BellBands in series fed with the restored values
                // (b1..b8 definitions are unchanged since E1a).
                std::array<labq8::dsp::BellBand, 8> e1a;
                for (int b = 0; b < params::numBands; ++b)
                {
                    const auto s = expectedSettings (p, b);
                    e1a[(size_t) b].setTargets ({ s.frequencyHz, s.gainDb, s.q, s.bypassed });
                    e1a[(size_t) b].prepare (fs, channels);
                }

                juce::MidiBuffer midi;
                bool identical = true;
                for (int block = 0; block < 100; ++block)
                {
                    auto buffer = noiseBuffer (channels, 512, (unsigned) block);
                    auto expected = buffer;
                    p.processBlock (buffer, midi);
                    for (auto& band : e1a)
                        band.process (expected.getArrayOfWritePointers(), channels, 512);
                    for (int ch = 0; ch < channels; ++ch)
                        identical = identical && std::equal (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + 512, expected.getReadPointer (ch));
                }
                LQ_CHECK (identical, str (file, ": not bit-identical to E1a, channels=", channels, " fs=", fs));
            }
}

LQ_TEST ("state: v3 round-trip of all 40 parameters; out-of-range type values are sanitised")
{
    LabQ8AudioProcessor a;
    setMixedTypes (a);
    setParam (a, "b6_bypass", 1.0f);
    const auto block = save (a);

    LabQ8AudioProcessor b;
    load (b, block);
    for (const auto* p : b.getParameters())
    {
        const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*> (p);
        LQ_CHECK (ranged != nullptr && getParam (a, ranged->paramID.toRawUTF8()) == getParam (b, ranged->paramID.toRawUTF8()),
                  str ("round-trip changed ", ranged->paramID.toStdString()));
    }
    LQ_CHECK (save (b) == block, "save after load differs");

    // Hand-edited / future type values.
    for (const double bad : { 9.0, -1.0, 2.6, 1.0e9 })
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
        xml->getChildByAttribute ("id", "b2_type")->setAttribute ("value", bad);
        juce::MemoryBlock edited;
        juce::AudioProcessor::copyXmlToBinary (*xml, edited);
        LabQ8AudioProcessor c;
        load (c, edited);
        const auto t = getParam (c, "b2_type");
        LQ_CHECK (t >= 0.0f && t <= 5.0f && t == std::round (t), str ("type value ", bad, " restored as ", t));
        c.prepareToPlay (48000.0, 256);
        juce::MidiBuffer midi;
        auto buffer = noiseBuffer (2, 256, 1);
        c.processBlock (buffer, midi);
        LQ_CHECK (std::isfinite (buffer.getSample (0, 255)), "finite output after a sanitised type");
    }
}

//==============================================================================
LQ_TEST ("rt: no heap allocation with type changes and crossfades (EQ core and processor) (M)")
{
    for (const auto fs : sampleRates)
    {
        labq8::dsp::EqCore core;
        core.prepare (fs, 2);
        auto l = makeNoise (48000, 700), r = makeNoise (48000, 701);
        std::mt19937 rng (702);
        std::uniform_int_distribution<int> type (0, 5), gap (1, 700);

        ScopedAllocationCounter counter;
        for (size_t pos = 0; pos < l.size();)
        {
            for (int b = 0; b < 8; ++b)
                core.setBandTargets (b, labq8::dsp::BandSettings { 100.0 * (b + 1), 6.0, 1.0, (pos / 7) % 5 == 0,
                                                                   labq8::dsp::filterTypeFromIndex (type (rng)) });
            const auto n = std::min<size_t> ((size_t) gap (rng), l.size() - pos);
            float* ch[] = { l.data() + pos, r.data() + pos };
            core.process (ch, 2, (int) n);
            pos += n;
        }
        LQ_CHECK (counter.count() == 0, str ("EqCore allocated ", counter.count(), " times with type changes, fs=", fs));
    }

    LabQ8AudioProcessor p;
    p.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    long allocations = 0;
    for (int block = 0; block < 400; ++block)
    {
        for (int b = 0; b < params::numBands; ++b)
            setParam (p, params::bandIDs[(size_t) b].type, (float) ((block + b) % 6));
        const auto noise = makeNoise (512, (std::uint32_t) block);
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, 0, noise.data(), 512);
        ScopedAllocationCounter counter;
        p.processBlock (buffer, midi);
        allocations += counter.count();
    }
    LQ_CHECK (allocations == 0, str ("processBlock allocated ", allocations, " times with type automation"));
}

LQ_TEST ("perf: worst case, all 8 bands crossfading continuously (stereo, Release)")
{
#ifndef NDEBUG
    ctx.skip ("Debug build: timings are not meaningful; run the Release test binary");
    return;
#else
    // Same absolute budget as the E1a test (5% of one core), justified by the
    // measured worst case reported in docs/ARCHITECTURE.md; trips only on a
    // real regression.
    constexpr double budget = 0.05;
    const auto fs = 96000.0;
    const auto seconds = 10.0;
    const auto n = samples (seconds, fs);
    const auto left = makeNoise (n, 710), right = makeNoise (n, 711);
    std::vector<double> loads;

    for (int run = 0; run < 6; ++run)
    {
        labq8::dsp::EqCore core;
        core.prepare (fs, 2);
        auto l = left, r = right;
        const auto fade = (size_t) std::lround (0.02 * fs);
        int flip = 0;
        const auto start = std::chrono::steady_clock::now();
        for (size_t pos = 0; pos < n; pos += 64)
        {
            if (pos % fade < 64) // retrigger every fade length: every band always fading
            {
                ++flip;
                for (int b = 0; b < 8; ++b)
                    core.setBandTargets (b, labq8::dsp::BandSettings { 200.0 * (b + 1), 6.0, 1.0, false,
                                                                       labq8::dsp::filterTypeFromIndex ((b + flip) % 6) });
            }
            float* ch[] = { l.data() + pos, r.data() + pos };
            core.process (ch, 2, (int) std::min<size_t> (64, n - pos));
        }
        const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        if (run > 0)
            loads.push_back (elapsed / seconds);
    }

    std::sort (loads.begin(), loads.end());
    const auto load = loads[loads.size() / 2];
    ctx.note (str ("8 bands always crossfading, 96 kHz stereo, block 64: ", load * 100.0, "% of one core (", load * 1.0e9 / fs, " ns per stereo frame)"));
    LQ_CHECK (load < budget, str ("CPU load ", load * 100.0, "% over budget"));
#endif
}
