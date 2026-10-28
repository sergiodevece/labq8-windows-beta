// State migration E0 (v1) -> E1a (v2), using genuine E0 states written by the
// dfcf25b sources (tests/fixtures, see tests/fixtures/README.md).

#pragma clang diagnostic ignored "-Wfloat-equal" // bit-exactness is a tested property

#include "TestHarness.h"
#include "TestSignals.h"

#include "dsp/BellBand.h"
#include "parameters/ParameterIDs.h"
#include "plugin/PluginProcessor.h"

using labq8::LabQ8AudioProcessor;
using labq8::test::str;
namespace params = labq8::params;

namespace
{
juce::MemoryBlock loadFixture (const char* name)
{
    juce::MemoryBlock block;
    juce::File (LABQ8_TEST_FIXTURES_DIR).getChildFile (name).loadFileAsData (block);
    return block;
}

void setParam (LabQ8AudioProcessor& p, const char* id, float value)
{
    auto* parameter = p.getValueTreeState().getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

float getParam (LabQ8AudioProcessor& p, const char* id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}

float defaultOf (LabQ8AudioProcessor& p, const char* id)
{
    auto* parameter = p.getValueTreeState().getParameter (id);
    return parameter->convertFrom0to1 (parameter->getDefaultValue());
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

// Without migration JUCE 9.0.3 re-saves absent parameters as <PARAM id="..."/>
// with no value in some instances, which makes the saved state depend on the
// instance's history. Every PARAM must carry an explicit value.
bool allParamsHaveValues (const juce::MemoryBlock& block)
{
    // Unknown PARAMs (e.g. from a newer version) are kept untouched, so only
    // the 32 known IDs are required here.
    const auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    if (xml == nullptr)
        return false;

    for (const auto& ids : params::bandIDs)
        for (const auto* id : { ids.frequency, ids.gain, ids.q, ids.bypass })
        {
            const auto* child = xml->getChildByAttribute ("id", id);
            if (child == nullptr || ! child->hasAttribute ("value"))
                return false;
        }

    return true;
}

// Scrambles every parameter of bands 2..8 away from its default.
void scrambleBands2to8 (LabQ8AudioProcessor& p)
{
    for (int b = 1; b < params::numBands; ++b)
    {
        const auto& ids = params::bandIDs[(size_t) b];
        setParam (p, ids.frequency, 777.0f + 100.0f * (float) b);
        setParam (p, ids.gain, -11.0f);
        setParam (p, ids.q, 7.0f);
        setParam (p, ids.bypass, 1.0f);
    }
}

struct E0Fixture
{
    const char* file;
    float freq, gain, q;
    bool bypass;
};

// A few float ulp of `scale`: absorbs the arm64/x86_64 FMA difference and
// nothing more. Gain is snapped as -24 + k * 0.01, so its rounding error
// scales with ulp(24) ~ 1.9e-6 even when the value is near 0.
bool nearlyEqual (float a, float b, float scale = 0.0f)
{
    return std::abs (a - b) <= 5.0e-7f * std::max ({ 1.0f, std::abs (b), scale });
}

// Values exactly as stored in the fixtures (written on arm64).
const E0Fixture kFixtures[] = {
    { "e0_default.bin", 1000.0f, -5.364418029785156e-7f, 1.0f, false },
    { "e0_custom.bin", 3149.99951171875f, -7.250000476837158f, 4.500000476837158f, false },
    { "e0_bypassed.bin", 119.9999923706055f, 8.999999046325684f, 0.699999988079071f, true },
};
} // namespace

LQ_TEST ("state: genuine E0 states migrate with deterministic defaults for bands 2..8")
{
    for (const auto& fx : kFixtures)
    {
        const auto e0 = loadFixture (fx.file);
        LQ_CHECK (e0.getSize() > 0, str ("fixture missing: ", fx.file));

        const auto xml = juce::AudioProcessor::getXmlFromBinary (e0.getData(), (int) e0.getSize());
        LQ_CHECK (xml != nullptr && xml->getIntAttribute (params::stateVersionProperty) == 1, "fixture is a v1 state");
        LQ_CHECK (xml != nullptr && xml->getNumChildElements() == 4, "fixture has only band 1");

        LabQ8AudioProcessor fresh;
        load (fresh, e0);

        LabQ8AudioProcessor used; // instance whose bands 2..8 were changed before loading
        scrambleBands2to8 (used);
        setParam (used, params::id::band1Gain, 20.0f);
        load (used, e0);

        for (auto* p : { &fresh, &used })
        {
            // Not bit-exact across architectures: JUCE's 0.01 dB snapping is
            // fused (FMA) on arm64 and not on x86_64, so restored values may
            // differ from the stored ones by about one float ulp.
            LQ_CHECK (nearlyEqual (getParam (*p, params::id::band1Frequency), fx.freq), str (fx.file, ": b1_freq ", getParam (*p, params::id::band1Frequency)));
            LQ_CHECK (nearlyEqual (getParam (*p, params::id::band1Gain), fx.gain, 24.0f), str (fx.file, ": b1_gain ", getParam (*p, params::id::band1Gain)));
            LQ_CHECK (nearlyEqual (getParam (*p, params::id::band1Q), fx.q), str (fx.file, ": b1_q"));
            LQ_CHECK ((getParam (*p, params::id::band1Bypass) >= 0.5f) == fx.bypass, str (fx.file, ": b1_bypass"));

            for (int b = 1; b < params::numBands; ++b)
            {
                const auto& ids = params::bandIDs[(size_t) b];
                for (const auto* id : { ids.frequency, ids.gain, ids.q, ids.bypass })
                    LQ_CHECK (getParam (*p, id) == defaultOf (*p, id), str (fx.file, ": ", id, " = ", getParam (*p, id), " not default"));
            }
        }

        // Same E0 input -> same E1a state, whatever the instance's history.
        const auto a = save (fresh), b = save (used);
        LQ_CHECK (a == b, str (fx.file, ": migrated state depends on instance history"));

        const auto migrated = juce::AudioProcessor::getXmlFromBinary (a.getData(), (int) a.getSize());
        LQ_CHECK (migrated != nullptr && migrated->getIntAttribute (params::stateVersionProperty) == params::currentStateVersion,
                  "re-saved state is v2");
        LQ_CHECK (migrated != nullptr && migrated->getNumChildElements() == 32, "re-saved state has 32 parameters");
        LQ_CHECK (allParamsHaveValues (a) && allParamsHaveValues (b), str (fx.file, ": re-saved state has PARAMs without a value"));
    }
}

LQ_TEST ("state: migrated E0 sessions sound like E0")
{
    for (const auto& fx : kFixtures)
        for (const auto channels : { 1, 2 })
            for (const auto fs : labq8::test::signals::sampleRates)
            {
                LabQ8AudioProcessor p;
                const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
                juce::AudioProcessor::BusesLayout layout;
                layout.inputBuses.add (set);
                layout.outputBuses.add (set);
                p.setBusesLayout (layout);
                load (p, loadFixture (fx.file));
                p.prepareToPlay (fs, 512);

                // E0 DSP path: one BellBand fed with the b1 values as restored on
                // this architecture. The b1 parameter definition is unchanged
                // from E0 (checked by the layout tests), so an E0 instance
                // restoring this state would see exactly these values.
                labq8::dsp::BellBand e0;
                e0.setTargets ({ getParam (p, params::id::band1Frequency), getParam (p, params::id::band1Gain),
                                 getParam (p, params::id::band1Q), getParam (p, params::id::band1Bypass) >= 0.5f });
                e0.prepare (fs, channels);

                juce::MidiBuffer midi;
                bool identical = true, identity = true;
                float maxE0Deviation = 0.0f;

                for (int block = 0; block < 100; ++block)
                {
                    juce::AudioBuffer<float> buffer (channels, 512);
                    for (int ch = 0; ch < channels; ++ch)
                    {
                        const auto n = labq8::test::signals::makeNoise (512, (std::uint32_t) (block * 2 + ch));
                        buffer.copyFrom (ch, 0, n.data(), 512);
                    }
                    const auto input = buffer;
                    auto viaE0 = buffer;
                    p.processBlock (buffer, midi);
                    e0.process (viaE0.getArrayOfWritePointers(), channels, 512);

                    for (int ch = 0; ch < channels; ++ch)
                        for (int i = 0; i < 512; ++i)
                        {
                            identical = identical && buffer.getSample (ch, i) == viaE0.getSample (ch, i);
                            identity = identity && buffer.getSample (ch, i) == input.getSample (ch, i);
                            maxE0Deviation = std::max (maxE0Deviation, std::abs (viaE0.getSample (ch, i) - input.getSample (ch, i)));
                        }
                }

                if (std::abs (fx.gain) >= 0.005f)
                {
                    LQ_CHECK (identical, str (fx.file, ": not bit-identical to E0, channels=", channels, " fs=", fs));
                }
                else
                {
                    // e0_default: E0 ran the band at -5.4e-7 dB; E1a treats it as 0 dB (idle).
                    LQ_CHECK (identity, str (fx.file, ": E1a not bit-exact transparent"));
                    LQ_CHECK (maxE0Deviation < 1.0e-6f, str (fx.file, ": E0 deviated ", maxE0Deviation, " from identity"));
                    if (channels == 2 && fs == 48000.0)
                        ctx.note (str ("e0_default: E0 output deviated up to ", maxE0Deviation, " from input; E1a output is the input"));
                }
            }
}

LQ_TEST ("state: v2 round-trip restores all 32 parameters exactly")
{
    LabQ8AudioProcessor a;
    for (int b = 0; b < params::numBands; ++b)
    {
        const auto& ids = params::bandIDs[(size_t) b];
        setParam (a, ids.frequency, 31.0f * (float) (b + 1) * (float) (b + 1));
        setParam (a, ids.gain, -20.0f + 5.5f * (float) b);
        setParam (a, ids.q, 0.15f + 2.0f * (float) b);
        setParam (a, ids.bypass, (b % 3 == 0) ? 1.0f : 0.0f);
    }

    const auto block = save (a);
    LabQ8AudioProcessor b;
    scrambleBands2to8 (b);
    load (b, block);

    for (int band = 0; band < params::numBands; ++band)
    {
        const auto& ids = params::bandIDs[(size_t) band];
        for (const auto* id : { ids.frequency, ids.gain, ids.q, ids.bypass })
            LQ_CHECK (getParam (a, id) == getParam (b, id), str (id, " ", getParam (b, id), " != ", getParam (a, id)));
    }

    LQ_CHECK (save (b) == block, "save after load differs");
}

LQ_TEST ("state: partial, unversioned and unknown content is handled deterministically")
{
    // v2 state missing some parameters: missing ones become defaults.
    LabQ8AudioProcessor source;
    setParam (source, "b3_gain", 4.0f);
    setParam (source, "b6_freq", 6000.0f);
    auto block = save (source);
    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    xml->removeChildElement (xml->getChildByAttribute ("id", "b6_freq"), true);
    xml->removeChildElement (xml->getChildByAttribute ("id", "b2_q"), true);

    // Unknown parameter from some future version: ignored.
    auto* unknown = xml->createNewChildElement ("PARAM");
    unknown->setAttribute ("id", "b9_freq");
    unknown->setAttribute ("value", 123.0);

    juce::MemoryBlock edited;
    juce::AudioProcessor::copyXmlToBinary (*xml, edited);

    LabQ8AudioProcessor target;
    setParam (target, "b6_freq", 42.0f);
    setParam (target, "b2_q", 9.0f);
    load (target, edited);

    LQ_CHECK (getParam (target, "b3_gain") == getParam (source, "b3_gain"), "present parameter restored");
    LQ_CHECK (getParam (target, "b6_freq") == defaultOf (target, "b6_freq"), "missing b6_freq not reset to default");
    LQ_CHECK (getParam (target, "b2_q") == defaultOf (target, "b2_q"), "missing b2_q not reset to default");
    LQ_CHECK (allParamsHaveValues (save (target)), "re-saved partial state has PARAMs without a value");
    LQ_CHECK (target.getValueTreeState().getParameter ("b9_freq") == nullptr, "unknown ID must not create a parameter");

    // No version property at all (hand-made or pre-E0 tooling): migrated like v1.
    juce::XmlElement bare (LabQ8AudioProcessor::stateType);
    auto* p = bare.createNewChildElement ("PARAM");
    p->setAttribute ("id", "b1_gain");
    p->setAttribute ("value", 3.0);
    juce::MemoryBlock bareBlock;
    juce::AudioProcessor::copyXmlToBinary (bare, bareBlock);

    LabQ8AudioProcessor fromBare;
    scrambleBands2to8 (fromBare);
    load (fromBare, bareBlock);
    // 3.0 goes through JUCE's 0.01 dB float grid (-24 + 2700 * 0.01f), so a few ulp off.
    LQ_CHECK (std::abs (getParam (fromBare, "b1_gain") - 3.0f) < 1.0e-5f, str ("bare state value ", getParam (fromBare, "b1_gain")));
    LQ_CHECK (getParam (fromBare, "b1_freq") == defaultOf (fromBare, "b1_freq"), "bare state: b1_freq default");
    LQ_CHECK (getParam (fromBare, "b4_bypass") == defaultOf (fromBare, "b4_bypass"), "bare state: b4_bypass default");
}
