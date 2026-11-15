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

// One parameter of each kind. The STEREO bank (E0..E2c.1) and the E4a MID /
// SIDE banks are built with the same functions, so every bank has the same
// contract; only the ID, the version hint and the name prefix differ. The
// frozen v4 contract (tests/fixtures/contract_v4_ui2c.txt) checks that the
// STEREO bank is still exactly what 152d46c exposed.
namespace
{
    namespace dl = dsp::dynamics::limits;

    std::unique_ptr<juce::RangedAudioParameter> makeFrequency (const char* id, int hint, const juce::String& prefix, int band)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, hint },
            prefix + "Frequency",
            makeLogRange (limits::minFrequencyHz, limits::maxFrequencyHz),
            static_cast<float> (defaultFrequencyHz[(size_t) band]),
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
        static_assert (filterTypeNames.size() == dsp::numFilterTypes);
        juce::StringArray typeNames;
        for (const auto* name : filterTypeNames)
            typeNames.add (name);

        return std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id, hint },
            prefix + "Type",
            typeNames,
            static_cast<int> (dsp::FilterType::bell));
    }

    // Dynamics: the validated DSP ranges (dsp::dynamics::limits): threshold
    // and range linear on a 0.1 dB grid, attack and release logarithmic like
    // frequency and Q, on 0.01 ms and 0.1 ms grids.
    std::unique_ptr<juce::RangedAudioParameter> makeDynamicEnable (const char* id, int hint, const juce::String& prefix)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, hint }, prefix + "Dynamic EQ", false);
    }

    std::unique_ptr<juce::RangedAudioParameter> makeThreshold (const char* id, int hint, const juce::String& prefix)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, hint },
            prefix + "Threshold",
            juce::NormalisableRange<float> (static_cast<float> (dl::minThresholdDb), static_cast<float> (dl::maxThresholdDb), 0.1f),
            static_cast<float> (defaultThresholdDb),
            juce::AudioParameterFloatAttributes().withLabel ("dBFS").withStringFromValueFunction (decibelsToText));
    }

    std::unique_ptr<juce::RangedAudioParameter> makeRange (const char* id, int hint, const juce::String& prefix)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, hint },
            prefix + "Range",
            juce::NormalisableRange<float> (static_cast<float> (dl::minRangeDb), static_cast<float> (dl::maxRangeDb), 0.1f),
            static_cast<float> (defaultRangeDb),
            juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (decibelsToText));
    }

    std::unique_ptr<juce::RangedAudioParameter> makeAttack (const char* id, int hint, const juce::String& prefix)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, hint },
            prefix + "Attack",
            makeSteppedLogRange (dl::minAttackMs, dl::maxAttackMs, 0.01f),
            static_cast<float> (defaultAttackMs),
            juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (millisecondsToText));
    }

    std::unique_ptr<juce::RangedAudioParameter> makeRelease (const char* id, int hint, const juce::String& prefix)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, hint },
            prefix + "Release",
            makeSteppedLogRange (dl::minReleaseMs, dl::maxReleaseMs, 0.1f),
            static_cast<float> (defaultReleaseMs),
            juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (millisecondsToText));
    }

    juce::String bandPrefix (const char* bankPrefix, int band)
    {
        return juce::String (bankPrefix) + "Band " + juce::String (band + 1) + " ";
    }

    // E4a: a MID or SIDE bank, band by band (10 parameters per band, the
    // STEREO bank's per-band order: static, type, dynamics).
    void addBank (juce::AudioProcessorValueTreeState::ParameterLayout& layout, Bank bank)
    {
        const auto ids = bankOf (bank);
        for (int band = 0; band < numBands; ++band)
        {
            const auto& b = ids.bands[(size_t) band];
            const auto& d = ids.dynamics[(size_t) band];
            const auto prefix = bandPrefix (ids.namePrefix, band);

            layout.add (makeFrequency (b.frequency, versionHintE4, prefix, band),
                        makeGain (b.gain, versionHintE4, prefix),
                        makeQ (b.q, versionHintE4, prefix),
                        makeBypass (b.bypass, versionHintE4, prefix),
                        makeType (b.type, versionHintE4, prefix),
                        makeDynamicEnable (d.enabled, versionHintE4, prefix),
                        makeThreshold (d.threshold, versionHintE4, prefix),
                        makeRange (d.range, versionHintE4, prefix),
                        makeAttack (d.attack, versionHintE4, prefix),
                        makeRelease (d.release, versionHintE4, prefix));
        }
    }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto stereo = bankOf (Bank::stereo);

    // Order matters to hosts: band 1 first, in its E0 order, then bands 2..8.
    for (int band = 0; band < numBands; ++band)
    {
        const auto& ids = stereo.bands[(size_t) band];
        const auto hint = band == 0 ? versionHintE0 : versionHintE1a;
        const auto prefix = bandPrefix (stereo.namePrefix, band);

        layout.add (makeFrequency (ids.frequency, hint, prefix, band),
                    makeGain (ids.gain, hint, prefix),
                    makeQ (ids.q, hint, prefix),
                    makeBypass (ids.bypass, hint, prefix));
    }

    // E1b: one type per band, appended after all E0/E1a parameters.
    for (int band = 0; band < numBands; ++band)
        layout.add (makeType (stereo.bands[(size_t) band].type, versionHintE1b, bandPrefix (stereo.namePrefix, band)));

    // E2c.1: dynamics, appended after the types.
    for (int band = 0; band < numBands; ++band)
    {
        const auto& ids = stereo.dynamics[(size_t) band];
        const auto prefix = bandPrefix (stereo.namePrefix, band);

        layout.add (makeDynamicEnable (ids.enabled, versionHintE2c, prefix),
                    makeThreshold (ids.threshold, versionHintE2c, prefix),
                    makeRange (ids.range, versionHintE2c, prefix),
                    makeAttack (ids.attack, versionHintE2c, prefix),
                    makeRelease (ids.release, versionHintE2c, prefix));
    }

    // E4a: the processing mode (index 80), then the MID (81..160) and SIDE
    // (161..240) banks. Appended only: indices 0..79 are untouched.
    juce::StringArray modeNames;
    for (const auto* name : procModeNames)
        modeNames.add (name);
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { procModeID, versionHintE4 }, "Stereo Mode", modeNames, static_cast<int> (ProcMode::stereo)));

    addBank (layout, Bank::mid);
    addBank (layout, Bank::side);

    return layout;
}
} // namespace labq8::params
