#include "dsp/BiquadDesign.h"

#include "dsp/BandLimits.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace labq8::dsp
{
double sanitiseSampleRate (double sampleRate) noexcept
{
    if (! std::isfinite (sampleRate) || sampleRate < limits::minSampleRate || sampleRate > limits::maxSampleRate)
        return limits::fallbackSampleRate;

    return sampleRate;
}

double clampFrequency (double frequencyHz, double sampleRate) noexcept
{
    if (! std::isfinite (frequencyHz))
        frequencyHz = limits::defaultFrequencyHz;

    const auto upper = std::min (limits::maxFrequencyHz,
                                 limits::maxNyquistFraction * sanitiseSampleRate (sampleRate));
    return std::clamp (frequencyHz, limits::minFrequencyHz, upper);
}

double clampGainDb (double gainDb) noexcept
{
    if (! std::isfinite (gainDb))
        return limits::defaultGainDb;

    return std::clamp (gainDb, limits::minGainDb, limits::maxGainDb);
}

double clampQ (double q) noexcept
{
    if (! std::isfinite (q))
        return limits::defaultQ;

    return std::clamp (q, limits::minQ, limits::maxQ);
}

BiquadCoefficients designBell (double sampleRate, double frequencyHz, double gainDb, double q) noexcept
{
    sampleRate  = sanitiseSampleRate (sampleRate);
    frequencyHz = clampFrequency (frequencyHz, sampleRate);
    gainDb      = clampGainDb (gainDb);
    q           = clampQ (q);

    const auto A     = std::pow (10.0, gainDb / 40.0);
    const auto w0    = 2.0 * std::numbers::pi * frequencyHz / sampleRate;
    const auto cosW0 = std::cos (w0);
    const auto alpha = std::sin (w0) / (2.0 * q);

    const auto a0 = 1.0 + alpha / A;
    const auto b1 = -2.0 * cosW0;

    BiquadCoefficients c;
    c.b0 = (1.0 + alpha * A) / a0;
    c.b1 = b1 / a0;
    c.b2 = (1.0 - alpha * A) / a0;
    c.a1 = b1 / a0; // same expression as b1 on purpose: keeps 0 dB bit-exact
    c.a2 = (1.0 - alpha / A) / a0;
    return c;
}

double magnitudeAt (const BiquadCoefficients& c, double frequencyHz, double sampleRate) noexcept
{
    const auto w  = 2.0 * std::numbers::pi * frequencyHz / sampleRate;
    const auto z1 = std::polar (1.0, -w);
    const auto z2 = z1 * z1;
    const auto num = c.b0 + c.b1 * z1 + c.b2 * z2;
    const auto den = 1.0 + c.a1 * z1 + c.a2 * z2;
    return std::abs (num / den);
}

bool isStable (const BiquadCoefficients& c) noexcept
{
    return std::abs (c.a2) < 1.0 && std::abs (c.a1) < 1.0 + c.a2;
}
} // namespace labq8::dsp
