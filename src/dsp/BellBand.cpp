#include "dsp/BellBand.h"

#include <algorithm>
#include <cmath>

namespace labq8::dsp
{
//==============================================================================
void BellBand::LinearRamp::snap (double value) noexcept
{
    current = target = value;
    step = 0.0;
    remainingTicks = 0;
}

void BellBand::LinearRamp::setTarget (double value, int ticks) noexcept
{
    if (value == target)
        return;

    target = value;

    if (ticks <= 0)
    {
        snap (value);
        return;
    }

    step = (target - current) / ticks;
    remainingTicks = ticks;
}

bool BellBand::LinearRamp::tick() noexcept
{
    if (remainingTicks <= 0)
        return false;

    if (--remainingTicks == 0)
        current = target; // land exactly, no accumulated rounding
    else
        current += step;

    return true;
}

//==============================================================================
BellBand::BellBand() noexcept
{
    prepare (sampleRate, maxChannels);
}

void BellBand::prepare (double newSampleRate, int numChannels) noexcept
{
    sampleRate = sanitiseSampleRate (newSampleRate);
    numPreparedChannels = std::clamp (numChannels, 0, maxChannels);

    const auto rampSamples = rampTimeSeconds * sampleRate;
    rampTicks = std::max (1, static_cast<int> (std::lround (rampSamples / updateIntervalSamples)));

    reset();
}

void BellBand::reset() noexcept
{
    applyRequested (true);
    states.fill ({});
    samplesUntilTick = 0;
    updateCoefficients();
    idle = (gainDb.current == 0.0);
}

void BellBand::setTargets (const BellSettings& settings) noexcept
{
    requested = settings;
    applyRequested (false);
}

void BellBand::applyRequested (bool snap) noexcept
{
    const auto f = clampFrequency (requested.frequencyHz, sampleRate);
    const auto g = requested.bypassed ? 0.0 : clampGainDb (requested.gainDb);
    const auto q = clampQ (requested.q);

    if (snap)
    {
        log2Frequency.snap (std::log2 (f));
        gainDb.snap (g);
        log2Q.snap (std::log2 (q));
        return;
    }

    log2Frequency.setTarget (std::log2 (f), rampTicks);
    gainDb.setTarget (g, rampTicks);
    log2Q.setTarget (std::log2 (q), rampTicks);

    // A zeroed state at 0 dB is an exact identity, so leaving idle here is
    // seamless: the gain ramp starts from 0 dB.
    if (gainDb.target != 0.0)
        idle = false;
}

double BellBand::getCurrentFrequencyHz() const noexcept { return std::exp2 (log2Frequency.current); }
double BellBand::getCurrentQ() const noexcept { return std::exp2 (log2Q.current); }

void BellBand::updateCoefficients() noexcept
{
    coefficients = designBell (sampleRate, getCurrentFrequencyHz(), gainDb.current, getCurrentQ());
}

void BellBand::tick() noexcept
{
    // Every ramp must advance, so no short-circuit evaluation here.
    const bool frequencyMoving = log2Frequency.tick();
    const bool gainMoving = gainDb.tick();
    const bool qMoving = log2Q.tick();

    if (frequencyMoving || gainMoving || qMoving)
        updateCoefficients();

    bool residual = false;

    for (auto& s : states)
    {
        if (! std::isfinite (s.s1) || ! std::isfinite (s.s2))
            s = {};

        if (std::abs (s.s1) < denormalFlushThreshold) s.s1 = 0.0;
        if (std::abs (s.s2) < denormalFlushThreshold) s.s2 = 0.0;

        residual = residual || std::abs (s.s1) >= idleStateThreshold || std::abs (s.s2) >= idleStateThreshold;
    }

    if (! idle && gainDb.isSettled() && gainDb.current == 0.0 && ! residual)
    {
        states.fill ({});
        idle = true;
    }
}

void BellBand::processSegment (float* data, int numSamples, ChannelState& state) const noexcept
{
    const auto c = coefficients;
    auto s1 = state.s1;
    auto s2 = state.s2;

    for (int i = 0; i < numSamples; ++i)
    {
        double x = data[i];

        if (! std::isfinite (x))
            x = 0.0;

        const auto y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        data[i] = static_cast<float> (y);
    }

    state.s1 = s1;
    state.s2 = s2;
}

void BellBand::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numSamples <= 0)
        return;

    const auto activeChannels = std::min ({ numChannels, numPreparedChannels, maxChannels });

    for (int position = 0; position < numSamples;)
    {
        if (samplesUntilTick == 0)
        {
            tick();
            samplesUntilTick = updateIntervalSamples;
        }

        const auto n = std::min (samplesUntilTick, numSamples - position);

        if (! idle)
            for (int ch = 0; ch < activeChannels; ++ch)
                if (channels[ch] != nullptr)
                    processSegment (channels[ch] + position, n, states[(size_t) ch]);

        samplesUntilTick -= n;
        position += n;
    }
}
} // namespace labq8::dsp
