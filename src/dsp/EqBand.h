#pragma once

#include "dsp/BellBand.h"
#include "dsp/BiquadDesign.h"
#include "dsp/FilterTypes.h"

#include <array>
#include <optional>

namespace labq8::dsp
{
// Requested settings for one band, in host units (sanitised by EqBand).
struct BandSettings
{
    double frequencyHz = 1000.0;
    double gainDb      = 0.0;
    double q           = 1.0;
    bool bypassed      = false;
    FilterType type    = FilterType::bell;
};

// One band of the multi-filter EQ: six topologies, click-free type changes
// and bypass, real-time safe (fixed-size state, no allocation or locks).
//
// Signal paths. A path is one biquad with its own smoothing ramps, coefficients
// and per-channel state, or the "dry" path (no filtering). Two paths exist;
// normally one is active and the other unused.
//
// Smoothing. Identical to BellBand: linear ramps of rampTimeSeconds (log2 for
// frequency and Q, dB for gain), coefficients recomputed every
// updateIntervalSamples on an absolute sample grid. With type Bell the band
// is bit-identical to BellBand (verified by tests).
//
// Type changes. Changing the path kind starts a linear crossfade of
// crossfadeTimeSeconds (rounded to whole samples) from the old path to a new
// path whose ramps start at the requested settings and whose state starts at
// zero. Both paths keep processing (and following parameter changes) during
// the fade; output = old + w * (new - old), w = k / N per sample, in double.
// This is a convex combination, so the law itself never exceeds the larger
// of the two path outputs; at w = 1 the output is the new path exactly.
//
// Requests during a crossfade (bounded, latest wins, never discontinuous):
//   - same kind as the incoming path: nothing to do;
//   - same kind as the outgoing path: the fade reverses in place
//     (roles swapped, w -> 1 - w), so the output is unchanged at the switch;
//   - any other kind: stored as the single pending kind (overwriting any
//     earlier pending one) and started when the current fade completes.
//   The last requested kind is therefore reached within two fade lengths.
//
// Bypass. Bell and shelves bypass by ramping their gain to 0 dB, where they
// are an exact identity (E0/E1a behaviour). Cuts and notch ignore gain, so
// their bypass is a crossfade to the dry path. Once dry (or at 0 dB with
// the residual state decayed) the band is idle: buffer untouched, no CPU.
class EqBand
{
public:
    static constexpr int maxChannels               = BellBand::maxChannels;
    static constexpr int updateIntervalSamples     = BellBand::updateIntervalSamples;
    static constexpr double rampTimeSeconds        = BellBand::rampTimeSeconds;
    static constexpr double idleStateThreshold     = BellBand::idleStateThreshold;
    static constexpr double denormalFlushThreshold = BellBand::denormalFlushThreshold;
    static constexpr double crossfadeTimeSeconds   = 0.02;

    EqBand() noexcept;

    void prepare (double sampleRate, int numChannels) noexcept;
    void reset() noexcept;

    // Real-time safe. Repeated calls with unchanged settings are free.
    void setTargets (const BandSettings& settings) noexcept;
    void setTargets (const BellSettings& s) noexcept { setTargets (BandSettings { s.frequencyHz, s.gainDb, s.q, s.bypassed, FilterType::bell }); }

    // Real-time safe, in place.
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    bool isIdle() const noexcept;
    bool isCrossfading() const noexcept { return fading; }
    bool hasPendingChange() const noexcept { return pendingKind.has_value(); }
    int getCrossfadeLengthSamples() const noexcept { return fadeLength; }
    int getCrossfadePosition() const noexcept { return fadePosition; }

    // Filter of the active (incoming) path; nullopt when it is the dry path.
    std::optional<FilterType> getActiveFilter() const noexcept { return filterOf (paths[(size_t) active].kind); }
    // Filter of the outgoing path during a crossfade; nullopt otherwise or when dry.
    std::optional<FilterType> getFadingFilter() const noexcept;
    bool isFadingPathDry() const noexcept { return fading && paths[(size_t) (active ^ 1)].kind == dryKind; }

    double getSampleRate() const noexcept { return sampleRate; }

    // Active path (identity coefficients when dry).
    const BiquadCoefficients& getCoefficients() const noexcept { return paths[(size_t) active].coefficients; }
    double getCurrentFrequencyHz() const noexcept;
    double getCurrentGainDb() const noexcept { return paths[(size_t) active].gainDb.current; }
    double getCurrentQ() const noexcept;

private:
    static constexpr int dryKind = -1; // path kinds: 0..5 = FilterType, -1 = dry

    struct LinearRamp
    {
        double current = 0.0, target = 0.0, step = 0.0;
        int remainingTicks = 0;

        void snap (double value) noexcept;
        void setTarget (double value, int ticks) noexcept;
        bool tick() noexcept;
        bool isSettled() const noexcept { return remainingTicks == 0; }
    };

    struct ChannelState
    {
        double s1 = 0.0, s2 = 0.0;
    };

    struct Path
    {
        int kind = static_cast<int> (FilterType::bell);
        LinearRamp log2Frequency, gainDb, log2Q;
        BiquadCoefficients coefficients;
        std::array<ChannelState, maxChannels> states {};
        bool idle = true;
    };

    static std::optional<FilterType> filterOf (int kind) noexcept;
    static bool isGainKind (int kind) noexcept;
    int kindFor (const BandSettings& s) const noexcept;

    void applyTargets (Path& path, bool snap) noexcept;
    void initialisePath (Path& path, int kind) noexcept;
    void updateCoefficients (Path& path) const noexcept;
    void tickPath (Path& path) noexcept;
    void startCrossfade (int kind) noexcept;
    void finishCrossfade() noexcept;

    static void processSegment (const Path& path, float* data, int numSamples, ChannelState& state) noexcept;
    static double step (const Path& path, double x, ChannelState& state) noexcept;

    BandSettings requested;
    std::array<Path, 2> paths {};
    int active = 0;
    bool fading = false;
    int fadePosition = 0, fadeLength = 1;
    std::optional<int> pendingKind;

    double sampleRate = 44100.0;
    int numPreparedChannels = maxChannels;
    int rampTicks = 1;
    int samplesUntilTick = 0;
};
} // namespace labq8::dsp
