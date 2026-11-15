#pragma once

// E4b: Mid/Side encoding and decoding (no JUCE, no state).
//
//   M = (L + R) / 2      L = M + S
//   S = (L - R) / 2      R = M - S
//
// Normalisation: a mono signal (L = R) gives M = L exactly and S = 0, so the
// MID bank works at the level the STEREO bank sees (its dynamic thresholds
// mean the same for centred material); an antiphase signal (R = -L) gives
// M = 0 and S = L exactly. The round trip has unity gain.
//
// Arithmetic: the sum and difference of two floats and the halving are done
// in double, then rounded once to float (no overflow for |L|, |R| up to
// FLT_MAX); decoding adds in double and rounds once. Measured (E4a §8): the
// round trip is exact for mono, antiphase and one-sided signals, and within
// 1 ulp of max(|L|, |R|) otherwise (about -138 dB relative). Bit-exact
// transparency for flat banks is therefore obtained by not coding at all
// (StereoModeEngine's flat path), never by relying on the round trip.

namespace labq8::dsp::midside
{
inline void encode (const float* left, const float* right, float* mid, float* side, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const auto l = static_cast<double> (left[i]);
        const auto r = static_cast<double> (right[i]);
        mid[i]  = static_cast<float> (0.5 * (l + r));
        side[i] = static_cast<float> (0.5 * (l - r));
    }
}

inline void decode (const float* mid, const float* side, float* left, float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const auto m = static_cast<double> (mid[i]);
        const auto s = static_cast<double> (side[i]);
        left[i]  = static_cast<float> (m + s);
        right[i] = static_cast<float> (m - s);
    }
}
} // namespace labq8::dsp::midside
