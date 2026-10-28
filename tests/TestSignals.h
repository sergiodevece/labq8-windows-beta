#pragma once

// Signal and measurement helpers for the E1a tests. DspTests.cpp keeps its own
// copies so the 18 E0 tests stay byte-identical to the validated commit.

#include "dsp/BiquadDesign.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <random>
#include <vector>

namespace labq8::test::signals
{
using Signal = std::vector<float>;

inline constexpr double designTolDb  = 1.0e-4; // double design vs analog prototype
inline constexpr double measureTolDb = 0.01;   // sine through float32 I/O
inline constexpr double sampleRates[] = { 44100.0, 48000.0, 96000.0 };

inline Signal makeNoise (size_t n, std::uint32_t seed, float amplitude = 0.5f)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-amplitude, amplitude);
    Signal s (n);
    for (auto& v : s)
        v = dist (rng);
    return s;
}

inline Signal makeSine (double frequency, double sampleRate, size_t n, double amplitude = 0.25)
{
    Signal s (n);
    for (size_t i = 0; i < n; ++i)
        s[i] = static_cast<float> (amplitude * std::sin (2.0 * std::numbers::pi * frequency * static_cast<double> (i) / sampleRate));
    return s;
}

inline size_t samples (double seconds, double sampleRate)
{
    return static_cast<size_t> (std::llround (seconds * sampleRate));
}

inline double toDb (double linear) { return 20.0 * std::log10 (linear); }

// Analog prototype at the bilinear-warped frequency (independent of designBell).
inline double analogBellDb (double f, double f0, double gainDb, double q, double fs)
{
    const auto A = std::pow (10.0, gainDb / 40.0);
    const auto omega = std::tan (std::numbers::pi * f / fs) / std::tan (std::numbers::pi * f0 / fs);
    const std::complex<double> s (0.0, omega);
    const auto h = (s * s + s * (A / q) + 1.0) / (s * s + s / (A * q) + 1.0);
    return 20.0 * std::log10 (std::abs (h));
}

// Least-squares amplitude of a known-frequency sinusoid in y[begin, end).
inline double fitAmplitude (const Signal& y, size_t begin, size_t end, double f, double fs)
{
    double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;

    for (auto i = begin; i < end; ++i)
    {
        const auto ph = 2.0 * std::numbers::pi * f * static_cast<double> (i) / fs;
        const auto s = std::sin (ph), c = std::cos (ph);
        ss += s * s; cc += c * c; sc += s * c;
        ys += y[i] * s; yc += y[i] * c;
    }

    const auto det = ss * cc - sc * sc;
    const auto a = (ys * cc - yc * sc) / det;
    const auto b = (yc * ss - ys * sc) / det;
    return std::hypot (a, b);
}

// e^-12 decay time of the slowest pole (pole Q = A*Q, over- or underdamped).
inline double settleSeconds (double f0, double gainDb, double q)
{
    const auto poleQ = std::pow (10.0, gainDb / 40.0) * q;
    const auto w0 = 2.0 * std::numbers::pi * f0;
    const auto k = 1.0 / (2.0 * poleQ);
    const auto slowestRate = poleQ >= 0.5 ? w0 * k : w0 * (k - std::sqrt (k * k - 1.0));
    return std::max (0.25, 12.0 / slowestRate);
}

inline bool allFinite (const Signal& s)
{
    return std::all_of (s.begin(), s.end(), [] (float v) { return std::isfinite (v); });
}

inline float peak (const Signal& s, size_t begin = 0, size_t end = static_cast<size_t> (-1))
{
    float p = 0.0f;
    for (auto i = begin; i < std::min (end, s.size()); ++i)
        p = std::max (p, std::abs (s[i]));
    return p;
}
} // namespace labq8::test::signals
