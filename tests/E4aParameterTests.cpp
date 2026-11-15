// E4a: the 241-parameter contract (80 STEREO, proc_mode, 80 MID, 80 SIDE),
// state v5 and its migration: the parameters, the saved state and the fact
// that the STEREO sound does not depend on the MID / SIDE banks. See
// docs/MIDSIDE_E4A.md. The M/S processing itself (E4b) is tested in
// tests/MidSideTests.cpp; the E4a-only test "proc_mode is stored but not
// processed" was retired when E4b implemented it.
//
// Fixtures (tests/fixtures/README.md): contract_v4_ui2c.txt and the genuine
// v4 states ui2c_*.bin were written by the 152d46c sources;
// e4a_custom_v5.bin by these sources (LABQ8_WRITE_FIXTURES, below), and
// ui2c_resaved_from_v5.bin is that v5 state opened and saved again by 152d46c.

#pragma clang diagnostic ignored "-Wfloat-equal" // bit-exactness is a tested property

#include "ParameterContract.h"
#include "TestHarness.h"
#include "TestSignals.h"

#include "parameters/ParameterIDs.h"
#include "plugin/PluginProcessor.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <set>

using labq8::LabQ8AudioProcessor;
using labq8::test::str;
namespace params = labq8::params;
namespace contract = labq8::contract;

namespace
{
juce::File fixture (const char* name)
{
    return juce::File (LABQ8_TEST_FIXTURES_DIR).getChildFile (name);
}

juce::MemoryBlock loadFixture (const char* name)
{
    juce::MemoryBlock block;
    fixture (name).loadFileAsData (block);
    return block;
}

std::vector<std::string> readLines (const juce::File& f)
{
    std::vector<std::string> lines;
    std::ifstream in (f.getFullPathName().toStdString());
    for (std::string line; std::getline (in, line);)
        lines.push_back (line);
    return lines;
}

juce::RangedAudioParameter& param (LabQ8AudioProcessor& p, const char* id)
{
    return *p.getValueTreeState().getParameter (id);
}

void setParam (LabQ8AudioProcessor& p, const char* id, float value)
{
    auto& parameter = param (p, id);
    parameter.setValueNotifyingHost (parameter.convertTo0to1 (value));
}

float getParam (LabQ8AudioProcessor& p, const char* id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}

float defaultOf (LabQ8AudioProcessor& p, const char* id)
{
    auto& parameter = param (p, id);
    return parameter.convertFrom0to1 (parameter.getDefaultValue());
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

std::unique_ptr<juce::XmlElement> xmlOf (const juce::MemoryBlock& block)
{
    return juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
}

std::vector<juce::String> allIds (LabQ8AudioProcessor& p)
{
    std::vector<juce::String> ids;
    for (auto* parameter : p.getParameters())
        ids.push_back (dynamic_cast<juce::RangedAudioParameter*> (parameter)->paramID);
    return ids;
}

std::vector<float> allValues (LabQ8AudioProcessor& p)
{
    std::vector<float> v;
    for (auto* parameter : p.getParameters())
        v.push_back (parameter->getValue());
    return v;
}

void randomiseAll (LabQ8AudioProcessor& p, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    for (auto* parameter : p.getParameters())
        parameter->setValueNotifyingHost (u (rng));
}

// Every ID of one bank, band by band, in the host order of an E4a bank.
std::vector<const char*> bankIdsInOrder (params::Bank bank)
{
    const auto ids = params::bankOf (bank);
    std::vector<const char*> v;
    for (int b = 0; b < params::numBands; ++b)
    {
        const auto& s = ids.bands[(size_t) b];
        const auto& d = ids.dynamics[(size_t) b];
        for (const auto* id : { s.frequency, s.gain, s.q, s.bypass, s.type, d.enabled, d.threshold, d.range, d.attack, d.release })
            v.push_back (id);
    }
    return v;
}

std::vector<const char*> newIds()
{
    auto v = bankIdsInOrder (params::Bank::mid);
    const auto side = bankIdsInOrder (params::Bank::side);
    v.insert (v.begin(), params::procModeID);
    v.insert (v.end(), side.begin(), side.end());
    return v;
}

// A distinctive v5 session: STEREO as ui2c_custom, every MID and SIDE
// parameter away from its default, proc_mode = Mid/Side.
void makeE4aCustom (LabQ8AudioProcessor& p)
{
    load (p, loadFixture ("ui2c_custom.bin"));
    setParam (p, params::procModeID, (float) params::ProcMode::midSide);
    for (const auto bank : { params::Bank::mid, params::Bank::side })
    {
        const auto ids = params::bankOf (bank);
        const auto k = bank == params::Bank::mid ? 1.0f : -1.0f;
        for (int b = 0; b < params::numBands; ++b)
        {
            const auto& s = ids.bands[(size_t) b];
            const auto& d = ids.dynamics[(size_t) b];
            setParam (p, s.frequency, 35.0f * (float) (b + 2) * (float) (b + 2) * (bank == params::Bank::mid ? 1.0f : 1.5f));
            setParam (p, s.gain, k * (2.0f + 2.5f * (float) b));
            setParam (p, s.q, 0.3f + 1.7f * (float) b);
            setParam (p, s.bypass, (b % 4 == (bank == params::Bank::mid ? 1 : 2)) ? 1.0f : 0.0f);
            setParam (p, s.type, (float) ((b + (bank == params::Bank::mid ? 0 : 3)) % 6));
            setParam (p, d.enabled, (b % 2 == 0) ? 1.0f : 0.0f);
            setParam (p, d.threshold, -60.0f + 9.0f * (float) b + (bank == params::Bank::mid ? 0.0f : -5.0f));
            setParam (p, d.range, 1.0f + 2.5f * (float) b);
            setParam (p, d.attack, 0.5f * std::pow (2.5f, (float) b));
            setParam (p, d.release, 5.0f * std::pow (2.6f, (float) b));
        }
    }
}

// ±5e-7 relative, at least on the scale of 24: the arm64 / x86_64 FMA
// difference of JUCE's grids (as in the E1a / E2c.1 migration tests).
bool nearlyStored (float got, float stored)
{
    return std::abs (got - stored) <= 5.0e-7f * std::max (24.0f, std::abs (stored));
}

bool setLayout (LabQ8AudioProcessor& p, int channels)
{
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    return p.setBusesLayout (layout);
}

// Renders `seconds` of noise + tones through p (prepared here).
juce::AudioBuffer<float> render (LabQ8AudioProcessor& p, int channels, double fs, int blockSize, double seconds = 1.0)
{
    setLayout (p, channels);
    p.prepareToPlay (fs, blockSize);
    const auto total = (int) std::lround (seconds * fs);
    juce::AudioBuffer<float> out (channels, total);
    juce::MidiBuffer midi;
    for (int pos = 0, block = 0; pos < total; pos += blockSize, ++block)
    {
        const auto n = std::min (blockSize, total - pos);
        juce::AudioBuffer<float> buffer (channels, n);
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto noise = labq8::test::signals::makeNoise ((size_t) n, (std::uint32_t) (block * 2 + ch + 1), 0.3f);
            for (int i = 0; i < n; ++i)
            {
                const auto t = (double) (pos + i) / fs;
                buffer.setSample (ch, i, noise[(size_t) i] + 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * (ch == 0 ? 220.0 : 3300.0) * t));
            }
        }
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < channels; ++ch)
            out.copyFrom (ch, pos, buffer, ch, 0, n);
    }
    return out;
}

bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) != 0)
            return false;
    return true;
}

// The fixtures before E4a: every version and the genuine UI-2c states.
const char* const kOldStates[] = {
    "e0_default.bin", "e0_custom.bin", "e0_bypassed.bin",                  // v1
    "e1a_default.bin", "e1a_custom.bin", "e1a_from_e0_custom.bin",         // v2
    "e1b_high_q.bin", "e1b_within_limits.bin", "e2b_custom.bin",           // v3
    "ui2c_default.bin", "ui2c_custom.bin",                                 // v4 (152d46c)
};

} // namespace

//==============================================================================
// Writes the E4a fixtures when LABQ8_WRITE_FIXTURES names a directory (never
// in a normal run; tests/fixtures/README.md).
LQ_TEST ("e4a fixtures: write e4a_custom_v5 and contract_v5_e4a (only with LABQ8_WRITE_FIXTURES)")
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("LABQ8_WRITE_FIXTURES", {});
    if (dir.isEmpty())
    {
        ctx.skip ("LABQ8_WRITE_FIXTURES not set (normal run)");
        return;
    }
    const juce::File out (dir);
    out.createDirectory();
    LabQ8AudioProcessor p;
    makeE4aCustom (p);
    const auto block = save (p);
    out.getChildFile ("e4a_custom_v5.bin").replaceWithData (block.getData(), block.getSize());
    out.getChildFile ("e4a_custom_v5.xml").replaceWithText (xmlOf (block)->toString());

    LabQ8AudioProcessor fresh;
    std::ofstream file (out.getChildFile ("contract_v5_e4a.txt").getFullPathName().toStdString());
    for (const auto& line : contract::describe (fresh))
        file << line << "\n";
    LQ_CHECK (true, "written");
}

//==============================================================================
// 1. The contract.

LQ_TEST ("e4a contract: host indices 0..79 are exactly the v4 contract of 152d46c (IDs, names, hints, host IDs, ranges, defaults, texts)")
{
    const auto frozen = readLines (fixture ("contract_v4_ui2c.txt"));
    LQ_CHECK (frozen.size() == 80, str ("frozen v4 contract has ", frozen.size(), " lines"));

    LabQ8AudioProcessor p;
    const auto now = contract::describe (p);
    LQ_CHECK (now.size() == 241, str ("parameter count ", now.size()));
    for (size_t i = 0; i < std::min (frozen.size(), now.size()); ++i)
        LQ_CHECK (now[i] == frozen[i], str ("index ", i, " changed:\n    v4: ", frozen[i], "\n    now: ", now[i]));
}

LQ_TEST ("e4a contract: all 241 parameters match the reviewed v5 inventory (contract_v5_e4a.txt)")
{
    const auto frozen = readLines (fixture ("contract_v5_e4a.txt"));
    LabQ8AudioProcessor p;
    const auto now = contract::describe (p);
    LQ_CHECK (frozen.size() == 241 && now.size() == 241, str ("inventory ", frozen.size(), " lines, layout ", now.size()));
    int changed = 0;
    for (size_t i = 0; i < std::min (frozen.size(), now.size()); ++i)
        if (now[i] != frozen[i] && ++changed <= 5)
            LQ_CHECK (false, str ("index ", i, ":\n    inventory: ", frozen[i], "\n    now:       ", now[i]));
    LQ_CHECK (changed == 0, str (changed, " parameters differ from the inventory"));
}

LQ_TEST ("e4a contract: proc_mode at 80, MID at 81..160 and SIDE at 161..240, band by band, hint 5, IDs and names as specified")
{
    LabQ8AudioProcessor p;
    const auto& all = p.getParameters();
    LQ_CHECK (all.size() == params::numParameters && params::numParameters == 241, "241 parameters");
    LQ_CHECK (params::procModeIndex == 80 && params::midBankFirstIndex == 81 && params::sideBankFirstIndex == 161, "index constants");

    const auto* mode = dynamic_cast<const juce::AudioParameterChoice*> (all[80]);
    LQ_CHECK (mode != nullptr && mode->paramID == "proc_mode" && mode->getName (64) == "Stereo Mode", "index 80 is proc_mode, \"Stereo Mode\"");
    if (mode != nullptr)
    {
        LQ_CHECK (mode->choices.joinIntoString ("|") == "Stereo|Mid/Side", "proc_mode choices Stereo | Mid/Side (index frozen)");
        LQ_CHECK (mode->getIndex() == 0 && static_cast<const juce::AudioProcessorParameter*> (mode)->getDefaultValue() == 0.0f, "proc_mode defaults to Stereo");
        LQ_CHECK (mode->getVersionHint() == params::versionHintE4 && params::versionHintE4 == 5, "proc_mode hint 5");
        LQ_CHECK (mode->isAutomatable(), "proc_mode automatable");
    }

    const char* suffixes[] = { "freq", "gain", "q", "bypass", "type", "dyn_on", "threshold", "range", "attack", "release" };
    const char* names[] = { "Frequency", "Gain", "Q", "Bypass", "Type", "Dynamic EQ", "Threshold", "Range", "Attack", "Release" };
    for (const auto bank : { params::Bank::mid, params::Bank::side })
    {
        const bool mid = bank == params::Bank::mid;
        const auto inOrder = bankIdsInOrder (bank);
        int index = mid ? 81 : 161;
        for (int b = 0; b < params::numBands; ++b)
            for (int k = 0; k < 10; ++k, ++index)
            {
                const auto expectedId = juce::String (mid ? "mid_b" : "side_b") + juce::String (b + 1) + "_" + suffixes[k];
                const auto expectedName = juce::String (mid ? "Mid Band " : "Side Band ") + juce::String (b + 1) + " " + names[k];
                const auto* r = dynamic_cast<const juce::RangedAudioParameter*> (all[index]);
                LQ_CHECK (r != nullptr && r->paramID == expectedId && expectedId == inOrder[(size_t) (b * 10 + k)],
                          str ("index ", index, " is not ", expectedId));
                LQ_CHECK (r != nullptr && r->getName (64) == expectedName, str (expectedId, " name ", r != nullptr ? r->getName (64) : juce::String()));
                LQ_CHECK (r != nullptr && r->getVersionHint() == params::versionHintE4, str (expectedId, " hint"));
                LQ_CHECK (r != nullptr && r->isAutomatable(), str (expectedId, " automatable"));
            }
    }

    // bankOf: STEREO is the legacy arrays (same storage), banks are disjoint.
    LQ_CHECK (&params::bankOf (params::Bank::stereo).bands == &params::bandIDs
                  && &params::bankOf (params::Bank::stereo).dynamics == &params::dynamicIDs
                  && juce::String (params::bankOf (params::Bank::stereo).namePrefix).isEmpty(),
              "the STEREO bank is bandIDs + dynamicIDs, names unprefixed");
}

LQ_TEST ("e4a contract: every MID and SIDE parameter means exactly what its STEREO counterpart means")
{
    LabQ8AudioProcessor p;
    const auto lines = contract::describe (p);
    std::map<std::string, std::string> meaningById;
    for (const auto& line : lines)
    {
        const auto first = line.find ('|'), second = line.find ('|', first + 1);
        meaningById[line.substr (first + 1, second - first - 1)] = contract::meaning (line);
    }

    const auto stereo = bankIdsInOrder (params::Bank::stereo);
    for (const auto bank : { params::Bank::mid, params::Bank::side })
    {
        const auto other = bankIdsInOrder (bank);
        for (size_t i = 0; i < stereo.size(); ++i)
            LQ_CHECK (meaningById[other[i]] == meaningById[stereo[i]],
                      str (other[i], " differs from ", stereo[i], ":\n    ", meaningById[other[i]], "\n    ", meaningById[stereo[i]]));
    }
}

LQ_TEST ("e4a contract: IDs unique, no reserved ID reused; VST3 / AU host IDs unique, not JUCE's reserved ones; AU order of the 80 v4 parameters unchanged")
{
    LabQ8AudioProcessor p;
    const auto ids = allIds (p);
    std::set<juce::String> unique (ids.begin(), ids.end());
    LQ_CHECK (unique.size() == 241, str (unique.size(), " distinct IDs"));

    // Reserved in PARAMETER_SCHEMA.md for other features: must not exist.
    for (int b = 1; b <= params::numBands; ++b)
        for (const auto* suffix : { "_place", "_slope", "_sc" })
            LQ_CHECK (unique.count ("b" + juce::String (b) + suffix) == 0, str ("reserved b", b, suffix, " used"));
    LQ_CHECK (unique.count ("out_gain") == 0 && unique.count ("master_bypass") == 0, "reserved out_gain / master_bypass used");

    // Host IDs: VST3 Vst::ParamID and AU AudioUnitParameterID (same value).
    // JUCE reserves 'prst' (programs) and 'byps' (the wrapper's bypass).
    std::set<std::uint32_t> hostIds;
    for (const auto& id : ids)
    {
        const auto h = contract::hostParameterId (id);
        LQ_CHECK (h != 0x70727374u && h != 0x62797073u, str (id, " collides with a reserved VST3 ID"));
        LQ_CHECK (hostIds.insert (h).second, str (id, " host ID collides"));
    }

    // AU: kAudioUnitProperty_ParameterList is sorted by host ID, then
    // (stable) by version hint. Hint 5 sorts after 1..4, so the 80 v4
    // parameters keep their AU order.
    auto auOrder = [] (std::vector<std::pair<juce::String, int>> params)
    {
        std::sort (params.begin(), params.end(), [] (auto& a, auto& b) { return contract::hostParameterId (a.first) < contract::hostParameterId (b.first); });
        std::stable_sort (params.begin(), params.end(), [] (auto& a, auto& b) { return a.second < b.second; });
        std::vector<juce::String> v;
        for (auto& x : params)
            v.push_back (x.first);
        return v;
    };
    std::vector<std::pair<juce::String, int>> now, v4;
    for (auto* parameter : p.getParameters())
    {
        const auto* r = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        now.push_back ({ r->paramID, r->getVersionHint() });
    }
    for (const auto& line : readLines (fixture ("contract_v4_ui2c.txt")))
    {
        juce::StringArray f;
        f.addTokens (juce::String (line), "|", "");
        v4.push_back ({ f[1], f[4].fromFirstOccurrenceOf ("hint=", false, false).getIntValue() });
    }
    const auto orderNow = auOrder (now), orderV4 = auOrder (v4);
    LQ_CHECK (orderV4.size() == 80 && std::equal (orderV4.begin(), orderV4.end(), orderNow.begin()), "AU order of the v4 parameters changed");
    LQ_CHECK (std::all_of (orderNow.begin() + 80, orderNow.end(), [] (const juce::String& id) { return id == "proc_mode" || id.startsWith ("mid_") || id.startsWith ("side_"); }),
              "AU: the E4a parameters come after the 80 v4 ones");
}

//==============================================================================
// 2. Migration and serialisation.

LQ_TEST ("e4a state: every earlier state (v1..v4, incl. genuine UI-2c) loads as v5: Stereo, MID / SIDE at defaults, stored values unchanged, deterministic")
{
    for (const auto* file : kOldStates)
    {
        const auto old = loadFixture (file);
        LQ_CHECK (old.getSize() > 0, str ("fixture missing: ", file));
        const auto oldXml = xmlOf (old);

        LabQ8AudioProcessor fresh;
        load (fresh, old);
        LabQ8AudioProcessor used; // history: all 241 moved, proc_mode included
        randomiseAll (used, 77);
        setParam (used, params::procModeID, 1.0f);
        load (used, old);

        for (auto* p : { &fresh, &used })
        {
            LQ_CHECK (getParam (*p, params::procModeID) == 0.0f, str (file, ": proc_mode not Stereo"));
            for (const auto* id : newIds())
                LQ_CHECK (getParam (*p, id) == defaultOf (*p, id), str (file, ": ", id, " = ", getParam (*p, id), " not its default"));
        }

        if (oldXml != nullptr)
            for (auto* stored : oldXml->getChildWithTagNameIterator ("PARAM"))
            {
                const auto id = stored->getStringAttribute ("id");
                const auto value = (float) stored->getDoubleAttribute ("value");
                LQ_CHECK (nearlyStored (getParam (fresh, id.toRawUTF8()), value), str (file, ": ", id, " = ", getParam (fresh, id.toRawUTF8()), " stored ", value));
            }

        const auto a = save (fresh), b = save (used);
        LQ_CHECK (a == b, str (file, ": migrated state depends on instance history"));

        const auto migrated = xmlOf (a);
        bool explicitValues = migrated != nullptr && migrated->getNumChildElements() == 241
                           && migrated->getIntAttribute (params::stateVersionProperty) == 5;
        if (migrated != nullptr)
            for (auto* child : migrated->getChildIterator())
                explicitValues = explicitValues && child->hasTagName ("PARAM") && child->hasAttribute ("value");
        LQ_CHECK (explicitValues, str (file, ": re-saved as v5 with 241 explicit values"));

        // Saved again: a fixed point.
        LabQ8AudioProcessor again;
        load (again, a);
        LQ_CHECK (save (again) == a, str (file, ": v5 re-save is not a fixed point"));
    }
}

LQ_TEST ("e4a state: v5 serialisation: root LABQ8, labq8StateVersion=5, one PARAM per parameter (241, explicit values), nothing else")
{
    LabQ8AudioProcessor p;
    makeE4aCustom (p);
    const auto xml = xmlOf (save (p));
    LQ_CHECK (xml != nullptr && xml->hasTagName ("LABQ8"), "root tag");
    if (xml == nullptr)
        return;
    LQ_CHECK (xml->getNumAttributes() == 1 && xml->getIntAttribute ("labq8StateVersion") == 5, "only the version attribute, 5");

    std::set<juce::String> seen;
    for (auto* child : xml->getChildIterator())
    {
        LQ_CHECK (child->hasTagName ("PARAM") && child->getNumAttributes() == 2 && child->hasAttribute ("id") && child->hasAttribute ("value"),
                  str ("unexpected child ", child->toString (juce::XmlElement::TextFormat().singleLine())));
        seen.insert (child->getStringAttribute ("id"));
    }
    const auto ids = allIds (p);
    LQ_CHECK (seen.size() == 241 && std::all_of (ids.begin(), ids.end(), [&] (const juce::String& id) { return seen.count (id) == 1; }),
              "exactly the 241 parameter IDs");
    LQ_CHECK (xml->getChildByAttribute ("id", "proc_mode")->getDoubleAttribute ("value") == 1.0, "proc_mode stored as its choice index");
}

LQ_TEST ("e4a state: v5 round trip restores all 241 parameters exactly and is a fixed point (random sessions)")
{
    for (unsigned seed = 1; seed <= 20; ++seed)
    {
        LabQ8AudioProcessor a;
        randomiseAll (a, seed);
        const auto block = save (a);
        LabQ8AudioProcessor b;
        randomiseAll (b, seed + 1000);
        load (b, block);
        // Compared as the values the DSP reads (raw, denormalised), like the
        // E2c.1 round-trip test.
        std::vector<float> va, vb;
        for (const auto& id : allIds (a))
        {
            va.push_back (getParam (a, id.toRawUTF8()));
            vb.push_back (getParam (b, id.toRawUTF8()));
        }
        int differ = 0;
        for (size_t i = 0; i < va.size(); ++i)
            differ += va[i] != vb[i] ? 1 : 0;
        // Frequency and Q (frozen since E0) may move by one ulp on their
        // first save (log mapping; E2c.1 measured it); the second save is
        // exact. Everything else must be exact at once.
        LabQ8AudioProcessor c;
        load (c, save (b));
        LQ_CHECK (save (c) == save (b), str ("seed ", seed, ": second save differs"));
        int nonLogDiffer = 0;
        const auto ids = allIds (a);
        for (size_t i = 0; i < va.size(); ++i)
            if (va[i] != vb[i] && ! (ids[i].endsWith ("_freq") || ids[i].endsWith ("_q")) && ++nonLogDiffer <= 3)
                LQ_CHECK (false, str ("seed ", seed, ": ", ids[i], " ", va[i], " -> ", vb[i]));
        LQ_CHECK (nonLogDiffer == 0, str ("seed ", seed, ": ", nonLogDiffer, " non-log parameters changed in a round trip"));
        if (seed == 1)
            ctx.note (str ("seed 1: ", differ, " of 241 values moved by an ulp in the first round trip (frequency / Q log mapping, as in v4)"));
    }
}

LQ_TEST ("e4a state: genuine v5 fixture restores every value; partial v5, unknown IDs and a newer version are handled deterministically")
{
    // The fixture written by these sources (makeE4aCustom).
    const auto v5 = loadFixture ("e4a_custom_v5.bin");
    LQ_CHECK (v5.getSize() > 0, "fixture missing: e4a_custom_v5.bin");
    LabQ8AudioProcessor reference;
    makeE4aCustom (reference);
    LabQ8AudioProcessor fromFile;
    load (fromFile, v5);
    for (const auto& id : allIds (reference))
        LQ_CHECK (nearlyStored (getParam (fromFile, id.toRawUTF8()), getParam (reference, id.toRawUTF8())), str ("e4a_custom_v5: ", id));

    // Partial: missing MID / SIDE / proc_mode entries get their defaults.
    auto xml = xmlOf (v5);
    for (const auto* id : { "proc_mode", "mid_b3_gain", "side_b7_dyn_on", "side_b1_type" })
        xml->removeChildElement (xml->getChildByAttribute ("id", id), true);
    // Unknown, from a future version.
    auto* unknown = xml->createNewChildElement ("PARAM");
    unknown->setAttribute ("id", "future_param");
    unknown->setAttribute ("value", 0.5);
    juce::MemoryBlock partial;
    juce::AudioProcessor::copyXmlToBinary (*xml, partial);

    LabQ8AudioProcessor target;
    randomiseAll (target, 5);
    setParam (target, "proc_mode", 1.0f);
    load (target, partial);
    for (const auto* id : { "proc_mode", "mid_b3_gain", "side_b7_dyn_on", "side_b1_type" })
        LQ_CHECK (getParam (target, id) == defaultOf (target, id), str ("missing ", id, " not reset to its default"));
    LQ_CHECK (nearlyStored (getParam (target, "mid_b4_gain"), getParam (reference, "mid_b4_gain")), "present MID value restored");
    LQ_CHECK (target.getValueTreeState().getParameter ("future_param") == nullptr, "unknown ID creates no parameter");
    const auto resaved = xmlOf (save (target));
    LQ_CHECK (resaved != nullptr && resaved->getChildByAttribute ("id", "future_param") != nullptr
                  && resaved->getChildByAttribute ("id", "future_param")->getDoubleAttribute ("value") == 0.5,
              "unknown PARAM kept and saved again unchanged");
    LQ_CHECK (resaved != nullptr && resaved->getNumChildElements() == 242, "241 + the unknown one");

    // A newer version (v6): known values restored, unknown kept, saved as v5.
    xml = xmlOf (v5);
    xml->setAttribute ("labq8StateVersion", 6);
    auto* newer = xml->createNewChildElement ("PARAM");
    newer->setAttribute ("id", "v6_param");
    newer->setAttribute ("value", 3.0);
    juce::MemoryBlock v6;
    juce::AudioProcessor::copyXmlToBinary (*xml, v6);
    LabQ8AudioProcessor fromV6;
    load (fromV6, v6);
    LQ_CHECK (getParam (fromV6, "proc_mode") == 1.0f && nearlyStored (getParam (fromV6, "side_b2_gain"), getParam (reference, "side_b2_gain")), "v6: known values restored");
    const auto v6Saved = xmlOf (save (fromV6));
    LQ_CHECK (v6Saved != nullptr && v6Saved->getIntAttribute ("labq8StateVersion") == 5 && v6Saved->getChildByAttribute ("id", "v6_param") != nullptr,
              "v6: saved back as v5 with the unknown entry kept");
}

LQ_TEST ("e4a state: a v5 session opened and saved again by the v4 build (152d46c) keeps its MID, SIDE and proc_mode values")
{
    // ui2c_resaved_from_v5.bin: e4a_custom_v5.bin opened in 152d46c (which
    // plays only its STEREO bank) and saved there. The v4 build keeps the
    // entries it does not know and labels the state 4.
    const auto resaved = loadFixture ("ui2c_resaved_from_v5.bin");
    LQ_CHECK (resaved.getSize() > 0, "fixture missing: ui2c_resaved_from_v5.bin");
    const auto xml = xmlOf (resaved);
    LQ_CHECK (xml != nullptr && xml->getIntAttribute ("labq8StateVersion") == 4 && xml->getNumChildElements() == 241,
              "152d46c saved it as v4 with all 241 entries");

    LabQ8AudioProcessor reference;
    makeE4aCustom (reference);
    LabQ8AudioProcessor back;
    load (back, resaved);
    int wrong = 0;
    for (const auto& id : allIds (reference))
        if (! nearlyStored (getParam (back, id.toRawUTF8()), getParam (reference, id.toRawUTF8())) && ++wrong <= 5)
            LQ_CHECK (false, str (id, ": ", getParam (back, id.toRawUTF8()), " expected ", getParam (reference, id.toRawUTF8())));
    LQ_CHECK (wrong == 0, str (wrong, " values lost through the v4 build"));
}

//==============================================================================
// 3. Independence of the banks (parameters) and STEREO sound.

LQ_TEST ("e4a parameters: writing any one of the 241 parameters (host automation path) changes that parameter only")
{
    LabQ8AudioProcessor p;
    randomiseAll (p, 9);
    const auto ids = allIds (p);
    auto& all = p.getParameters();
    int leaks = 0;
    for (int i = 0; i < all.size(); ++i)
    {
        const auto before = allValues (p);
        auto* parameter = all[i];
        const auto target = before[(size_t) i] < 0.5f ? 0.9f : 0.1f;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (target);
        parameter->endChangeGesture();
        const auto after = allValues (p);
        LQ_CHECK (after[(size_t) i] != before[(size_t) i], str (ids[(size_t) i], " did not change"));
        for (size_t j = 0; j < after.size(); ++j)
            if (j != (size_t) i && after[j] != before[j] && ++leaks <= 5)
                LQ_CHECK (false, str ("writing ", ids[(size_t) i], " changed ", ids[j]));
        // Same through the value the DSP reads.
        LQ_CHECK (p.getValueTreeState().getRawParameterValue (ids[(size_t) i])->load()
                      == dynamic_cast<juce::RangedAudioParameter*> (parameter)->convertFrom0to1 (after[(size_t) i]),
                  str (ids[(size_t) i], ": raw value not updated"));
    }
    LQ_CHECK (leaks == 0, str (leaks, " cross-writes"));
}

LQ_TEST ("e4a audio: in Stereo the output is bit-identical whatever the MID / SIDE banks hold (mono and stereo, 3 rates, 2 block sizes)")
{
    for (const auto channels : { 1, 2 })
        for (const auto fs : labq8::test::signals::sampleRates)
            for (const auto blockSize : { 64, 512 })
            {
                LabQ8AudioProcessor plain;
                load (plain, loadFixture ("ui2c_custom.bin")); // STEREO bank with dynamics; MID / SIDE defaults

                LabQ8AudioProcessor busy;
                randomiseAll (busy, 31);
                load (busy, loadFixture ("ui2c_custom.bin"));
                for (const auto* id : newIds()) // every MID / SIDE value away from its default
                    param (busy, id).setValueNotifyingHost (0.37f);
                setParam (busy, params::procModeID, 0.0f);

                LQ_CHECK (identical (render (plain, channels, fs, blockSize), render (busy, channels, fs, blockSize)),
                          str ("MID / SIDE values changed the STEREO sound: ", channels, " ch, ", fs, " Hz, block ", blockSize));
            }
}
