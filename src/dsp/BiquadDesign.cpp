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

namespace
{
    // Clamped design inputs shared by the E1b topologies.
    struct DesignInputs
    {
        double A, cosW0, sinW0, alpha, oneMinusCos;
    };

    DesignInputs prepareDesign (double sampleRate, double frequencyHz, double gainDb, double q) noexcept
    {
        sampleRate  = sanitiseSampleRate (sampleRate);
        frequencyHz = clampFrequency (frequencyHz, sampleRate);
        q           = clampQ (q);

        const auto w0 = 2.0 * std::numbers::pi * frequencyHz / sampleRate;
        const auto halfSin = std::sin (0.5 * w0);

        DesignInputs d;
        d.A     = std::pow (10.0, clampGainDb (gainDb) / 40.0);
        d.cosW0 = std::cos (w0);
        d.sinW0 = std::sin (w0);
        d.alpha = d.sinW0 / (2.0 * q);
        d.oneMinusCos = 2.0 * halfSin * halfSin; // 1 - cos(w0) without cancellation at low f0
        return d;
    }

    BiquadCoefficients normalise (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
    {
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }
} // namespace

BiquadCoefficients designLowShelf (double sampleRate, double frequencyHz, double gainDb, double q) noexcept
{
    const auto d = prepareDesign (sampleRate, frequencyHz, gainDb, q);
    const auto A = d.A;
    const auto twoSqrtAAlpha = 2.0 * std::sqrt (A) * d.alpha;

    // At A == 1 every b term equals its a term bit for bit (A-1 == 0, A+1 == 2,
    // scaling by 2 is exact), so 0 dB is an exact identity like the Bell.
    return normalise (A * ((A + 1.0) - (A - 1.0) * d.cosW0 + twoSqrtAAlpha),
                      2.0 * A * ((A - 1.0) - (A + 1.0) * d.cosW0),
                      A * ((A + 1.0) - (A - 1.0) * d.cosW0 - twoSqrtAAlpha),
                      (A + 1.0) + (A - 1.0) * d.cosW0 + twoSqrtAAlpha,
                      -2.0 * ((A - 1.0) + (A + 1.0) * d.cosW0),
                      (A + 1.0) + (A - 1.0) * d.cosW0 - twoSqrtAAlpha);
}

BiquadCoefficients designHighShelf (double sampleRate, double frequencyHz, double gainDb, double q) noexcept
{
    const auto d = prepareDesign (sampleRate, frequencyHz, gainDb, q);
    const auto A = d.A;
    const auto twoSqrtAAlpha = 2.0 * std::sqrt (A) * d.alpha;

    return normalise (A * ((A + 1.0) + (A - 1.0) * d.cosW0 + twoSqrtAAlpha),
                      -2.0 * A * ((A - 1.0) + (A + 1.0) * d.cosW0),
                      A * ((A + 1.0) + (A - 1.0) * d.cosW0 - twoSqrtAAlpha),
                      (A + 1.0) - (A - 1.0) * d.cosW0 + twoSqrtAAlpha,
                      2.0 * ((A - 1.0) - (A + 1.0) * d.cosW0),
                      (A + 1.0) - (A - 1.0) * d.cosW0 - twoSqrtAAlpha);
}

BiquadCoefficients designLowCut (double sampleRate, double frequencyHz, double q) noexcept
{
    const auto d = prepareDesign (sampleRate, frequencyHz, 0.0, q);
    const auto onePlusCos = 2.0 - d.oneMinusCos;

    return normalise (0.5 * onePlusCos, -onePlusCos, 0.5 * onePlusCos,
                      1.0 + d.alpha, -2.0 * d.cosW0, 1.0 - d.alpha);
}

BiquadCoefficients designHighCut (double sampleRate, double frequencyHz, double q) noexcept
{
    const auto d = prepareDesign (sampleRate, frequencyHz, 0.0, q);

    return normalise (0.5 * d.oneMinusCos, d.oneMinusCos, 0.5 * d.oneMinusCos,
                      1.0 + d.alpha, -2.0 * d.cosW0, 1.0 - d.alpha);
}

BiquadCoefficients designNotch (double sampleRate, double frequencyHz, double q) noexcept
{
    const auto d = prepareDesign (sampleRate, frequencyHz, 0.0, q);

    return normalise (1.0, -2.0 * d.cosW0, 1.0,
                      1.0 + d.alpha, -2.0 * d.cosW0, 1.0 - d.alpha);
}

BiquadCoefficients designFilter (FilterType type, double sampleRate, double frequencyHz, double gainDb, double q) noexcept
{
    switch (type)
    {
        case FilterType::bell:      return designBell (sampleRate, frequencyHz, gainDb, q);
        case FilterType::lowShelf:  return designLowShelf (sampleRate, frequencyHz, gainDb, q);
        case FilterType::highShelf: return designHighShelf (sampleRate, frequencyHz, gainDb, q);
        case FilterType::lowCut:    return designLowCut (sampleRate, frequencyHz, q);
        case FilterType::highCut:   return designHighCut (sampleRate, frequencyHz, q);
        case FilterType::notch:     return designNotch (sampleRate, frequencyHz, q);
    }

    return designBell (sampleRate, frequencyHz, gainDb, q);
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
