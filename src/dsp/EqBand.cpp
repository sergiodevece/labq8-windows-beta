#include "dsp/EqBand.h"

#include <algorithm>
#include <cmath>

namespace labq8::dsp
{
//==============================================================================
EqBand::EqBand() noexcept
{
    prepare (sampleRate, maxChannels);
}

std::optional<FilterType> EqBand::filterOf (int kind) noexcept
{
    if (kind == dryKind)
        return std::nullopt;

    return filterTypeFromIndex (kind);
}

double EqBand::modulationFor (int kind) const noexcept
{
    // E2b: only the Bell is dynamic. Shelves keep their static gain until a
    // shelf detection region is defined (docs/DYNAMICS_E2B.md).
    return kind == static_cast<int> (FilterType::bell) ? gainModulationDb : 0.0;
}

bool EqBand::isGainKind (int kind) noexcept
{
    return kind != dryKind && usesGain (filterTypeFromIndex (kind));
}

int EqBand::kindFor (const BandSettings& s) const noexcept
{
    const auto type = filterTypeFromIndex (static_cast<int> (s.type));
    return (s.bypassed && ! usesGain (type)) ? dryKind : static_cast<int> (type);
}

std::optional<FilterType> EqBand::getFadingFilter() const noexcept
{
    return fading ? filterOf (paths[(size_t) (active ^ 1)].kind) : std::nullopt;
}

double EqBand::getCurrentFrequencyHz() const noexcept { return std::exp2 (paths[(size_t) active].log2Frequency.current); }
double EqBand::getCurrentQ() const noexcept { return std::exp2 (paths[(size_t) active].log2Q.current); }

void EqBand::setGainModulationDb (double modulationDb) noexcept
{
    if (! std::isfinite (modulationDb))
        modulationDb = 0.0;

    gainModulationDb = modulationDb;

    // A modulated gain topology is no longer an identity; it is picked up at
    // the next control tick (tickPath).
    if (modulationDb != 0.0)
        for (auto& path : paths)
            if (modulationFor (path.kind) != 0.0)
                path.idle = false;
}

bool EqBand::isIdle() const noexcept
{
    const auto& path = paths[(size_t) active];
    return ! fading && (path.kind == dryKind || path.idle);
}

//==============================================================================
void EqBand::prepare (double newSampleRate, int numChannels) noexcept
{
    sampleRate = sanitiseSampleRate (newSampleRate);
    numPreparedChannels = std::clamp (numChannels, 0, maxChannels);

    const auto rampSamples = rampTimeSeconds * sampleRate;
    rampTicks = std::max (1, static_cast<int> (std::lround (rampSamples / updateIntervalSamples)));
    fadeLength = std::max (1, static_cast<int> (std::lround (crossfadeTimeSeconds * sampleRate)));

    reset();
}

void EqBand::reset() noexcept
{
    fading = false;
    fadePosition = 0;
    pendingKind.reset();
    initialisePath (paths[(size_t) active], kindFor (requested));
    samplesUntilTick = 0;
}

void EqBand::applyTargets (Path& path, bool snap) noexcept
{
    // Bell and shelves bypass by going to 0 dB; other kinds track the user
    // gain (unused by cuts, notch and dry, but kept continuous).
    const auto f = clampFrequency (requested.frequencyHz, sampleRate);
    const auto g = (requested.bypassed && isGainKind (path.kind)) ? 0.0 : clampGainDb (requested.gainDb);
    // Each path designs with the effective Q of its own topology (E1b.1);
    // the limit acts on the target, so the 20 ms ramp still applies to it.
    const auto q = path.kind == dryKind ? clampQ (requested.q)
                                        : effectiveQ (filterTypeFromIndex (path.kind), clampQ (requested.q));

    if (snap)
    {
        path.log2Frequency.snap (std::log2 (f));
        path.gainDb.snap (g);
        path.log2Q.snap (std::log2 (q));
        return;
    }

    path.log2Frequency.setTarget (std::log2 (f), rampTicks);
    path.gainDb.setTarget (g, rampTicks);
    path.log2Q.setTarget (std::log2 (q), rampTicks);

    if (isGainKind (path.kind) && path.gainDb.target != 0.0)
        path.idle = false;
}

void EqBand::initialisePath (Path& path, int kind) noexcept
{
    path.kind = kind;
    path.interpolating = false;
    applyTargets (path, true);
    path.states.fill ({});
    updateCoefficients (path);
    path.idle = kind == dryKind || (isGainKind (kind) && path.gainDb.current == 0.0 && modulationFor (kind) == 0.0);
}

void EqBand::updateCoefficients (Path& path) const noexcept
{
    if (path.kind == dryKind)
    {
        path.coefficients = {};
        return;
    }

    // Gain modulation (E2a) only acts on gain topologies. With no modulation
    // the gain passed to the designer is exactly the E1b.1 one.
    const auto modulation = modulationFor (path.kind);
    path.appliedModulationDb = modulation;

    path.coefficients = designFilter (filterTypeFromIndex (path.kind), sampleRate,
                                      std::exp2 (path.log2Frequency.current),
                                      modulation == 0.0 ? path.gainDb.current : path.gainDb.current + modulation,
                                      std::exp2 (path.log2Q.current));
}

void EqBand::tickPath (Path& path) noexcept
{
    // Every ramp must advance, so no short-circuit evaluation here.
    const bool frequencyMoving = path.log2Frequency.tick();
    const bool gainMoving = path.gainDb.tick();
    const bool qMoving = path.log2Q.tick();

    if (path.kind == dryKind)
        return;

    const bool modulationMoving = path.appliedModulationDb != modulationFor (path.kind);

    path.interpolating = false;

    if (frequencyMoving || gainMoving || qMoving || modulationMoving)
    {
        const auto previous = path.coefficients;
        updateCoefficients (path);

        // A modulation change is spread over this tick, sample by sample
        // (see interpolatedCoefficients). Without modulation changes the
        // coefficients still switch at the tick, exactly as in E1b.1.
        if (modulationMoving && ! path.idle)
        {
            path.interpolationStart = previous;
            path.interpolating = true;
        }
    }

    bool residual = false;

    for (auto& s : path.states)
    {
        if (! std::isfinite (s.s1) || ! std::isfinite (s.s2))
            s = {};

        if (std::abs (s.s1) < denormalFlushThreshold) s.s1 = 0.0;
        if (std::abs (s.s2) < denormalFlushThreshold) s.s2 = 0.0;

        residual = residual || std::abs (s.s1) >= idleStateThreshold || std::abs (s.s2) >= idleStateThreshold;
    }

    // Only gain topologies are neutral at 0 dB; cuts and notch never go idle.
    if (isGainKind (path.kind) && ! path.idle && path.gainDb.isSettled() && path.gainDb.current == 0.0
        && modulationFor (path.kind) == 0.0 && ! path.interpolating && ! residual)
    {
        path.states.fill ({});
        path.idle = true;
    }
}

//==============================================================================
void EqBand::setTargets (const BandSettings& settings) noexcept
{
    requested = settings;
    const auto kind = kindFor (settings);

    if (! fading)
    {
        if (kind == paths[(size_t) active].kind)
            applyTargets (paths[(size_t) active], false);
        else
            startCrossfade (kind);

        return;
    }

    if (kind == paths[(size_t) active].kind)
    {
        pendingKind.reset();
    }
    else if (kind == paths[(size_t) (active ^ 1)].kind)
    {
        // Back to the outgoing kind: reverse in place. old + w(new - old) is
        // unchanged when the roles swap and w becomes 1 - w.
        pendingKind.reset();
        active ^= 1;
        fadePosition = fadeLength - fadePosition;
    }
    else
    {
        pendingKind = kind; // latest request wins
    }

    applyTargets (paths[0], false);
    applyTargets (paths[1], false);

    if (fadePosition >= fadeLength) // reversed before any sample was faded
        finishCrossfade();
}

void EqBand::startCrossfade (int kind) noexcept
{
    applyTargets (paths[(size_t) active], false); // the outgoing path keeps following the settings
    active ^= 1;
    initialisePath (paths[(size_t) active], kind);
    fading = true;
    fadePosition = 0;
}

void EqBand::finishCrossfade() noexcept
{
    fading = false;
    fadePosition = 0;

    if (pendingKind.has_value() && *pendingKind != paths[(size_t) active].kind)
    {
        const auto kind = *pendingKind;
        pendingKind.reset();
        startCrossfade (kind);
        return;
    }

    pendingKind.reset();
}

//==============================================================================
void EqBand::processSegment (const Path& path, float* data, int numSamples, ChannelState& state) noexcept
{
    // Identical arithmetic to BellBand::processSegment.
    const auto c = path.coefficients;
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

BiquadCoefficients EqBand::interpolatedCoefficients (const Path& path, int indexInTick) noexcept
{
    // Linear interpolation from the coefficients in force at the end of the
    // previous tick to this tick's design, reached exactly on the last sample.
    // The stability triangle is convex, so every intermediate set is stable.
    if (! path.interpolating || indexInTick >= updateIntervalSamples - 1)
        return path.coefficients;

    const auto w = static_cast<double> (indexInTick + 1) / updateIntervalSamples;
    const auto& a = path.interpolationStart;
    const auto& b = path.coefficients;
    return { a.b0 + w * (b.b0 - a.b0), a.b1 + w * (b.b1 - a.b1), a.b2 + w * (b.b2 - a.b2),
             a.a1 + w * (b.a1 - a.a1), a.a2 + w * (b.a2 - a.a2) };
}

void EqBand::processSegmentInterpolated (const Path& path, float* data, int numSamples, int firstIndex, ChannelState& state) noexcept
{
    auto s1 = state.s1;
    auto s2 = state.s2;

    for (int i = 0; i < numSamples; ++i)
    {
        double x = data[i];

        if (! std::isfinite (x))
            x = 0.0;

        const auto c = interpolatedCoefficients (path, firstIndex + i);
        const auto y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        data[i] = static_cast<float> (y);
    }

    state.s1 = s1;
    state.s2 = s2;
}

double EqBand::step (const Path& path, double x, ChannelState& state, int indexInTick) noexcept
{
    if (path.kind == dryKind || path.idle)
        return x;

    const auto c = path.interpolating ? interpolatedCoefficients (path, indexInTick) : path.coefficients;
    const auto y = c.b0 * x + state.s1;
    state.s1 = c.b1 * x - c.a1 * y + state.s2;
    state.s2 = c.b2 * x - c.a2 * y;
    return y;
}

void EqBand::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numSamples <= 0)
        return;

    const auto activeChannels = std::min ({ numChannels, numPreparedChannels, maxChannels });

    for (int position = 0; position < numSamples;)
    {
        if (samplesUntilTick == 0)
        {
            tickPath (paths[(size_t) active]);
            if (fading)
                tickPath (paths[(size_t) (active ^ 1)]);
            samplesUntilTick = updateIntervalSamples;
        }

        auto n = std::min (samplesUntilTick, numSamples - position);
        const auto indexInTick = updateIntervalSamples - samplesUntilTick;

        if (fading)
        {
            n = std::min (n, fadeLength - fadePosition);
            auto& incoming = paths[(size_t) active];
            auto& outgoing = paths[(size_t) (active ^ 1)];

            for (int ch = 0; ch < activeChannels; ++ch)
            {
                if (channels[ch] == nullptr)
                    continue;

                auto* data = channels[ch] + position;
                auto& inState = incoming.states[(size_t) ch];
                auto& outState = outgoing.states[(size_t) ch];

                for (int i = 0; i < n; ++i)
                {
                    double x = data[i];

                    if (! std::isfinite (x))
                        x = 0.0;

                    const auto yIn = step (incoming, x, inState, indexInTick + i);
                    const auto yOut = step (outgoing, x, outState, indexInTick + i);
                    const auto w = static_cast<double> (fadePosition + i + 1) / fadeLength;
                    data[i] = static_cast<float> (yOut + w * (yIn - yOut));
                }
            }

            fadePosition += n;

            if (fadePosition >= fadeLength)
                finishCrossfade();
        }
        else
        {
            const auto& path = paths[(size_t) active];

            if (path.kind != dryKind && ! path.idle)
                for (int ch = 0; ch < activeChannels; ++ch)
                    if (channels[ch] != nullptr)
                    {
                        auto& state = paths[(size_t) active].states[(size_t) ch];
                        if (path.interpolating)
                            processSegmentInterpolated (path, channels[ch] + position, n, indexInTick, state);
                        else
                            processSegment (path, channels[ch] + position, n, state);
                    }
        }

        samplesUntilTick -= n;
        position += n;
    }
}
} // namespace labq8::dsp
