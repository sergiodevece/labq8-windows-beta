#include "dsp/DynamicBellBand.h"

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
} // namespace

DynamicBellBand::DynamicBellBand() noexcept
{
    prepare (sampleRate, maxChannels);
}

void DynamicBellBand::prepare (double newSampleRate, int numChannels) noexcept
{
    sampleRate = sanitiseSampleRate (newSampleRate);
    rampTicks = std::max (1, static_cast<int> (std::lround (rampTimeSeconds * sampleRate / controlIntervalSamples)));
    band.prepare (sampleRate, numChannels);
    detector.prepare (sampleRate, numChannels);
    setTargets (requested); // ballistics coefficients for the new rate
    reset();
}

void DynamicBellBand::reset() noexcept
{
    band.setGainModulationDb (0.0);
    band.reset();
    detector.reset();
    ballistics.value = 0.0;
    thresholdDb.snap (clampOr (requested.thresholdDb, dynamics::limits::minThresholdDb, dynamics::limits::maxThresholdDb, -24.0));
    rangeDb.snap (clampOr (requested.rangeDb, dynamics::limits::minRangeDb, dynamics::limits::maxRangeDb, 0.0));
    enable.snap (requested.dynamicEnabled ? 1.0 : 0.0);
    detectorActive = requested.dynamicEnabled;
    samplesUntilTick = 0;
}

void DynamicBellBand::setTargets (const DynamicBellSettings& settings) noexcept
{
    namespace dl = dynamics::limits;
    requested = settings;

    band.setTargets (BandSettings { settings.frequencyHz, settings.staticGainDb, settings.q, false, FilterType::bell });

    thresholdDb.setTarget (clampOr (settings.thresholdDb, dl::minThresholdDb, dl::maxThresholdDb, -24.0), rampTicks);
    rangeDb.setTarget (clampOr (settings.rangeDb, dl::minRangeDb, dl::maxRangeDb, 0.0), rampTicks);
    enable.setTarget (settings.dynamicEnabled ? 1.0 : 0.0, rampTicks);

    if (settings.dynamicEnabled && ! detectorActive)
    {
        detector.reset(); // starts from silence: the reduction builds up with the attack
        detectorActive = true;
    }

    const auto tickSeconds = controlIntervalSamples / sampleRate;
    ballistics.setTimes (clampOr (settings.attackMs, dl::minAttackMs, dl::maxAttackMs, 10.0) * 1.0e-3,
                         clampOr (settings.releaseMs, dl::minReleaseMs, dl::maxReleaseMs, 100.0) * 1.0e-3,
                         tickSeconds);
}

void DynamicBellBand::controlTick() noexcept
{
    thresholdDb.tick();
    rangeDb.tick();
    enable.tick();

    if (enable.current == 0.0 && enable.target == 0.0)
    {
        // Fully disabled: static band, detector idle.
        detectorActive = false;
        ballistics.value = 0.0;
        band.setGainModulationDb (0.0);
        return;
    }

    detector.sanitise();
    detector.setRegion (band.getCurrentFrequencyHz(), band.getCurrentQ());

    const auto target = dynamics::gainReductionDb (detector.getLevelDb(), thresholdDb.current, rangeDb.current);
    auto reduction = std::min (ballistics.process (target), rangeDb.current);

    if (target == 0.0 && reduction < dynamics::limits::restSnapDb)
        reduction = ballistics.value = 0.0;

    const auto modulation = enable.current * reduction;
    band.setGainModulationDb (modulation == 0.0 ? 0.0 : -modulation);
}

void DynamicBellBand::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numSamples <= 0)
        return;

    const auto activeChannels = std::clamp (numChannels, 0, maxChannels);
    float* segment[maxChannels] {};

    for (int position = 0; position < numSamples;)
    {
        if (samplesUntilTick == 0)
        {
            controlTick();
            samplesUntilTick = controlIntervalSamples;
        }

        const auto n = std::min (samplesUntilTick, numSamples - position);

        // Detection reads the input before the band filters it in place.
        if (detectorActive)
            detector.process (channels, activeChannels, position, n);

        for (int ch = 0; ch < activeChannels; ++ch)
            segment[ch] = channels[ch] != nullptr ? channels[ch] + position : nullptr;

        band.process (segment, activeChannels, n);

        samplesUntilTick -= n;
        position += n;
    }
}
} // namespace labq8::dsp
