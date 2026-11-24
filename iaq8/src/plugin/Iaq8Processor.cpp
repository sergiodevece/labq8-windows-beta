#include "plugin/Iaq8Processor.h"

#include "plugin/DebugEditor.h"

namespace iaq8
{
Iaq8AudioProcessor::Iaq8AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void Iaq8AudioProcessor::prepareToPlay (double, int) {}

bool Iaq8AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void Iaq8AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Pass-through: mono->mono and stereo->stereo only, so there are no
    // extra output channels to clear.
    juce::ignoreUnused (buffer);
}

juce::AudioProcessorEditor* Iaq8AudioProcessor::createEditor()
{
    return new DebugEditor (*this);
}
} // namespace iaq8

// JUCE plug-in client entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new iaq8::Iaq8AudioProcessor();
}
