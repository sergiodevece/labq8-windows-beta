#include "plugin/PluginProcessor.h"

#include "parameters/ParameterIDs.h"
#include "parameters/ParameterLayout.h"

#include <cmath>

namespace labq8
{
namespace
{
    std::atomic<float>* rawParameter (juce::AudioProcessorValueTreeState& state, const char* parameterID)
    {
        auto* value = state.getRawParameterValue (parameterID);
        jassert (value != nullptr); // every ID in ParameterIDs.h must exist in the layout
        return value;
    }
} // namespace

static_assert (params::numBands == dsp::EqCore::numBands);

LabQ8AudioProcessor::LabQ8AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, stateType, params::createParameterLayout())
{
    for (size_t band = 0; band < bandParameters.size(); ++band)
    {
        const auto& ids = params::bandIDs[band];
        bandParameters[band] = { rawParameter (parameters, ids.frequency),
                                 rawParameter (parameters, ids.gain),
                                 rawParameter (parameters, ids.q),
                                 rawParameter (parameters, ids.bypass) };
    }
}

bool LabQ8AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

dsp::BellSettings LabQ8AudioProcessor::readBandSettings (const BandParameters& band) const noexcept
{
    // The host gain parameter snaps to a 0.01 dB grid in float, so "0.00 dB"
    // arrives as about -5.4e-7 dB. Anything that displays as 0.00 dB is made
    // exactly 0 so the band can go idle (bit-exact, no CPU).
    constexpr double neutralGainDb = 0.005;

    dsp::BellSettings s;
    s.frequencyHz = band.frequency->load (std::memory_order_relaxed);
    s.gainDb      = band.gain->load (std::memory_order_relaxed);
    if (std::abs (s.gainDb) < neutralGainDb)
        s.gainDb = 0.0;

    s.q           = band.q->load (std::memory_order_relaxed);
    s.bypassed    = band.bypass->load (std::memory_order_relaxed) >= 0.5f;
    return s;
}

void LabQ8AudioProcessor::pushParametersToCore() noexcept
{
    for (size_t band = 0; band < bandParameters.size(); ++band)
        eq.setBandTargets (static_cast<int> (band), readBandSettings (bandParameters[band]));
}

void LabQ8AudioProcessor::prepareToPlay (double sampleRate, int)
{
    // Targets first so prepare() snaps to the current host values (no ramp
    // from defaults when playback starts).
    pushParametersToCore();
    eq.prepare (sampleRate, juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels()));
}

void LabQ8AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    for (auto ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    pushParametersToCore();
    eq.process (buffer.getArrayOfWritePointers(),
                juce::jmin (numIn, buffer.getNumChannels()),
                buffer.getNumSamples());
}

juce::AudioProcessorEditor* LabQ8AudioProcessor::createEditor()
{
    // Development-only editor that lists the parameters.
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

    auto tree = juce::ValueTree::fromXml (*xml);
    params::migrateState (tree, parameters);
    parameters.replaceState (tree);
}
} // namespace labq8

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new labq8::LabQ8AudioProcessor();
}
