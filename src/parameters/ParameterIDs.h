#pragma once

#include <array>

// Stable parameter identifiers. Once a build has been used in a saved
// session these strings MUST NOT change: hosts store automation and state
// against them. See docs/PARAMETER_SCHEMA.md.

namespace labq8::params
{
inline constexpr int numBands = 8;

// JUCE ParameterID version hints (AU/VST3 parameter ordering). A parameter
// keeps the hint of the phase that introduced it.
inline constexpr int versionHintE0  = 1;
inline constexpr int versionHintE1a = 2;

struct BandIDs
{
    const char* frequency;
    const char* gain;
    const char* q;
    const char* bypass;
};

// Spelled out on purpose (no string building): every frozen ID is greppable.
inline constexpr std::array<BandIDs, numBands> bandIDs { {
    { "b1_freq", "b1_gain", "b1_q", "b1_bypass" }, // E0
    { "b2_freq", "b2_gain", "b2_q", "b2_bypass" }, // E1a
    { "b3_freq", "b3_gain", "b3_q", "b3_bypass" },
    { "b4_freq", "b4_gain", "b4_q", "b4_bypass" },
    { "b5_freq", "b5_gain", "b5_q", "b5_bypass" },
    { "b6_freq", "b6_gain", "b6_q", "b6_bypass" },
    { "b7_freq", "b7_gain", "b7_q", "b7_bypass" },
    { "b8_freq", "b8_gain", "b8_q", "b8_bypass" },
} };

// Default centre frequencies (Hz). Band 1 keeps its E0 default; bands 2..8
// spread over the spectrum around it. Rationale in docs/PARAMETER_SCHEMA.md.
inline constexpr std::array<double, numBands> defaultFrequencyHz {
    1000.0, 40.0, 100.0, 250.0, 2500.0, 5000.0, 10000.0, 16000.0
};

namespace id
{
    inline constexpr const char* band1Frequency = bandIDs[0].frequency;
    inline constexpr const char* band1Gain      = bandIDs[0].gain;
    inline constexpr const char* band1Q         = bandIDs[0].q;
    inline constexpr const char* band1Bypass    = bandIDs[0].bypass;
} // namespace id
} // namespace labq8::params
