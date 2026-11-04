#pragma once

// Independent references for the E1b topologies: analog prototypes evaluated
// at the bilinear-warped frequency Omega = tan(pi f / fs) / tan(pi f0 / fs).
// Shares no code with src/dsp/BiquadDesign.cpp.

#include "dsp/BiquadDesign.h"
#include "dsp/FilterTypes.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace labq8::test::reference
{
using Complex = std::complex<double>;
using labq8::dsp::FilterType;

inline Complex analogResponse (FilterType type, double f, double f0, double gainDb, double q, double fs)
{
    const auto A = std::pow (10.0, gainDb / 40.0);
    const auto sqrtA = std::sqrt (A);
    const auto omega = std::tan (std::numbers::pi * f / fs) / std::tan (std::numbers::pi * f0 / fs);
    const Complex s (0.0, omega);
    const auto s2 = s * s;

    switch (type)
    {
        case FilterType::bell:      return (s2 + s * (A / q) + 1.0) / (s2 + s / (A * q) + 1.0);
        case FilterType::lowShelf:  return A * (s2 + s * (sqrtA / q) + A) / (A * s2 + s * (sqrtA / q) + 1.0);
        case FilterType::highShelf: return A * (A * s2 + s * (sqrtA / q) + 1.0) / (s2 + s * (sqrtA / q) + A);
        case FilterType::lowCut:    return s2 / (s2 + s / q + 1.0);
        case FilterType::highCut:   return 1.0 / (s2 + s / q + 1.0);
        case FilterType::notch:     return (s2 + 1.0) / (s2 + s / q + 1.0);
    }
    return 1.0;
}

// Complex response of a digital biquad (for crossfade phase analysis).
inline Complex digitalResponse (const labq8::dsp::BiquadCoefficients& c, double f, double fs)
{
    const auto z1 = std::polar (1.0, -2.0 * std::numbers::pi * f / fs);
    const auto z2 = z1 * z1;
    return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
}

// Largest pole radius of 1 + a1 z^-1 + a2 z^-2.
inline double poleRadius (const labq8::dsp::BiquadCoefficients& c)
{
    const auto disc = c.a1 * c.a1 - 4.0 * c.a2;
    if (disc < 0.0)
        return std::sqrt (c.a2);
    const auto r = std::sqrt (disc);
    return std::max (std::abs ((-c.a1 + r) * 0.5), std::abs ((-c.a1 - r) * 0.5));
}

// Samples for the impulse response envelope to fall by `ratio`, with an
// allowance for a double pole (t * r^t).
inline double decaySamples (const labq8::dsp::BiquadCoefficients& c, double ratio = 1.0e-6)
{
    const auto r = poleRadius (c);
    if (r <= 0.0)
        return 16.0;
    return 2.0 * std::log (ratio) / std::log (r) + 64.0;
}

inline const char* name (FilterType t)
{
    switch (t)
    {
        case FilterType::bell:      return "Bell";
        case FilterType::lowShelf:  return "LowShelf";
        case FilterType::highShelf: return "HighShelf";
        case FilterType::lowCut:    return "LowCut";
        case FilterType::highCut:   return "HighCut";
        case FilterType::notch:     return "Notch";
    }
    return "?";
}

inline constexpr FilterType allTypes[] = { FilterType::bell, FilterType::lowShelf, FilterType::highShelf,
                                          FilterType::lowCut, FilterType::highCut, FilterType::notch };
} // namespace labq8::test::reference
