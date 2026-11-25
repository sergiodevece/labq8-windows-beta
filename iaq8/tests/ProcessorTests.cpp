// Headless tests of the IAQ8 host adapter: parameters -> applied path,
// proposals -> preview path, apply/undo, state and real-time safety.

// Exact float comparisons are intentional (unchanged parameters, untorn copies).
#pragma clang diagnostic ignored "-Wfloat-equal"

#include "AllocationGuard.h"
#include "TestHarness.h"
#include "TestSignals.h"

#include "parameters/Iaq8Parameters.h"
#include "parameters/ParameterIDs.h"
#include "plugin/Iaq8Processor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <thread>
#include <vector>

using namespace iaq8;
using namespace labq8::test::signals;
using labq8::dsp::BandSettings;
using labq8::dsp::FilterType;
using labq8::params::bandIDs;
using labq8::test::ScopedAllocationCounter;
using labq8::test::str;

namespace
{
constexpr double kFs = 48000.0;
constexpr int kBlock = 512;

juce::RangedAudioParameter& param (Iaq8AudioProcessor& p, const char* id)
{
    auto* r = p.getState().getParameter (id);
    jassert (r != nullptr);
    return *r;
}

void setParam (Iaq8AudioProcessor& p, const char* id, float value)
{
    auto& r = param (p, id);
    r.setValueNotifyingHost (r.convertTo0to1 (value));
}

void prepare (Iaq8AudioProcessor& p, int channels, double fs, int block)
{
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    p.setBusesLayout (layout);
    p.setRateAndBufferSizeDetails (fs, block);
    p.prepareToPlay (fs, block);
}

// Feeds `x` to every channel in blocks of `block`; returns channel 0.
// `beforeBlock (position)` runs on this (message) thread before each block.
Signal run (Iaq8AudioProcessor& p, const Signal& x, int block,
            const std::function<void (size_t)>& beforeBlock = {})
{
    const auto channels = p.getTotalNumOutputChannels();
    Signal y (x.size());
    juce::AudioBuffer<float> buffer (channels, block);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos < x.size(); pos += (size_t) block)
    {
        if (beforeBlock)
            beforeBlock (pos);
        const auto n = (int) std::min ((size_t) block, x.size() - pos);
        buffer.setSize (channels, n, false, false, true);
        for (int c = 0; c < channels; ++c)
            buffer.copyFrom (c, 0, x.data() + pos, n);
        p.processBlock (buffer, midi);
        std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + n, y.data() + pos);
    }
    return y;
}

Signal twoTone (double fs, size_t n)
{
    auto x = makeSine (1000.0, fs, n, 0.2);
    const auto hi = makeSine (5000.0, fs, n, 0.2);
    for (size_t i = 0; i < n; ++i)
        x[i] += hi[i];
    return x;
}

// Level of the 1 kHz tone relative to the 5 kHz tone in y[begin, end), dB
// (the input has both at the same level). Broadband gain cancels out.
double toneRatioDb (const Signal& y, size_t begin, size_t end, double fs)
{
    return toDb (fitAmplitude (y, begin, end, 1000.0, fs) / fitAmplitude (y, begin, end, 5000.0, fs));
}

// -12 dB bell at 1 kHz, Q 4 (5 kHz is untouched); everything else flat.
dsp::BandSet cutAt1k()
{
    dsp::BandSet s {};
    s[3] = { 1000.0, -12.0, 4.0, false, FilterType::bell };
    return s;
}

bool closeRel (double a, double b, double tol) { return std::abs (a - b) <= tol * std::max (1.0, std::abs (b)); }

// Applied sets compare within each parameter's resolution: frequency and Q
// are stored as float, gain on a 0.01 dB grid.
bool sameApplied (const dsp::BandSet& a, const dsp::BandSet& b)
{
    for (size_t i = 0; i < a.size(); ++i)
        if (! closeRel (a[i].frequencyHz, b[i].frequencyHz, 1.0e-4) || std::abs (a[i].gainDb - b[i].gainDb) > 0.006
            || ! closeRel (a[i].q, b[i].q, 1.0e-4) || a[i].bypassed != b[i].bypassed || a[i].type != b[i].type)
            return false;
    return true;
}

struct Recorder final : juce::AudioProcessorParameter::Listener
{
    std::array<int, 64> values {}, begins {}, ends {};

    void attach (Iaq8AudioProcessor& p)
    {
        for (auto* prm : p.getParameters())
            prm->addListener (this);
    }
    void detach (Iaq8AudioProcessor& p)
    {
        for (auto* prm : p.getParameters())
            prm->removeListener (this);
    }
    int total() const
    {
        int t = 0;
        for (size_t i = 0; i < values.size(); ++i)
            t += values[i] + begins[i] + ends[i];
        return t;
    }
    void parameterValueChanged (int index, float) override { ++values[(size_t) index]; }
    void parameterGestureChanged (int index, bool starting) override { ++(starting ? begins : ends)[(size_t) index]; }
};

std::vector<float> normalisedValues (Iaq8AudioProcessor& p)
{
    std::vector<float> v;
    for (auto* prm : p.getParameters())
        v.push_back (prm->getValue());
    return v;
}
} // namespace

LQ_TEST ("processor: parameters drive the applied path")
{
    Iaq8AudioProcessor p;
    prepare (p, 2, kFs, kBlock);
    setParam (p, bandIDs[0].gain, -6.0f); // band 1 defaults: bell, 1 kHz, Q 1

    const auto n = samples (1.0, kFs);
    const auto y = run (p, makeSine (1000.0, kFs, n), kBlock);
    const auto db = toDb (fitAmplitude (y, samples (0.5, kFs), n, 1000.0, kFs) / 0.25);
    LQ_CHECK (std::abs (db + 6.0) < 0.05, str ("1 kHz through b1_gain -6 dB: ", db, " dB"));
}

LQ_TEST ("processor: prepareToPlay snaps to the host values (no ramp from defaults)")
{
    Iaq8AudioProcessor p;
    setParam (p, bandIDs[0].gain, -6.0f);
    prepare (p, 2, kFs, kBlock);

    const auto n = samples (0.05, kFs);
    const auto y = run (p, makeSine (1000.0, kFs, n), kBlock);
    // 5..20 ms: a 20 ms ramp from 0 dB would average about -3.75 dB here.
    const auto db = toDb (fitAmplitude (y, samples (0.005, kFs), samples (0.020, kFs), 1000.0, kFs) / 0.25);
    LQ_CHECK (std::abs (db + 6.0) < 0.3, str ("first 20 ms already at -6 dB: ", db, " dB"));
}

LQ_TEST ("processor: a proposal is audible in preview and never touches parameters")
{
    Recorder recorder;
    Iaq8AudioProcessor p;
    prepare (p, 2, kFs, kBlock);
    recorder.attach (p);
    const auto appliedBefore = p.getAppliedBands();
    const auto valuesBefore = normalisedValues (p);

    p.setProposal (cutAt1k());
    p.setListen (dsp::Listen::preview);
    LQ_CHECK (recorder.total() == 0, "setProposal/setListen send no parameter event");
    LQ_CHECK (p.hasProposal(), "proposal pending");

    const auto n = samples (1.0, kFs);
    auto y = run (p, twoTone (kFs, n), kBlock);
    const auto previewDb = toneRatioDb (y, samples (0.5, kFs), n, kFs);
    LQ_CHECK (std::abs (previewDb + 12.0) < 0.5, str ("preview plays the proposal: 1k/5k = ", previewDb, " dB"));

    p.setListen (dsp::Listen::applied);
    y = run (p, twoTone (kFs, n), kBlock);
    const auto appliedDb = toneRatioDb (y, samples (0.1, kFs), n, kFs);
    LQ_CHECK (std::abs (appliedDb) < 0.1, str ("applied stays flat: 1k/5k = ", appliedDb, " dB"));

    LQ_CHECK (recorder.total() == 0, "processing a proposal sends no parameter event");
    LQ_CHECK (normalisedValues (p) == valuesBefore, "no parameter value changed");
    LQ_CHECK (sameApplied (p.getAppliedBands(), appliedBefore), "applied set unchanged");
    recorder.detach (p);
}

LQ_TEST ("processor: apply writes all 40 parameters in one gesture each and clears the proposal")
{
    Recorder recorder;
    Iaq8AudioProcessor p;
    prepare (p, 2, kFs, kBlock);
    LQ_CHECK (! p.applyProposal(), "nothing to apply without a proposal");

    auto proposal = p.getAppliedBands();
    proposal[0] = { 800.0, -4.5, 2.0, false, FilterType::bell };
    proposal[2] = { 6000.0, 2.0, 0.7, false, FilterType::highShelf };
    proposal[5].bypassed = true;
    proposal[7].frequencyHz = 30.0;
    proposal[7].type = FilterType::lowCut;

    p.setProposal (proposal);
    recorder.attach (p);
    const auto before = normalisedValues (p);
    LQ_CHECK (p.applyProposal(), "apply succeeds");
    const auto after = normalisedValues (p);

    LQ_CHECK (sameApplied (p.getAppliedBands(), proposal), "applied set equals the proposal");
    LQ_CHECK (! p.hasProposal(), "proposal cleared");
    LQ_CHECK (p.getUndoDepth() == 1, "one undo step");

    int changed = 0;
    bool gesturesOk = true;
    for (size_t i = 0; i < before.size(); ++i)
    {
        const int expected = before[i] != after[i] ? 1 : 0;
        changed += expected;
        gesturesOk = gesturesOk && recorder.begins[i] == expected && recorder.ends[i] == expected
                     && recorder.values[i] == expected;
    }
    LQ_CHECK (before.size() == 40, "40 parameters");
    LQ_CHECK (changed == 10, str ("10 parameters changed, got ", changed));
    LQ_CHECK (gesturesOk, "one begin/value/end per changed parameter, nothing for the others");
    recorder.detach (p);

    // The audio follows the new applied set: b1 is now -4.5 dB at 800 Hz.
    const auto n = samples (1.0, kFs);
    const auto y = run (p, makeSine (800.0, kFs, n), kBlock);
    const auto db = toDb (fitAmplitude (y, samples (0.5, kFs), n, 800.0, kFs) / 0.25);
    LQ_CHECK (std::abs (db + 4.5) < 0.1, str ("800 Hz after apply: ", db, " dB"));
}

LQ_TEST ("processor: undo restores the previous applied set")
{
    Iaq8AudioProcessor p;
    prepare (p, 2, kFs, kBlock);
    LQ_CHECK (! p.undoApply(), "undo without history is false");

    const auto initial = p.getAppliedBands();
    auto p1 = initial;
    p1[1] = { 2000.0, 3.0, 1.5, false, FilterType::bell };
    auto p2 = p1;
    p2[1].gainDb = -7.0;
    p2[4] = { 9000.0, -2.0, 0.8, true, FilterType::highShelf };

    p.setProposal (p1);
    p.applyProposal();
    const auto afterP1 = p.getAppliedBands();
    p.setProposal (p2);
    p.applyProposal();
    LQ_CHECK (p.getUndoDepth() == 2, "two undo steps");

    Recorder recorder;
    recorder.attach (p);
    LQ_CHECK (p.undoApply(), "first undo");
    LQ_CHECK (sameApplied (p.getAppliedBands(), afterP1), "back to the first proposal");
    bool balanced = recorder.total() > 0;
    for (size_t i = 0; i < recorder.begins.size(); ++i)
        balanced = balanced && recorder.begins[i] == recorder.ends[i] && recorder.begins[i] <= 1;
    LQ_CHECK (balanced, "undo writes with one begin/end gesture per changed parameter");
    recorder.detach (p);

    LQ_CHECK (p.undoApply(), "second undo");
    LQ_CHECK (sameApplied (p.getAppliedBands(), initial), "back to the initial set");
    LQ_CHECK (! p.undoApply(), "history exhausted");
    LQ_CHECK (p.getUndoDepth() == 0, "empty history");

    for (int k = 0; k < 20; ++k)
    {
        auto s = initial;
        s[0].gainDb = -0.5 * (k + 1);
        p.setProposal (s);
        p.applyProposal();
    }
    LQ_CHECK (p.getUndoDepth() == Iaq8AudioProcessor::maxUndoDepth, str ("history capped at 16, got ", p.getUndoDepth()));
}

LQ_TEST ("processor: state round trip keeps applied set and conversation, drops pending proposal")
{
    Iaq8AudioProcessor a;
    setParam (a, bandIDs[0].gain, -7.25f);
    auto applied = a.getAppliedBands();
    applied[1] = { 2000.0, 3.0, 1.5, false, FilterType::bell };
    a.setProposal (applied);
    a.applyProposal();

    juce::ValueTree conversation (Iaq8AudioProcessor::assistantType);
    conversation.setProperty ("source", "voice", nullptr);
    juce::ValueTree message ("MESSAGE");
    message.setProperty ("role", "user", nullptr);
    message.setProperty ("text", juce::String (juce::CharPointer_UTF8 ("\xc2\xbf" "C\xc3\xb3mo suena?")), nullptr);
    conversation.appendChild (message, nullptr);
    a.setAssistantState (conversation);
    message.setProperty ("text", "changed after the call", nullptr); // deep copy: no effect
    a.getAssistantState().setProperty ("source", "bass", nullptr);   // returned copy: no effect

    a.setProposal (cutAt1k()); // pending: must never be saved
    juce::MemoryBlock withProposal, withoutProposal;
    a.getStateInformation (withProposal);
    a.clearProposal();
    a.getStateInformation (withoutProposal);
    LQ_CHECK (withProposal.getSize() > 0, "state not empty");
    LQ_CHECK (withProposal == withoutProposal, "a pending proposal is not part of the state");

    const auto xml = juce::AudioProcessor::getXmlFromBinary (withProposal.getData(), (int) withProposal.getSize());
    LQ_CHECK (xml != nullptr && xml->hasTagName (Iaq8AudioProcessor::stateType), "root tag IAQ8");
    LQ_CHECK (xml != nullptr && xml->getIntAttribute (Iaq8AudioProcessor::stateVersionProperty) == iaq8::params::stateVersion,
              "state version saved");
    LQ_CHECK (xml != nullptr && xml->getChildByName (Iaq8AudioProcessor::assistantType) != nullptr, "ASSISTANT child saved");
    if (xml == nullptr)
        return;

    Iaq8AudioProcessor b;
    setParam (b, bandIDs[0].gain, 5.0f); // must be overwritten
    b.setStateInformation (withProposal.getData(), (int) withProposal.getSize());
    LQ_CHECK (sameApplied (b.getAppliedBands(), a.getAppliedBands()), "applied set restored");
    LQ_CHECK (std::abs (b.getAppliedBands()[0].gainDb + 7.25) < 0.006, "b1_gain restored");
    const auto restored = b.getAssistantState();
    LQ_CHECK (restored.hasType (Iaq8AudioProcessor::assistantType), "ASSISTANT type");
    LQ_CHECK (restored.getProperty ("source").toString() == "voice", "source property restored");
    LQ_CHECK (restored.getNumChildren() == 1
                  && restored.getChild (0).getProperty ("text").toString()
                         == juce::String (juce::CharPointer_UTF8 ("\xc2\xbf" "C\xc3\xb3mo suena?")),
              "message child restored (UTF-8)");
    LQ_CHECK (restored.isEquivalentTo (a.getAssistantState()), "conversation equal to the saved one");

    juce::MemoryBlock again;
    b.getStateInformation (again);
    LQ_CHECK (again == withoutProposal, "round trip is idempotent");

    // A state without ASSISTANT child loads the parameters and an empty conversation.
    auto bare = juce::ValueTree::fromXml (*xml);
    bare.removeChild (bare.getChildWithName (Iaq8AudioProcessor::assistantType), nullptr);
    juce::MemoryBlock bareBlock;
    juce::AudioProcessor::copyXmlToBinary (*bare.createXml(), bareBlock);
    b.setStateInformation (bareBlock.getData(), (int) bareBlock.getSize());
    LQ_CHECK (sameApplied (b.getAppliedBands(), a.getAppliedBands()), "parameters from a state without ASSISTANT");
    LQ_CHECK (b.getAssistantState().getNumProperties() == 0 && b.getAssistantState().getNumChildren() == 0,
              "missing ASSISTANT child gives an empty conversation");

    // Corrupt or foreign data is ignored.
    const char garbage[] = "not a state";
    b.setStateInformation (garbage, (int) sizeof (garbage));
    LQ_CHECK (std::abs (b.getAppliedBands()[0].gainDb + 7.25) < 0.006, "corrupt state ignored");
}

LQ_TEST ("processor: an in-instance state load drops the proposal and the undo history")
{
    Iaq8AudioProcessor p;
    prepare (p, 2, kFs, kBlock);
    juce::MemoryBlock neutral;
    p.getStateInformation (neutral);

    auto s = p.getAppliedBands();
    s[0].gainDb = -3.0;
    p.setProposal (s);
    p.applyProposal();
    p.setProposal (cutAt1k());
    p.setListen (dsp::Listen::preview);
    LQ_CHECK (p.getUndoDepth() == 1 && p.hasProposal() && p.getListenRequest() == dsp::Listen::preview, "set up");

    const auto loadsBefore = p.getStateLoadCount();
    // Host preset change / A-B compare: same instance, off the message thread.
    std::thread ([&] { p.setStateInformation (neutral.getData(), (int) neutral.getSize()); }).join();
    LQ_CHECK (p.getStateLoadCount() == loadsBefore + 1, "the load is counted");
    LQ_CHECK (p.getListenRequest() == dsp::Listen::applied, "the old proposal is silenced at once (any thread)");

    // Message thread: the first API call notices the load.
    LQ_CHECK (! p.hasProposal() && ! p.getProposal().has_value(), "proposal dropped");
    LQ_CHECK (p.getUndoDepth() == 0 && ! p.undoApply(), "undo history dropped");
    LQ_CHECK (std::abs (p.getAppliedBands()[0].gainDb) < 0.006, "loaded parameters");

    // The audio thread gets the clear: the preview path no longer differs.
    p.setListen (dsp::Listen::preview);
    const auto n = samples (0.3, kFs);
    const auto y = run (p, twoTone (kFs, n), kBlock);
    const auto db = toneRatioDb (y, samples (0.05, kFs), n, kFs);
    LQ_CHECK (std::abs (db) < 0.3, str ("preview plays the applied set after the load: ", db, " dB"));

    // Corrupt data is not a load.
    const char garbage[] = "not a state";
    p.setStateInformation (garbage, (int) sizeof (garbage));
    LQ_CHECK (p.getStateLoadCount() == loadsBefore + 1, "corrupt data: not counted");
}

LQ_TEST ("processor: a conversation write never overwrites a newer loaded one")
{
    Iaq8AudioProcessor p;
    juce::ValueTree mine (Iaq8AudioProcessor::assistantType);
    mine.setProperty ("source", "voice", nullptr);
    std::uint32_t loads = 0;
    const auto current = p.getAssistantState (loads);
    LQ_CHECK (current.hasType (Iaq8AudioProcessor::assistantType) && loads == p.getStateLoadCount(), "state and counter");
    LQ_CHECK (p.setAssistantStateUnlessLoaded (mine, loads), "written while no load happened");

    juce::MemoryBlock other;
    {
        Iaq8AudioProcessor q;
        juce::ValueTree theirs (Iaq8AudioProcessor::assistantType);
        theirs.setProperty ("source", "bass", nullptr);
        q.setAssistantState (theirs);
        q.getStateInformation (other);
    }
    std::thread ([&] { p.setStateInformation (other.getData(), (int) other.getSize()); }).join();
    LQ_CHECK (! p.setAssistantStateUnlessLoaded (mine, loads), "refused after a load");
    LQ_CHECK (p.getAssistantState().getProperty ("source").toString() == "bass", "the loaded conversation stays");
}

LQ_TEST ("processor: prepareToPlay again keeps applied and preview")
{
    Iaq8AudioProcessor p;
    prepare (p, 2, kFs, kBlock);
    // Applied: -6 dB at 5 kHz (b5), so the applied path is recognisable too.
    setParam (p, bandIDs[4].frequency, 5000.0f);
    setParam (p, bandIDs[4].gain, -6.0f);
    setParam (p, bandIDs[4].q, 4.0f);
    p.setProposal (cutAt1k());
    p.setListen (dsp::Listen::preview);
    run (p, twoTone (kFs, samples (0.5, kFs)), kBlock);

    constexpr double fs2 = 96000.0;
    prepare (p, 2, fs2, 256);
    const auto n = samples (0.3, fs2);
    auto y = run (p, twoTone (fs2, n), 256);
    const auto previewDb = toneRatioDb (y, samples (0.02, fs2), n, fs2);
    LQ_CHECK (std::abs (previewDb + 12.0) < 0.5, str ("still previewing the proposal after re-prepare: ", previewDb, " dB"));
    LQ_CHECK (p.hasProposal(), "proposal still pending");

    p.setListen (dsp::Listen::applied);
    y = run (p, twoTone (fs2, n), 256);
    const auto appliedDb = toneRatioDb (y, samples (0.05, fs2), n, fs2);
    LQ_CHECK (std::abs (appliedDb - 6.0) < 0.3, str ("applied set kept after re-prepare: 1k/5k = ", appliedDb, " dB"));
}

LQ_TEST ("processor: automation on an applied parameter while previewing")
{
    // Listen::applied: moving b2_gain mid-stream changes the output.
    {
        Iaq8AudioProcessor p;
        setParam (p, bandIDs[1].frequency, 1000.0f);
        prepare (p, 2, kFs, kBlock);
        p.setProposal (cutAt1k());
        p.setListen (dsp::Listen::applied);
        const auto n = samples (2.0, kFs), change = samples (1.0, kFs);
        const auto y = run (p, makeSine (1000.0, kFs, n), kBlock, [&] (size_t pos)
        {
            if (pos <= change && change < pos + (size_t) kBlock)
                setParam (p, bandIDs[1].gain, -6.0f);
        });
        const auto beforeDb = toDb (fitAmplitude (y, samples (0.5, kFs), change, 1000.0, kFs) / 0.25);
        const auto afterDb = toDb (fitAmplitude (y, change + samples (0.5, kFs), n, 1000.0, kFs) / 0.25);
        LQ_CHECK (std::abs (beforeDb) < 0.05, str ("before: ", beforeDb, " dB"));
        LQ_CHECK (std::abs (afterDb + 6.0) < 0.05, str ("after b2_gain -6: ", afterDb, " dB"));
    }

    // Listen::preview: the compensation follows the new applied set.
    {
        Iaq8AudioProcessor p;
        setParam (p, bandIDs[1].frequency, 2000.0f);
        setParam (p, bandIDs[1].type, (float) FilterType::highShelf);
        prepare (p, 2, kFs, kBlock);
        dsp::BandSet proposal {};
        proposal[3] = { 3000.0, 2.0, 1.0, false, FilterType::bell };
        p.setProposal (proposal);
        p.setListen (dsp::Listen::preview);

        const auto n = samples (5.0, kFs);
        run (p, makeNoise (n, 77, 0.25f), kBlock);
        const auto c1 = p.getCompensationDb();
        setParam (p, bandIDs[1].gain, 5.0f); // applied gets much louder
        run (p, makeNoise (n, 78, 0.25f), kBlock);
        const auto c2 = p.getCompensationDb();
        ctx.note (str ("compensation before ", c1, " dB, after ", c2, " dB"));
        LQ_CHECK (c1 < 0.0, "preview louder than flat applied: negative compensation");
        LQ_CHECK (c2 - c1 > 3.0, "compensation recomputed against the new applied set");
    }
}

LQ_TEST ("processor: mono bus processes")
{
    Iaq8AudioProcessor p;
    prepare (p, 1, kFs, kBlock);
    LQ_CHECK (p.getTotalNumInputChannels() == 1 && p.getTotalNumOutputChannels() == 1, "mono bus");

    setParam (p, bandIDs[0].gain, -6.0f);
    const auto n = samples (1.0, kFs);
    auto y = run (p, makeSine (1000.0, kFs, n), kBlock);
    const auto db = toDb (fitAmplitude (y, samples (0.5, kFs), n, 1000.0, kFs) / 0.25);
    LQ_CHECK (std::abs (db + 6.0) < 0.05, str ("mono applied: ", db, " dB"));

    setParam (p, bandIDs[0].gain, 0.0f);
    p.setProposal (cutAt1k());
    p.setListen (dsp::Listen::preview);
    y = run (p, twoTone (kFs, n), kBlock);
    const auto previewDb = toneRatioDb (y, samples (0.5, kFs), n, kFs);
    LQ_CHECK (std::abs (previewDb + 12.0) < 0.5, str ("mono preview: ", previewDb, " dB"));
}

LQ_TEST ("processor: processBlock does not allocate")
{
    Iaq8AudioProcessor p;
    prepare (p, 2, kFs, kBlock);
    juce::AudioBuffer<float> big (2, 4 * kBlock); // larger than announced: chunked inside
    juce::MidiBuffer midi;
    const auto noise = makeNoise ((size_t) big.getNumSamples(), 5);
    long allocations = 0;
    p.getCapture().arm(); // the capture write path runs inside the counted blocks

    for (int i = 0; i < 60; ++i)
    {
        // Message-thread actions between blocks (outside the counter).
        if (i % 10 == 1)
        {
            auto s = cutAt1k();
            s[3].gainDb = -1.0 - i * 0.1;
            p.setProposal (s);
        }
        if (i % 10 == 6)
            p.clearProposal();
        if (i % 4 == 0)
            p.setListen (i % 8 == 0 ? dsp::Listen::preview : dsp::Listen::applied);
        if (i % 7 == 3)
            setParam (p, bandIDs[2].gain, (float) (i % 5) - 2.0f);
        if (i == 30)
        {
            p.setProposal (cutAt1k());
            p.applyProposal();
        }

        const int n = i % 3 == 0 ? big.getNumSamples() : i % 3 == 1 ? kBlock : 1;
        for (int c = 0; c < 2; ++c)
            big.copyFrom (c, 0, noise.data(), big.getNumSamples());
        juce::AudioBuffer<float> view (big.getArrayOfWritePointers(), 2, n);

        ScopedAllocationCounter counter;
        p.processBlock (view, midi);
        allocations += counter.count();
    }
    LQ_CHECK (allocations == 0, str ("allocations in processBlock: ", allocations));
    LQ_CHECK (p.getCapture().getCapturedSeconds() > 0.5, "capture was writing during the test");
}

LQ_TEST ("processor: proposal handoff delivers whole proposals across threads")
{
    // Writer publishes sets whose every field encodes k; the reader must never
    // see a mix of two sets, must see k increase, and must end on the last one.
    ProposalHandoff handoff;
    constexpr int count = 20000;
    std::atomic<bool> done { false };

    std::thread writer ([&]
    {
        for (int k = 1; k <= count; ++k)
        {
            ProposalHandoff::Message m;
            m.active = k % 3 != 0;
            for (auto& b : m.bands)
                b = { 100.0 + k, (double) k, 1.0 + k * 1.0e-4, k % 2 == 0, FilterType::bell };
            handoff.publish (m);
        }
        done.store (true);
    });

    int torn = 0, backwards = 0, taken = 0, last = 0;
    const auto check = [&] (const ProposalHandoff::Message& m)
    {
        const auto k = (int) m.bands[0].gainDb;
        bool whole = m.active == (k % 3 != 0);
        for (const auto& b : m.bands)
            whole = whole && b.gainDb == (double) k && b.frequencyHz == 100.0 + k
                    && b.q == 1.0 + k * 1.0e-4 && b.bypassed == (k % 2 == 0);
        torn += whole ? 0 : 1;
        backwards += k > last ? 0 : 1;
        last = k;
        ++taken;
    };

    ProposalHandoff::Message m;
    while (! done.load())
        if (handoff.take (m))
            check (m);
    writer.join();
    if (handoff.take (m))
        check (m);

    ctx.note (str ("messages taken: ", taken, " of ", count));
    LQ_CHECK (taken > 0, "reader received messages");
    LQ_CHECK (torn == 0, str ("torn messages: ", torn));
    LQ_CHECK (backwards == 0, str ("stale or repeated messages: ", backwards));
    LQ_CHECK (last == count, str ("ends on the latest message, got ", last));
    LQ_CHECK (! handoff.take (m), "nothing new after the last one");
}
