#pragma once

#include "dsp/EqCore.h"
#include "parameters/StateMigration.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace labq8
{
// Plugin Host Adapter: owns the parameter tree and state, translates host
// parameters into DSP settings, and drives the EQ core. No DSP lives here.
class LabQ8AudioProcessor final : public juce::AudioProcessor
{
public:
    // Root tag of the saved state. Stable: changing it breaks saved sessions.
    static constexpr const char* stateType = "LABQ8";
    static constexpr const char* stateVersionProperty = params::stateVersionProperty;
    static constexpr int stateVersion = params::currentStateVersion;

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
    struct BandParameters
    {
        std::atomic<float>* frequency = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* bypass = nullptr;
        std::atomic<float>* type = nullptr;
    };

    dsp::BandSettings readBandSettings (const BandParameters& band) const noexcept;
    void pushParametersToCore() noexcept;

    juce::AudioProcessorValueTreeState parameters;
    std::array<BandParameters, dsp::EqCore::numBands> bandParameters;
    dsp::EqCore eq;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabQ8AudioProcessor)
};
} // namespace labq8
