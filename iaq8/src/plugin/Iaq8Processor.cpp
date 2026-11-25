#include "plugin/Iaq8Processor.h"

#include "assist/AssistClient.h"
#include "assist/Session.h"
#include "assist/Settings.h"
#include "parameters/Iaq8Parameters.h"
#include "parameters/ParameterIDs.h"
#include "ui/Iaq8Editor.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace iaq8
{
//==============================================================================
// ProposalHandoff
//
// Slot of generation g is g & 1. The writer is the only thread that modifies
// `generation`, so it always knows the current value; it writes the slot of
// g + 1, which is never the published one.
//
// The danger is the other slot: the reader may still be copying it from an
// older publication. The reader announces the slot it is about to copy in
// `readingSlot`, then re-reads `generation`; it copies only if the generation
// has not moved. The writer checks `readingSlot` before writing a slot. All
// four accesses (reader: store readingSlot, load generation; writer: store
// generation, load readingSlot) are seq_cst, so they are in one total order:
//  - If the writer's load of readingSlot comes after the reader's store, the
//    writer sees the slot as busy and waits until the reader releases it
//    (release store of -1, read by the writer's seq_cst load: the copy
//    happens-before the overwrite).
//  - If it comes before, then the writer's previous publication (its
//    generation store, earlier in program order) also comes before the
//    reader's re-check, so the reader sees a newer generation than the one it
//    chose a slot from and abandons the copy without touching the slot.
// The publication itself is a seq_cst store after the slot write, and the
// reader's first load of `generation` is seq_cst (acquire): a reader that sees
// generation g sees the complete slot g & 1.
void ProposalHandoff::publish (const Message& message) noexcept
{
    const auto next = generation.load (std::memory_order_relaxed) + 1; // writer owns it
    const auto slot = (int) (next & 1u);

    while (readingSlot.load (std::memory_order_seq_cst) == slot)
        std::this_thread::yield(); // the reader is copying this slot (a few hundred bytes)

    slots[(size_t) slot] = message;
    generation.store (next, std::memory_order_seq_cst);
}

bool ProposalHandoff::take (Message& out) noexcept
{
    // Two attempts: a failed check means a newer message was just published,
    // which the second attempt (or the next block) picks up. Never waits.
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const auto g = generation.load (std::memory_order_seq_cst);
        if (g == lastTaken)
            return false;

        const auto slot = (int) (g & 1u);
        readingSlot.store (slot, std::memory_order_seq_cst);
        if (generation.load (std::memory_order_seq_cst) != g)
        {
            readingSlot.store (-1, std::memory_order_release);
            continue;
        }

        out = slots[(size_t) slot];
        readingSlot.store (-1, std::memory_order_release);
        lastTaken = g;
        return true;
    }
    return false;
}

//==============================================================================
namespace
{
    enum Field { frequencyField, gainField, qField, bypassField, typeField };

    // Two values closer than this are the same parameter value: it absorbs the
    // float round trip of the log ranges, far below any audible change (and
    // below the 0.01 dB gain grid).
    bool sameValue (float a, float b) noexcept
    {
        return std::abs (a - b) <= 1.0e-5f * std::max (1.0f, std::abs (b));
    }

    // Legal value nearest to `value` (clamped and snapped like the host would).
    float legalValue (const juce::RangedAudioParameter& p, float value)
    {
        return p.convertFrom0to1 (p.convertTo0to1 (value));
    }
} // namespace

Iaq8AudioProcessor::Iaq8AudioProcessor()
    : Iaq8AudioProcessor (std::make_unique<assist::AssistClient> (
          assist::makeHttpsTransport(),
          [] { return assist::Settings::serverUrl(); },   // read when a request is sent
          [] { return assist::Settings::accessCode(); }))
{
}

Iaq8AudioProcessor::Iaq8AudioProcessor (std::unique_ptr<assist::AssistClient> assistClient)
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, stateType, params::createLayout())
{
    static_assert (labq8::params::numBands == std::tuple_size_v<dsp::BandSet>);

    for (size_t b = 0; b < bandParameters.size(); ++b)
    {
        const auto& ids = labq8::params::bandIDs[b];
        const std::array<const char*, 5> fieldIDs { ids.frequency, ids.gain, ids.q, ids.bypass, ids.type };
        for (size_t f = 0; f < fieldIDs.size(); ++f)
        {
            bandParameters[b].params[f] = parameters.getParameter (fieldIDs[f]);
            bandParameters[b].raw[f] = parameters.getRawParameterValue (fieldIDs[f]);
            jassert (bandParameters[b].params[f] != nullptr && bandParameters[b].raw[f] != nullptr);
        }
    }

    client = std::move (assistClient);
    jassert (client != nullptr);
    session = std::make_unique<assist::Session> (*this, *client);
}

Iaq8AudioProcessor::~Iaq8AudioProcessor()
{
    session.reset(); // cancels its requests and joins its analysis worker
    client.reset();
}

bool Iaq8AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
dsp::BandSet Iaq8AudioProcessor::readAppliedBands() const noexcept
{
    // As LABQ8: the 0.01 dB gain grid stores "0.00 dB" as about -5.4e-7 dB;
    // anything that displays as 0.00 dB is exactly 0 so the band can go idle.
    constexpr double neutralGainDb = 0.005;

    dsp::BandSet set {};
    for (size_t b = 0; b < set.size(); ++b)
    {
        const auto& raw = bandParameters[b].raw;
        auto& s = set[b];
        s.frequencyHz = raw[frequencyField]->load (std::memory_order_relaxed);
        s.gainDb = raw[gainField]->load (std::memory_order_relaxed);
        if (std::abs (s.gainDb) < neutralGainDb)
            s.gainDb = 0.0;
        s.q = raw[qField]->load (std::memory_order_relaxed);
        s.bypassed = raw[bypassField]->load (std::memory_order_relaxed) >= 0.5f;
        s.type = labq8::dsp::filterTypeFromIndex ((int) std::lround (raw[typeField]->load (std::memory_order_relaxed)));
    }
    return set;
}

dsp::BandSet Iaq8AudioProcessor::getAppliedBands() const
{
    return readAppliedBands();
}

void Iaq8AudioProcessor::pushToEngine() noexcept
{
    // The handoff is read first: applyProposal() writes the parameters before
    // it publishes the clear, so a block that sees the clear also reads the
    // new parameter values (the handoff's acquire orders the loads below).
    ProposalHandoff::Message message;
    const bool hasMessage = handoff.take (message);

    // Re-sending an unchanged applied set is harmless (no ramp restarts).
    engine.setApplied (readAppliedBands());

    if (hasMessage)
    {
        if (message.active)
            engine.setPreview (message.bands); // identical set: no-op
        else
            engine.clearPreview();
    }

    engine.setListen (listenRequest.load (std::memory_order_relaxed) == (int) dsp::Listen::preview
                          ? dsp::Listen::preview
                          : dsp::Listen::applied);
}

void Iaq8AudioProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const auto channels = juce::jlimit (1, dsp::AssistEngine::maxChannels,
                                        juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels()));

    // Targets first so prepare() snaps both paths to the current host values
    // (no ramp from defaults when playback starts, as in LABQ8). The engine
    // keeps its applied set, preview and listen state across a re-prepare.
    pushToEngine();
    engine.prepare (sampleRate, channels, juce::jmax (1, maximumExpectedSamplesPerBlock));
    // The prepared engine then receives the current set again (unchanged:
    // no ramp, no meter restart).
    pushToEngine();

    capture.prepare (sampleRate, channels); // allocates; resets to idle
    inputMeter.reset();
    outputMeter.reset();
}

void Iaq8AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals; // K-weighting and EQ state on x86_64

    const auto numIn = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();
    for (auto ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    pushToEngine();

    const auto numChannels = juce::jmin (numIn, buffer.getNumChannels());
    // Input before the EQ (no-op unless armed).
    capture.write (buffer.getArrayOfReadPointers(), numChannels, buffer.getNumSamples());
    inputMeter.measure (buffer.getArrayOfReadPointers(), numChannels, buffer.getNumSamples());
    engine.process (buffer.getArrayOfWritePointers(), numChannels, buffer.getNumSamples());
    outputMeter.measure (buffer.getArrayOfReadPointers(), numChannels, buffer.getNumSamples());
}

void Iaq8AudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const auto numChannels = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    inputMeter.measure (buffer.getArrayOfReadPointers(), numChannels, buffer.getNumSamples());
    AudioProcessor::processBlockBypassed (buffer, midi);
    outputMeter.measure (buffer.getArrayOfReadPointers(), numChannels, buffer.getNumSamples());
}

juce::AudioProcessorEditor* Iaq8AudioProcessor::createEditor()
{
    return new ui::Iaq8Editor (*this);
}

//==============================================================================
void Iaq8AudioProcessor::invalidateIfStateLoaded()
{
    JUCE_ASSERT_MESSAGE_THREAD
    const auto loads = getStateLoadCount();
    if (loads == stateLoadsSeen)
        return;

    stateLoadsSeen = loads;
    undoHistory.clear();
    proposal.reset();
    handoff.publish ({ {}, false }); // always: the audio side may hold an older proposal
}

bool Iaq8AudioProcessor::hasProposal()
{
    invalidateIfStateLoaded();
    return proposal.has_value();
}

std::optional<dsp::BandSet> Iaq8AudioProcessor::getProposal()
{
    invalidateIfStateLoaded();
    return proposal;
}

int Iaq8AudioProcessor::getUndoDepth()
{
    invalidateIfStateLoaded();
    return (int) undoHistory.size();
}

dsp::Listen Iaq8AudioProcessor::getListenRequest() const noexcept
{
    return listenRequest.load (std::memory_order_relaxed) == (int) dsp::Listen::preview ? dsp::Listen::preview
                                                                                       : dsp::Listen::applied;
}

void Iaq8AudioProcessor::setProposal (const dsp::BandSet& newProposal)
{
    JUCE_ASSERT_MESSAGE_THREAD
    invalidateIfStateLoaded();
    proposal = newProposal;
    handoff.publish ({ newProposal, true });
}

void Iaq8AudioProcessor::clearProposal()
{
    JUCE_ASSERT_MESSAGE_THREAD
    invalidateIfStateLoaded();
    proposal.reset();
    handoff.publish ({ {}, false });
}

void Iaq8AudioProcessor::setListen (dsp::Listen listen) noexcept
{
    listenRequest.store ((int) listen, std::memory_order_relaxed);
}

void Iaq8AudioProcessor::writeBands (const dsp::BandSet& bands)
{
    for (size_t b = 0; b < bands.size(); ++b)
    {
        const auto& s = bands[b];
        const std::array<float, 5> values { (float) s.frequencyHz, (float) s.gainDb, (float) s.q,
                                            s.bypassed ? 1.0f : 0.0f, (float) static_cast<int> (s.type) };
        for (size_t f = 0; f < values.size(); ++f)
        {
            auto& p = *bandParameters[b].params[f];
            const auto target = legalValue (p, values[f]);
            if (sameValue (target, bandParameters[b].raw[f]->load (std::memory_order_relaxed)))
                continue; // unchanged: no gesture, no automation point

            p.beginChangeGesture();
            p.setValueNotifyingHost (p.convertTo0to1 (target));
            p.endChangeGesture();
        }
    }
}

bool Iaq8AudioProcessor::applyProposal()
{
    JUCE_ASSERT_MESSAGE_THREAD
    invalidateIfStateLoaded();
    if (! proposal.has_value())
        return false;

    undoHistory.push_back (getAppliedBands());
    while ((int) undoHistory.size() > maxUndoDepth)
        undoHistory.pop_front();

    // Parameters first, then the clear (see pushToEngine): the preview fades
    // to an applied path that already holds the proposal.
    writeBands (*proposal);
    clearProposal();
    return true;
}

bool Iaq8AudioProcessor::undoApply()
{
    JUCE_ASSERT_MESSAGE_THREAD
    invalidateIfStateLoaded();
    if (undoHistory.empty())
        return false;

    const auto previous = undoHistory.back();
    undoHistory.pop_back();
    writeBands (previous);
    return true;
}

//==============================================================================
juce::ValueTree Iaq8AudioProcessor::getAssistantState() const
{
    const juce::ScopedLock lock (assistantLock);
    return assistantState.createCopy();
}

void Iaq8AudioProcessor::setAssistantState (const juce::ValueTree& state)
{
    // Always an ASSISTANT tree holding a deep copy of what was given.
    juce::ValueTree copy (assistantType);
    if (state.isValid())
        copy.copyPropertiesAndChildrenFrom (state, nullptr);

    const juce::ScopedLock lock (assistantLock);
    assistantState = copy;
}

juce::ValueTree Iaq8AudioProcessor::getAssistantState (std::uint32_t& loadCount) const
{
    const juce::ScopedLock lock (assistantLock);
    loadCount = stateLoads.load (std::memory_order_relaxed); // only changed under this lock
    return assistantState.createCopy();
}

bool Iaq8AudioProcessor::setAssistantStateUnlessLoaded (const juce::ValueTree& state, std::uint32_t loadCount)
{
    juce::ValueTree copy (assistantType);
    if (state.isValid())
        copy.copyPropertiesAndChildrenFrom (state, nullptr);

    const juce::ScopedLock lock (assistantLock);
    if (stateLoads.load (std::memory_order_relaxed) != loadCount)
        return false;
    assistantState = copy;
    return true;
}

void Iaq8AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (stateVersionProperty, params::stateVersion, nullptr);
    state.removeChild (state.getChildWithName (assistantType), nullptr);
    state.appendChild (getAssistantState(), nullptr); // never the proposal

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void Iaq8AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return; // unknown or corrupt data: keep current state

    auto tree = juce::ValueTree::fromXml (*xml);

    // Version 1 is the only one so far: nothing to migrate. The property is
    // kept on the tree and rewritten on the next save.
    const auto assistant = tree.getChildWithName (assistantType);
    tree.removeChild (assistant, nullptr);

    // Silence a pending proposal at once (this may be any thread); the
    // message thread drops it, and the undo history, when it sees the counter.
    listenRequest.store ((int) dsp::Listen::applied, std::memory_order_relaxed);

    parameters.replaceState (tree);

    juce::ValueTree copy (assistantType); // missing child: empty conversation
    if (assistant.isValid())
        copy.copyPropertiesAndChildrenFrom (assistant, nullptr);

    const juce::ScopedLock lock (assistantLock);
    assistantState = copy;
    stateLoads.fetch_add (1, std::memory_order_release); // with the conversation, under the lock
}
} // namespace iaq8

// JUCE plug-in client entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new iaq8::Iaq8AudioProcessor();
}
