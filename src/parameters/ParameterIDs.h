#pragma once

// Stable parameter identifiers. Once a build has been used in a saved
// session these strings MUST NOT change: hosts store automation and state
// against them. See docs/PARAMETER_SCHEMA.md for the full 8-band plan.

namespace labq8::params
{
// JUCE ParameterID version hint (needed for AU/VST3 parameter ordering).
// Parameters introduced in later phases get a higher hint; existing ones keep 1.
inline constexpr int versionHintE0 = 1;

namespace id
{
    inline constexpr const char* band1Frequency = "b1_freq";
    inline constexpr const char* band1Gain      = "b1_gain";
    inline constexpr const char* band1Q         = "b1_q";
    inline constexpr const char* band1Bypass    = "b1_bypass";
} // namespace id
} // namespace labq8::params
