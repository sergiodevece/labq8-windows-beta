#pragma once

namespace labq8::dsp
{
// Filter topology of a band. The integer values are FROZEN: they are the
// host parameter index of b{n}_type and are stored in saved sessions.
// New topologies may only be appended.
enum class FilterType : int
{
    bell      = 0,
    lowShelf  = 1,
    highShelf = 2,
    lowCut    = 3,
    highCut   = 4,
    notch     = 5,
};

inline constexpr int numFilterTypes = 6;

// Topologies whose response depends on gain and that are an exact identity
// at 0 dB (so they may go idle). Cuts and the notch ignore gain and are
// always active unless bypassed.
constexpr bool usesGain (FilterType t) noexcept
{
    return t == FilterType::bell || t == FilterType::lowShelf || t == FilterType::highShelf;
}

// Out-of-range indices (corrupt state, future versions) fall back to Bell.
constexpr FilterType filterTypeFromIndex (int index) noexcept
{
    return (index >= 0 && index < numFilterTypes) ? static_cast<FilterType> (index) : FilterType::bell;
}
} // namespace labq8::dsp
