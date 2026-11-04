#pragma once

#include <algorithm>
#include <cmath>

// Dynamic engine (E2a): transfer law and gain-reduction ballistics.
// Pure functions and plain state, no JUCE, no allocation.

namespace labq8::dsp::dynamics
{
// Width of the soft knee around the threshold, in dB.
inline constexpr double kneeWidthDb = 6.0;

// Gain reduction in dB (>= 0) for a detector level L, threshold T and
// dynamic range R (maximum reduction), with o = L - T:
//
//   o <= -K/2        0
//   |o| <  K/2       (o + K/2)^2 / (2K)      (quadratic soft knee)
//   o >= K/2         o                        (slope 1: above the knee the
//                                              band holds the detected
//                                              component at T)
//   then             min(result, R)
//
// The knee is continuous with slope 0 at -K/2 and slope 1 at +K/2.
inline double gainReductionDb (double levelDb, double thresholdDb, double rangeDb) noexcept
{
    const auto over = levelDb - thresholdDb;
    const auto half = 0.5 * kneeWidthDb;
    double reduction = 0.0;

    if (over >= half)
        reduction = over;
    else if (over > -half)
        reduction = (over + half) * (over + half) / (2.0 * kneeWidthDb);

    return std::min (reduction, std::max (0.0, rangeDb));
}

// One-pole smoothing of the gain reduction, run once per control tick.
// Attack when the reduction increases, release when it decreases. Time
// constants are 1 - 1/e (63.2 %) times of the exponential approach.
struct GainBallistics
{
    double value = 0.0;
    double attackCoefficient = 0.0, releaseCoefficient = 0.0;

    void setTimes (double attackSeconds, double releaseSeconds, double tickSeconds) noexcept
    {
        attackCoefficient = std::exp (-tickSeconds / attackSeconds);
        releaseCoefficient = std::exp (-tickSeconds / releaseSeconds);
    }

    double process (double target) noexcept
    {
        const auto c = target > value ? attackCoefficient : releaseCoefficient;
        value = target + c * (value - target);
        return value;
    }
};
} // namespace labq8::dsp::dynamics
