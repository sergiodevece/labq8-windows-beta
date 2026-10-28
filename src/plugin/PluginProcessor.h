#pragma once

#include "dsp/BellBand.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace labq8
{
// Plugin Host Adapter: owns the parameter tree and state, translates host
// parameters into DSP settings, and drives the EQ core. No DSP lives here.
class LabQ8AudioProcessor final : public juce::AudioProcessor
{
public:
    // Root tag of the saved state. Stable: changing it breaks saved sessions.
    static constexpr const char* stateType = "LABQ8";
    static constexpr const char* stateVersionProperty = "labq8StateVersion";
    static constexpr int stateVersion = 1;

    LabQ8AudioProcessor();

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "LABQ8"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return parameters; }

private:
    dsp::BellSettings readBand1Settings() const noexcept;

    juce::AudioProcessorValueTreeState parameters;

    std::atomic<float>& band1Frequency;
    std::atomic<float>& band1Gain;
    std::atomic<float>& band1Q;
    std::atomic<float>& band1Bypass;

    dsp::BellBand band1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabQ8AudioProcessor)
};
} // namespace labq8
