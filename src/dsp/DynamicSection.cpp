#include "dsp/DynamicSection.h"

#include <algorithm>
#include <cmath>

namespace labq8::dsp
{
namespace
{
    double clampOr (double value, double lo, double hi, double fallback) noexcept
    {
        return std::isfinite (value) ? std::clamp (value, lo, hi) : fallback;
    }

    double clampedThreshold (const DynamicSettings& s) noexcept
    {
        return clampOr (s.thresholdDb, dynamics::limits::minThresholdDb, dynamics::limits::maxThresholdDb, -24.0);
    }

    double clampedRange (const DynamicSettings& s) noexcept
    {
        return clampOr (s.rangeDb, dynamics::limits::minRangeDb, dynamics::limits::maxRangeDb, 0.0);
    }
} // namespace

DynamicSection::DynamicSection() noexcept
{
    prepare (sampleRate, maxChannels);
}

void DynamicSection::prepare (double newSampleRate, int numChannels) noexcept
{
    sampleRate = sanitiseSampleRate (newSampleRate);
    rampTicks = std::max (1, static_cast<int> (std::lround (rampTimeSeconds * sampleRate / controlIntervalSamples)));
    detector.prepare (sampleRate, numChannels);
    setTargets (requested); // ballistics coefficients for the new rate
    reset();
}

void DynamicSection::reset() noexcept
{
    detector.reset();
    ballistics.value = 0.0;
    reductionDb = 0.0;
    thresholdDb.snap (clampedThreshold (requested));
    rangeDb.snap (clampedRange (requested));
    const bool on = requested.enabled && engaged;
    enable.snap (on ? 1.0 : 0.0);
    detectorActive = on;
}

void DynamicSection::setTargets (const DynamicSettings& settings) noexcept
{
    namespace dl = dynamics::limits;
    requested = settings;

    thresholdDb.setTarget (clampedThreshold (settings), rampTicks);
    rangeDb.setTarget (clampedRange (settings), rampTicks);
    updateEnable();

    const auto tickSeconds = controlIntervalSamples / sampleRate;
    ballistics.setTimes (clampOr (settings.attackMs, dl::minAttackMs, dl::maxAttackMs, 10.0) * 1.0e-3,
                         clampOr (settings.releaseMs, dl::minReleaseMs, dl::maxReleaseMs, 100.0) * 1.0e-3,
                         tickSeconds);
}

void DynamicSection::setEngaged (bool shouldBeEngaged) noexcept
{
    engaged = shouldBeEngaged;
    updateEnable();
}

void DynamicSection::updateEnable() noexcept
{
    const bool on = requested.enabled && engaged;
    enable.setTarget (on ? 1.0 : 0.0, rampTicks);

    if (on && ! detectorActive)
    {
        detector.reset(); // starts from silence: the reduction builds up with the attack
        detectorActive = true;
    }
}

double DynamicSection::tick (double frequencyHz, double q) noexcept
{
    thresholdDb.tick();
    rangeDb.tick();
    enable.tick();

    if (enable.current == 0.0 && enable.target == 0.0)
    {
        // Fully off: no reduction, detector idle.
        detectorActive = false;
        ballistics.value = 0.0;
        reductionDb = 0.0;
        return 0.0;
    }

    detector.sanitise();
    detector.setRegion (frequencyHz, q);

    const auto target = dynamics::gainReductionDb (detector.getLevelDb(), thresholdDb.current, rangeDb.current);
    auto reduction = std::min (ballistics.process (target), rangeDb.current);

    if (target == 0.0 && reduction < dynamics::limits::restSnapDb)
        reduction = ballistics.value = 0.0;

    reductionDb = enable.current * reduction;
    return reductionDb == 0.0 ? 0.0 : -reductionDb;
}

void DynamicSection::processDetector (const float* const* channels, int numChannels, int start, int numSamples) noexcept
{
    if (detectorActive)
        detector.process (channels, numChannels, start, numSamples);
}
} // namespace labq8::dsp
