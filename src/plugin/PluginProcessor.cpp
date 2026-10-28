#include "plugin/PluginProcessor.h"

#include "parameters/ParameterIDs.h"
#include "parameters/ParameterLayout.h"

namespace labq8
{
namespace
{
    std::atomic<float>& rawParameter (juce::AudioProcessorValueTreeState& state, const char* parameterID)
    {
        auto* value = state.getRawParameterValue (parameterID);
        jassert (value != nullptr); // every ID in ParameterIDs.h must exist in the layout
        return *value;
    }
} // namespace

LabQ8AudioProcessor::LabQ8AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, stateType, params::createParameterLayout()),
      band1Frequency (rawParameter (parameters, params::id::band1Frequency)),
      band1Gain (rawParameter (parameters, params::id::band1Gain)),
      band1Q (rawParameter (parameters, params::id::band1Q)),
      band1Bypass (rawParameter (parameters, params::id::band1Bypass))
{
}

bool LabQ8AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

dsp::BellSettings LabQ8AudioProcessor::readBand1Settings() const noexcept
{
    dsp::BellSettings s;
    s.frequencyHz = band1Frequency.load (std::memory_order_relaxed);
    s.gainDb      = band1Gain.load (std::memory_order_relaxed);
    s.q           = band1Q.load (std::memory_order_relaxed);
    s.bypassed    = band1Bypass.load (std::memory_order_relaxed) >= 0.5f;
    return s;
}

void LabQ8AudioProcessor::prepareToPlay (double sampleRate, int)
{
    // Targets first so prepare() snaps to the current host values (no ramp
    // from defaults when playback starts).
    band1.setTargets (readBand1Settings());
    band1.prepare (sampleRate, juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels()));
}

void LabQ8AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    for (auto ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    band1.setTargets (readBand1Settings());
    band1.process (buffer.getArrayOfWritePointers(),
                   juce::jmin (numIn, buffer.getNumChannels()),
                   buffer.getNumSamples());
}

juce::AudioProcessorEditor* LabQ8AudioProcessor::createEditor()
{
    // E0: development-only editor that lists the parameters.
    return new juce::GenericAudioProcessorEditor (*this);
}

void LabQ8AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (stateVersionProperty, stateVersion, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void LabQ8AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return; // unknown or corrupt data: keep current state

    // Future versions migrate older states here, keyed on stateVersionProperty.
    parameters.replaceState (juce::ValueTree::fromXml (*xml));
}
} // namespace labq8

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new labq8::LabQ8AudioProcessor();
}
