#include "parameters/Iaq8Parameters.h"

#include "dsp/BandLimits.h"
#include "dsp/FilterTypes.h"
#include "parameters/ParameterIDs.h"

#include <cmath>

// The factories below are copies of LABQ8's (src/parameters/ParameterLayout.cpp),
// which live in an anonymous namespace and cannot be reused. The test
// "iaq8 params: 40 parameters with LABQ8 STEREO ids, ranges and defaults"
// is what guarantees parity: if LABQ8 changes, that test fails.

namespace iaq8::params
{
namespace
{
    namespace limits = labq8::dsp::limits;
    using labq8::params::bandIDs;
    using labq8::params::numBands;

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

    juce::String bandPrefix (int band)
    {
        return "Band " + juce::String (band + 1) + " ";
    }

    std::unique_ptr<juce::RangedAudioParameter> makeFrequency (const char* id, int hint, const juce::String& prefix, int band)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, hint },
            prefix + "Frequency",
            makeLogRange (limits::minFrequencyHz, limits::maxFrequencyHz),
            static_cast<float> (labq8::params::defaultFrequencyHz[(size_t) band]),
            juce::AudioParameterFloatAttributes()
                .withLabel ("Hz")
                .withStringFromValueFunction (frequencyToText)
                .withValueFromStringFunction (textToFrequency));
    }

    std::unique_ptr<juce::RangedAudioParameter> makeGain (const char* id, int hint, const juce::String& prefix)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, hint },
            prefix + "Gain",
            juce::NormalisableRange<float> (static_cast<float> (limits::minGainDb),
                                            static_cast<float> (limits::maxGainDb),
                                            0.01f),
            static_cast<float> (limits::defaultGainDb),
            juce::AudioParameterFloatAttributes()
                .withLabel ("dB")
                .withStringFromValueFunction (gainToText));
    }

    std::unique_ptr<juce::RangedAudioParameter> makeQ (const char* id, int hint, const juce::String& prefix)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, hint },
            prefix + "Q",
            makeLogRange (limits::minQ, limits::maxQ),
            static_cast<float> (limits::defaultQ),
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float q, int) { return juce::String (q, 2); }));
    }

    std::unique_ptr<juce::RangedAudioParameter> makeBypass (const char* id, int hint, const juce::String& prefix)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, hint }, prefix + "Bypass", false);
    }

    std::unique_ptr<juce::RangedAudioParameter> makeType (const char* id, int hint, const juce::String& prefix)
    {
        static_assert (labq8::params::filterTypeNames.size() == labq8::dsp::numFilterTypes);
        juce::StringArray typeNames;
        for (const auto* name : labq8::params::filterTypeNames)
            typeNames.add (name);

        return std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id, hint },
            prefix + "Type",
            typeNames,
            static_cast<int> (labq8::dsp::FilterType::bell));
    }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Same host order as LABQ8: band 1 (E0 hint) then bands 2..8 (E1a hint),
    // each freq, gain, q, bypass; then the 8 types (E1b hint).
    for (int band = 0; band < numBands; ++band)
    {
        const auto& ids = bandIDs[(size_t) band];
        const auto hint = band == 0 ? labq8::params::versionHintE0 : labq8::params::versionHintE1a;
        const auto prefix = bandPrefix (band);

        layout.add (makeFrequency (ids.frequency, hint, prefix, band),
                    makeGain (ids.gain, hint, prefix),
                    makeQ (ids.q, hint, prefix),
                    makeBypass (ids.bypass, hint, prefix));
    }

    for (int band = 0; band < numBands; ++band)
        layout.add (makeType (bandIDs[(size_t) band].type, labq8::params::versionHintE1b, bandPrefix (band)));

    return layout;
}
} // namespace iaq8::params
