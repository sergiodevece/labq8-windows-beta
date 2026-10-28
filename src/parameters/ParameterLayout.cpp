#include "parameters/ParameterLayout.h"

#include "dsp/BandLimits.h"
#include "parameters/ParameterIDs.h"

#include <cmath>

namespace labq8::params
{
namespace
{
    namespace limits = dsp::limits;

    // Logarithmic 0..1 mapping: equal knob travel per octave (frequency) or
    // per doubling of Q, which is how both are perceived.
    juce::NormalisableRange<float> makeLogRange (double minValue, double maxValue)
    {
        const auto lo = static_cast<float> (minValue);
        const auto hi = static_cast<float> (maxValue);

        return { lo, hi,
                 [] (float start, float end, float normalised)
                 { return start * std::pow (end / start, normalised); },
                 [] (float start, float end, float value)
                 { return std::log (value / start) / std::log (end / start); },
                 [] (float start, float end, float value)
                 { return juce::jlimit (start, end, value); } };
    }

    juce::String frequencyToText (float hz, int)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz"
                             : juce::String (hz, 1) + " Hz";
    }

    float textToFrequency (const juce::String& text)
    {
        const auto value = text.retainCharacters ("0123456789.-").getFloatValue();
        return text.containsIgnoreCase ("k") ? value * 1000.0f : value;
    }

    juce::String gainToText (float db, int)
    {
        return (db > 0.0f ? "+" : "") + juce::String (db, 2) + " dB";
    }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Order matters to hosts: band 1 first, in its E0 order, then bands 2..8.
    for (int band = 0; band < numBands; ++band)
    {
        const auto& ids = bandIDs[(size_t) band];
        const auto hint = band == 0 ? versionHintE0 : versionHintE1a;
        const auto prefix = "Band " + juce::String (band + 1) + " ";

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.frequency, hint },
            prefix + "Frequency",
            makeLogRange (limits::minFrequencyHz, limits::maxFrequencyHz),
            static_cast<float> (defaultFrequencyHz[(size_t) band]),
            juce::AudioParameterFloatAttributes()
                .withLabel ("Hz")
                .withStringFromValueFunction (frequencyToText)
                .withValueFromStringFunction (textToFrequency)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.gain, hint },
            prefix + "Gain",
            juce::NormalisableRange<float> (static_cast<float> (limits::minGainDb),
                                            static_cast<float> (limits::maxGainDb),
                                            0.01f),
            static_cast<float> (limits::defaultGainDb),
            juce::AudioParameterFloatAttributes()
                .withLabel ("dB")
                .withStringFromValueFunction (gainToText)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.q, hint },
            prefix + "Q",
            makeLogRange (limits::minQ, limits::maxQ),
            static_cast<float> (limits::defaultQ),
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float q, int) { return juce::String (q, 2); })));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ids.bypass, hint },
            prefix + "Bypass",
            false));
    }

    return layout;
}
} // namespace labq8::params
