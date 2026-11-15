#pragma once

// E4a: a textual description of the public parameter contract of a
// LabQ8AudioProcessor, one line per host parameter, in host order. Header
// only and written against plain juce::AudioProcessor so the same code can be
// compiled with older plugin sources (tests/fixtures/ui2c_state_dump.cpp.txt
// builds it against 152d46c to freeze the v4 contract).
//
// Each line holds everything a host or a saved session depends on: index,
// ID, name, class, version hint, automatability, the derived VST3 and AU
// parameter IDs (JUCE: String::hashCode, Studio One compatible mask, the
// default), default, range end points and grid, the normalised -> value
// mapping at five points, the number of steps, label, choices and the
// display text at three points. Numbers are printed with 6 significant
// digits, and |v| < 1e-5 as 0, so the description is identical on arm64 and
// x86_64 (FMA may move a float by an ulp).

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace labq8::contract
{
// JUCE_USE_STUDIO_ONE_COMPATIBLE_PARAMETERS (default 1): VST3
// convertJuceParameterId() and AU generateAUParameterID() clear the top bit.
inline std::uint32_t hostParameterId (const juce::String& paramID)
{
    return static_cast<std::uint32_t> (paramID.hashCode()) & 0x7fffffffu;
}

inline std::string number (double v)
{
    // JUCE's 0.01 dB grid lands "0 dB" on -5.4e-7 with FMA (arm64) and on 0
    // without it (x86_64); nothing in the contract is that small otherwise.
    if (std::abs (v) < 1.0e-5)
        v = 0.0;
    char buffer[64];
    std::snprintf (buffer, sizeof (buffer), "%.6g", v);
    return buffer;
}

inline std::string hex (std::uint32_t v)
{
    char buffer[16];
    std::snprintf (buffer, sizeof (buffer), "0x%08x", v);
    return buffer;
}

inline std::string describe (int index, const juce::RangedAudioParameter& p)
{
    std::string kind = "float";
    std::string choices;
    if (const auto* c = dynamic_cast<const juce::AudioParameterChoice*> (&p))
    {
        kind = "choice";
        choices = c->choices.joinIntoString (",").toStdString();
    }
    else if (dynamic_cast<const juce::AudioParameterBool*> (&p) != nullptr)
    {
        kind = "bool";
    }
    else if (dynamic_cast<const juce::AudioParameterInt*> (&p) != nullptr)
    {
        kind = "int";
    }

    const auto& range = p.getNormalisableRange();
    std::string mapping;
    for (const auto x : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        mapping += (mapping.empty() ? "" : ",") + number (p.convertFrom0to1 (x));

    std::string texts;
    // 0.25 rather than 0.5: the centre of a gain is "-0.00 dB" or "0.00 dB"
    // depending on FMA, the quarter point is exact on both.
    for (const auto x : { 0.0f, 0.25f, 1.0f })
        texts += (texts.empty() ? "" : ",") + p.getText (x, 64).toStdString();

    const auto id = hostParameterId (p.paramID);
    return std::to_string (index)
         + "|" + p.paramID.toStdString()
         + "|" + p.getName (128).toStdString()
         + "|" + kind
         + "|hint=" + std::to_string (p.getVersionHint())
         + "|automatable=" + (p.isAutomatable() ? "1" : "0")
         + "|vst3=" + hex (id) + "|au=" + hex (id)
         + "|default=" + number (p.convertFrom0to1 (p.getDefaultValue()))
         + "|range=" + number (range.start) + ".." + number (range.end) + "/" + number (range.interval)
         + "|map=" + mapping
         + "|steps=" + std::to_string (p.getNumSteps())
         + "|label=" + p.getLabel().toStdString()
         + "|choices=" + choices
         + "|text=" + texts;
}

inline std::vector<std::string> describe (juce::AudioProcessor& processor)
{
    std::vector<std::string> lines;
    const auto& all = processor.getParameters();
    for (int i = 0; i < all.size(); ++i)
    {
        if (const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*> (all[i]))
            lines.push_back (describe (i, *ranged));
        else
            lines.push_back (std::to_string (i) + "|<not a RangedAudioParameter>");
    }
    return lines;
}

// The same line without the fields that name the parameter (index, ID,
// name, host IDs, hint): what a parameter *means*. Two banks have the same
// contract when their stripped lines are equal.
inline std::string meaning (const std::string& line)
{
    std::vector<std::string> fields;
    size_t start = 0;
    for (size_t bar = line.find ('|'); bar != std::string::npos; bar = line.find ('|', start))
    {
        fields.push_back (line.substr (start, bar - start));
        start = bar + 1;
    }
    fields.push_back (line.substr (start));

    std::string kept;
    for (size_t i = 0; i < fields.size(); ++i)
    {
        const auto& f = fields[i];
        if (i < 3 || f.rfind ("hint=", 0) == 0 || f.rfind ("vst3=", 0) == 0 || f.rfind ("au=", 0) == 0)
            continue;
        kept += (kept.empty() ? "" : "|") + f;
    }
    return kept;
}
} // namespace labq8::contract
