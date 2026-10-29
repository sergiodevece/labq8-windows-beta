#include "parameters/ParameterLayout.h"

#include "dsp/BandLimits.h"
#include "dsp/DynamicSection.h"
#include "dsp/FilterTypes.h"
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

    // Logarithmic mapping snapped to a fixed grid (E2c.1 attack / release).
    // Without the grid a value moves by an ulp at every save / load (the log
    // mapping is not an exact inverse in float); on the grid the round-trip
    // is a fixed point, so re-saved states are byte-identical.
    juce::NormalisableRange<float> makeSteppedLogRange (double minValue, double maxValue, float step)
    {
        return { static_cast<float> (minValue), static_cast<float> (maxValue),
                 [] (float start, float end, float normalised)
                 { return start * std::pow (end / start, normalised); },
                 [] (float start, float end, float value)
                 { return std::log (value / start) / std::log (end / start); },
                 [step] (float start, float end, float value)
                 { return juce::jlimit (start, end, std::round (value / step) * step); } };
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

    juce::String decibelsToText (float db, int)
    {
        return juce::String (db, 1) + " dB";
    }

    juce::String millisecondsToText (float ms, int)
    {
        return juce::String (ms, ms < 10.0f ? 2 : 1) + " ms";
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

    // E1b: one type per band, appended after all E0/E1a parameters.
    static_assert (filterTypeNames.size() == dsp::numFilterTypes);
    juce::StringArray typeNames;
    for (const auto* name : filterTypeNames)
        typeNames.add (name);

    for (int band = 0; band < numBands; ++band)
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { bandIDs[(size_t) band].type, versionHintE1b },
            "Band " + juce::String (band + 1) + " Type",
            typeNames,
            static_cast<int> (dsp::FilterType::bell)));

    // E2c.1: dynamics, appended after the types. Ranges are the validated DSP
    // ranges (dsp::dynamics::limits): threshold and range linear on a 0.1 dB
    // grid, attack and release logarithmic like frequency and Q, on 0.01 ms
    // and 0.1 ms grids.
    namespace dl = dsp::dynamics::limits;
    for (int band = 0; band < numBands; ++band)
    {
        const auto& ids = dynamicIDs[(size_t) band];
        const auto prefix = "Band " + juce::String (band + 1) + " ";

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ids.enabled, versionHintE2c },
            prefix + "Dynamic EQ",
            false));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.threshold, versionHintE2c },
            prefix + "Threshold",
            juce::NormalisableRange<float> (static_cast<float> (dl::minThresholdDb), static_cast<float> (dl::maxThresholdDb), 0.1f),
            static_cast<float> (defaultThresholdDb),
            juce::AudioParameterFloatAttributes().withLabel ("dBFS").withStringFromValueFunction (decibelsToText)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.range, versionHintE2c },
            prefix + "Range",
            juce::NormalisableRange<float> (static_cast<float> (dl::minRangeDb), static_cast<float> (dl::maxRangeDb), 0.1f),
            static_cast<float> (defaultRangeDb),
            juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (decibelsToText)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.attack, versionHintE2c },
            prefix + "Attack",
            makeSteppedLogRange (dl::minAttackMs, dl::maxAttackMs, 0.01f),
            static_cast<float> (defaultAttackMs),
            juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (millisecondsToText)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.release, versionHintE2c },
            prefix + "Release",
            makeSteppedLogRange (dl::minReleaseMs, dl::maxReleaseMs, 0.1f),
            static_cast<float> (defaultReleaseMs),
            juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (millisecondsToText)));
    }

    return layout;
}
} // namespace labq8::params
