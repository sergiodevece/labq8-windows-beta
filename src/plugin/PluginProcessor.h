#pragma once

#include "dsp/EqCore.h"
#include "parameters/StateMigration.h"
#include "plugin/LevelMeter.h"
#include "plugin/SpectrumTap.h"

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
    // Host bypass: the default pass-through, metered (IN = OUT = the
    // unprocessed signal).
    void processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlockBypassed;

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

    // Band shown by the editor's shared panel (message thread only). Kept
    // here so it survives closing and reopening the editor; not a parameter,
    // not saved, no effect on audio.
    int getUiSelectedBand() const noexcept { return uiSelectedBand; }
    void setUiSelectedBand (int band) noexcept { uiSelectedBand = band; }
    // UI-2b: the screen's view tab and the analyzer's source, the same way
    // (per instance, in memory only; the editor gives them their meaning).
    int getUiView() const noexcept { return uiView; }
    void setUiView (int view) noexcept { uiView = view; }
    int getUiAnalyzerSource() const noexcept { return uiAnalyzerSource; }
    void setUiAnalyzerSource (int analyzerSource) noexcept { uiAnalyzerSource = analyzerSource; }

    // UI-2a: sample peak of the input (before the EQ) and of the output
    // (what the host receives). Written by the audio thread, taken by the
    // editor; they never change the audio.
    LevelMeter& getInputMeter() noexcept { return inputMeter; }
    LevelMeter& getOutputMeter() noexcept { return outputMeter; }

    // UI-2b: the audio before the EQ (PRE) and after it (POST, what the host
    // receives), for the editor's spectrum analyzer. Copied only while the
    // analyzer is shown; they never change the audio.
    SpectrumTap& getPreTap() noexcept { return preTap; }
    SpectrumTap& getPostTap() noexcept { return postTap; }

private:
    struct BandParameters
    {
        std::atomic<float>* frequency = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* bypass = nullptr;
        std::atomic<float>* type = nullptr;
    };

    struct DynamicParameters
    {
        std::atomic<float>* enabled = nullptr;
        std::atomic<float>* threshold = nullptr;
        std::atomic<float>* range = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* release = nullptr;
    };

    dsp::BandSettings readBandSettings (const BandParameters& band) const noexcept;
    static dsp::DynamicSettings readDynamicSettings (const DynamicParameters& dynamics) noexcept;
    void pushParametersToCore() noexcept;

    juce::AudioProcessorValueTreeState parameters;
    std::array<BandParameters, dsp::EqCore::numBands> bandParameters;
    std::array<DynamicParameters, dsp::EqCore::numBands> dynamicParameters;
    dsp::EqCore eq;
    int uiSelectedBand = 0;
    int uiView = 3;           // ALL
    int uiAnalyzerSource = 1; // POST
    LevelMeter inputMeter, outputMeter;
    SpectrumTap preTap, postTap;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabQ8AudioProcessor)
};
} // namespace labq8
