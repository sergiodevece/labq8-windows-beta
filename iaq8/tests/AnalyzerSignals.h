#pragma once

// Deterministic test signals for the IAQ8 analyzer tests (pink noise and a
// biquad to shape it). Lives in iaq8/tests: LABQ8's tests/ stay untouched.

#include "dsp/BiquadDesign.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>
#include <random>
#include <vector>

namespace iaq8::test
{
using Signal = std::vector<float>;

inline void scaleToRmsDbfs (Signal& x, double rmsDbfs)
{
    double ms = 0;
    for (auto v : x)
        ms += (double) v * v;
    ms /= (double) std::max<size_t> (1, x.size());
    if (ms <= 0)
        return;
    const auto g = std::pow (10.0, rmsDbfs / 20.0) / std::sqrt (ms);
    for (auto& v : x)
        v = (float) (v * g);
}

// Pink noise: power density exactly proportional to 1/f from 10 Hz to Nyquist
// (zero below), times 10^(gainDb(f)/10) when gainDb is given. Built in the
// frequency domain with phases from a fixed-seed generator, so every run gives
// the same samples. Scaled to rmsDbfs.
inline Signal makePink (size_t n, double fs, std::uint32_t seed, double rmsDbfs = -20.0,
                        const std::function<double (double)>& gainDb = {})
{
    int order = 1;
    while ((size_t (1) << order) < n)
        ++order;
    const auto size = size_t (1) << order;

    std::vector<float> buf (2 * size, 0.0f);
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> phase (0.0, 2.0 * std::numbers::pi);

    for (size_t k = 1; k < size / 2; ++k)
    {
        const auto f = (double) k * fs / (double) size;
        const auto ph = phase (rng); // drawn for every bin: same phases whatever the shaping
        if (f < 10.0)
            continue;
        auto mag = 1.0 / std::sqrt (f);
        if (gainDb)
            mag *= std::pow (10.0, gainDb (f) / 20.0);
        buf[2 * k] = (float) (mag * std::cos (ph));
        buf[2 * k + 1] = (float) (mag * std::sin (ph));
    }

    juce::dsp::FFT (order).performRealOnlyInverseTransform (buf.data());
    Signal x (buf.begin(), buf.begin() + (std::ptrdiff_t) n);
    scaleToRmsDbfs (x, rmsDbfs);
    return x;
}

// Filters x in place through a biquad (transposed direct form II, double).
inline void applyBiquad (Signal& x, const labq8::dsp::BiquadCoefficients& c)
{
    double z1 = 0, z2 = 0;
    for (auto& v : x)
    {
        const double in = v;
        const double out = c.b0 * in + z1;
        z1 = c.b1 * in - c.a1 * out + z2;
        z2 = c.b2 * in - c.a2 * out;
        v = (float) out;
    }
}
} // namespace iaq8::test
