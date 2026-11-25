#pragma once

#include "analysis/CaptureBuffer.h"
#include "dsp/AssistEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>

namespace iaq8::assist
{
class AssistClient;
class Session;
} // namespace iaq8::assist

namespace iaq8
{
// Single-writer / single-reader mailbox that carries the latest proposal (or
// its removal) from the message thread to the audio thread. Two slots, an
// atomic generation (the published slot is generation & 1) and an atomic
// "slot being read" marker. The reader never waits and never allocates; the
// writer only waits while the reader is copying the one slot it needs (a few
// hundred bytes). Only the latest message matters: intermediate ones may be
// skipped. See the .cpp for the memory-ordering argument.
class ProposalHandoff
{
public:
    struct Message
    {
        dsp::BandSet bands {};
        bool active = false; // false: clear the preview
    };

    // Writer thread only (the message thread in the plugin).
    void publish (const Message& message) noexcept;

    // Reader thread only (the audio thread). Returns true and fills `out`
    // when a message newer than the last one taken has been published.
    // Wait-free: if the writer publishes during the copy window it returns
    // false and the next call picks up the newer message.
    bool take (Message& out) noexcept;

private:
    std::array<Message, 2> slots {};
    std::atomic<std::uint32_t> generation { 0 }; // 0: nothing published yet
    std::atomic<int> readingSlot { -1 };
    std::uint32_t lastTaken = 0; // reader only
};

// Host adapter. The 40 static parameters (LABQ8 STEREO bank) are the applied
// EQ; a proposal from the assistant only ever reaches the preview path and
// never touches a parameter until applyProposal() writes it.
//
// Threads: processBlock / prepareToPlay are the audio side. Everything else
// below is message-thread only, except getCompensationDb(), setListen(),
// getListenRequest() and getStateLoadCount() (any thread) and
// get/setStateInformation and the ASSISTANT accessors (any non-audio thread;
// the ASSISTANT tree is guarded by a lock that the audio thread never takes).
//
// In-instance state loads (host preset change, A/B compare) may run off the
// message thread, where the proposal and the undo history cannot be touched.
// setStateInformation therefore (1) forces Listen::applied at once (an
// atomic), so the old proposal stops being audible, and (2) bumps a load
// counter. Every message-thread entry point that reads or changes the
// proposal or the undo history first compares that counter with the last one
// it saw and, on a change, clears the proposal (publishing the clear to the
// audio thread) and the undo history. The Session polls the same counter to
// reload the restored conversation.
//
// The processor owns the assistant: an AssistClient (HTTPS transport, server
// URL and access code read from Settings when a request is sent, never at
// construction) and the Session. Construction never touches the network.
class Iaq8AudioProcessor final : public juce::AudioProcessor
{
public:
    // Root tag of the saved state. Stable: changing it breaks saved sessions.
    static constexpr const char* stateType = "IAQ8";
    static constexpr const char* stateVersionProperty = "iaq8StateVersion";
    static constexpr const char* assistantType = "ASSISTANT";
    static constexpr int maxUndoDepth = 16;

    Iaq8AudioProcessor();
    // Tests: the assistant talks through `client` (e.g. a fake transport).
    explicit Iaq8AudioProcessor (std::unique_ptr<assist::AssistClient> client);
    ~Iaq8AudioProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "IAQ8"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // APVTS (40 parameters) with stateVersionProperty and an ASSISTANT child.
    // The pending proposal is never saved.
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return parameters; }

    // The applied set as the 40 parameters currently hold it.
    dsp::BandSet getAppliedBands() const;

    // Preview only: no parameter is written, no host notification.
    void setProposal (const dsp::BandSet& proposal);
    void clearProposal();
    bool hasProposal();
    std::optional<dsp::BandSet> getProposal();
    void setListen (dsp::Listen listen) noexcept;            // any thread
    dsp::Listen getListenRequest() const noexcept;           // any thread

    // Writes the proposal into the 40 parameters (one begin/set/end gesture per
    // parameter that changes), pushes the previous applied set onto the undo
    // history and clears the proposal. False when there is no proposal.
    bool applyProposal();
    // Restores the applied set from before the last apply. False when empty.
    bool undoApply();
    int getUndoDepth();

    // Number of successful setStateInformation calls so far. Any thread.
    std::uint32_t getStateLoadCount() const noexcept { return stateLoads.load (std::memory_order_acquire); }
    // Message thread: clears the proposal and the undo history if a state was
    // loaded since the last call (the entry points above call it themselves).
    void invalidateIfStateLoaded();

    // Opaque conversation state (contents defined by the assistant session):
    // stored and restored as a deep copy, always of type ASSISTANT.
    juce::ValueTree getAssistantState() const;
    void setAssistantState (const juce::ValueTree& state);
    // The same copy plus, read under the same lock, the load counter it
    // belongs to.
    juce::ValueTree getAssistantState (std::uint32_t& loadCount) const;
    // Writes `state` only if no state was loaded after `loadCount` was read
    // (checked under the lock that setStateInformation holds while it stores
    // the loaded conversation and bumps the counter), so a late write of the
    // old conversation never replaces a newly loaded one. False: not written.
    bool setAssistantStateUnlessLoaded (const juce::ValueTree& state, std::uint32_t loadCount);

    // The conversation (message thread).
    assist::Session& getSession() noexcept { return *session; }

    // Input capture for the analysis (written before the EQ on the audio
    // thread; prepared in prepareToPlay, which resets it to idle).
    analysis::CaptureBuffer& getCapture() noexcept { return capture; }

    // Loudness compensation of the preview path (dB). Any thread.
    double getCompensationDb() const noexcept { return engine.getCompensationDb(); }

private:
    struct BandParameters
    {
        std::array<juce::RangedAudioParameter*, 5> params {}; // frequency, gain, q, bypass, type
        std::array<std::atomic<float>*, 5> raw {};
    };

    dsp::BandSet readAppliedBands() const noexcept;
    void pushToEngine() noexcept;
    void writeBands (const dsp::BandSet& bands);

    juce::AudioProcessorValueTreeState parameters;
    std::array<BandParameters, 8> bandParameters;
    dsp::AssistEngine engine;
    analysis::CaptureBuffer capture;

    // Message thread -> audio thread.
    ProposalHandoff handoff;
    std::atomic<int> listenRequest { (int) dsp::Listen::applied };

    // Message thread only.
    std::optional<dsp::BandSet> proposal;
    std::deque<dsp::BandSet> undoHistory;

    juce::CriticalSection assistantLock; // never taken on the audio thread
    juce::ValueTree assistantState { assistantType };
    std::atomic<std::uint32_t> stateLoads { 0 }; // bumped under assistantLock
    std::uint32_t stateLoadsSeen = 0;             // message thread

    // Last members: destroyed first (the session uses the client and
    // everything above; the client's transport is joined in its destructor).
    std::unique_ptr<assist::AssistClient> client;
    std::unique_ptr<assist::Session> session;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Iaq8AudioProcessor)
};
} // namespace iaq8
