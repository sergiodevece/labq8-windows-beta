#pragma once

#include "dsp/BellBand.h"
#include "dsp/BiquadDesign.h"
#include "dsp/FilterTypes.h"
#include "dsp/LinearRamp.h"

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
// Effective Q (E1b.1). Each path ramps towards effectiveQ(type, stored Q):
// shelves are limited to Q 1 and cuts to Q 2, Bell and notch keep the full
// range. The limit applies to the target, so a change of effective Q (stored
// Q crossing the limit, or a type change) is smoothed like any Q change, and
// moving the stored Q above the limit changes nothing at all.
//
// Gain modulation (E2a). setGainModulationDb() adds a dB offset to the gain
// of gain topologies (Bell, shelves) from the next control tick. The caller
// (the dynamic engine) supplies an already smoothed control signal on the
// tick grid; a change is spread over the tick by interpolating the biquad
// coefficients linearly, sample by sample, from the previous design to the
// new one (no 16-sample staircase). The total gain is still clamped to
// [-24, +24] dB by the designer. With modulation 0 (the default, and the
// only value the plugin uses) nothing is interpolated and the band is
// exactly the E1b.1 band.
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

    // Real-time safe. Takes effect at the next control tick; not reset by prepare().
    void setGainModulationDb (double modulationDb) noexcept;
    double getGainModulationDb() const noexcept { return gainModulationDb; }

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
    // Effective Q of the active path (what the filter uses), not the stored Q.
    double getCurrentQ() const noexcept;

private:
    static constexpr int dryKind = -1; // path kinds: 0..5 = FilterType, -1 = dry

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
        double appliedModulationDb = 0.0;
        BiquadCoefficients interpolationStart;
        bool interpolating = false; // this tick spreads a modulation change over its samples
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
    static void processSegmentInterpolated (const Path& path, float* data, int numSamples, int firstIndex, ChannelState& state) noexcept;
    static BiquadCoefficients interpolatedCoefficients (const Path& path, int indexInTick) noexcept;
    static double step (const Path& path, double x, ChannelState& state, int indexInTick) noexcept;

    BandSettings requested;
    std::array<Path, 2> paths {};
    int active = 0;
    bool fading = false;
    int fadePosition = 0, fadeLength = 1;
    std::optional<int> pendingKind;
    double gainModulationDb = 0.0;

    double sampleRate = 44100.0;
    int numPreparedChannels = maxChannels;
    int rampTicks = 1;
    int samplesUntilTick = 0;
};
} // namespace labq8::dsp
