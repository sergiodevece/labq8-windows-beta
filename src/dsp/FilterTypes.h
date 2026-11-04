#pragma once

#include "dsp/BandLimits.h"

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

// Largest Q each topology actually uses (E1b.1). The stored parameter keeps
// its full range [limits::minQ, limits::maxQ] for every type; the band designs
// with effectiveQ(), so stored and effective Q differ when a shelf or cut is
// set above its limit. Bell and notch use the full range.
//   - shelves: 1, overshoot outside the plateaus <= 1.12 dB at +/-24 dB;
//   - cuts:    2, resonance peak 6.30 dB.
// (docs/ARCHITECTURE.md, "Q efectivo"; labq8_sweep reproduces the figures.)
constexpr double maxEffectiveQ (FilterType t) noexcept
{
    switch (t)
    {
        case FilterType::lowShelf:
        case FilterType::highShelf: return 1.0;
        case FilterType::lowCut:
        case FilterType::highCut:   return 2.0;
        case FilterType::bell:
        case FilterType::notch:     break;
    }
    return limits::maxQ;
}

// Relative tolerance on the limit. The host stores Q as a float on a log
// range, so "2.00" set on a cut is saved as 2.0000002384; without the
// tolerance such E1b sessions would change by a fraction of a ppm. It also
// keeps the mapping continuous (a plain min with a slightly larger bound).
inline constexpr double effectiveQTolerance = 1.0e-6;

// Q the band designs with, for an already range-clamped stored Q. Returns q
// itself (bit for bit) whenever it is within the limit (+ tolerance), so it
// can exceed maxEffectiveQ() by at most effectiveQTolerance (relative).
constexpr double effectiveQ (FilterType t, double clampedQ) noexcept
{
    const auto bound = maxEffectiveQ (t) * (1.0 + effectiveQTolerance);
    return clampedQ > bound ? bound : clampedQ;
}

// Out-of-range indices (corrupt state, future versions) fall back to Bell.
constexpr FilterType filterTypeFromIndex (int index) noexcept
{
    return (index >= 0 && index < numFilterTypes) ? static_cast<FilterType> (index) : FilterType::bell;
}
} // namespace labq8::dsp
