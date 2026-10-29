#pragma once

#include "dsp/DynamicDetector.h"
#include "dsp/DynamicGainLaw.h"
#include "dsp/EqBand.h"
#include "dsp/LinearRamp.h"

namespace labq8::dsp
{
// Experimental settings of the dynamic Bell prototype (E2a). Not exposed to
// the host: no AU/VST3 parameter exists for any of them yet.
struct DynamicBellSettings
{
    double frequencyHz  = 1000.0;
    double q            = 1.0;
    double staticGainDb = 0.0;
    bool dynamicEnabled = false;
    double thresholdDb  = -24.0; // detector level (sine-peak dBFS) where reduction starts
    double rangeDb      = 6.0;   // maximum reduction
    double attackMs     = 10.0;
    double releaseMs    = 100.0;
};

namespace dynamics::limits
{
    inline constexpr double minThresholdDb = -80.0, maxThresholdDb = 24.0;
    inline constexpr double minRangeDb = 0.0, maxRangeDb = 24.0;
    inline constexpr double minAttackMs = 0.5, maxAttackMs = 500.0;
    inline constexpr double minReleaseMs = 5.0, maxReleaseMs = 5000.0;

    // Below this the smoothed reduction snaps to exactly 0 once the target is
    // 0, so a band at rest is bit-identical to the static band. 1e-4 dB is a
    // gain step of 1.2e-5 (about -98 dB relative), reached after
    // ln(R / 1e-4) release time constants (about 11 for R = 6 dB).
    inline constexpr double restSnapDb = 1.0e-4;
} // namespace dynamics::limits

// One dynamic Bell band (E2a prototype): a static EqBand (type Bell) whose
// gain is reduced by the dynamic engine.
//
// Per control tick (EqBand's 16-sample grid, absolute since prepare()):
//   1. ramps for threshold, range and the enable fade advance (20 ms);
//   2. the detector region follows the band's current frequency and Q;
//   3. target reduction = gainReductionDb(detector level, threshold, range);
//   4. GainBallistics (attack / release) smooths it; capped at the range;
//   5. EqBand::setGainModulationDb(-enable * reduction) for this tick.
// Between ticks the detector reads the input samples before the band
// filters them in place, so detection never sees the EQ output. The
// reduction applied to a tick is computed from the input up to its first
// sample (causal, no lookahead).
//
// Dynamics disabled (and its 20 ms fade-out finished): the detector is not
// run, the modulation is exactly 0 and the output is bit-identical to a
// static EqBand Bell with the same settings. Enabled with the signal below
// the knee: the same, once any earlier reduction has released.
class DynamicBellBand
{
public:
    static constexpr int maxChannels = EqBand::maxChannels;
    static constexpr int controlIntervalSamples = EqBand::updateIntervalSamples;
    static constexpr double rampTimeSeconds = EqBand::rampTimeSeconds;

    DynamicBellBand() noexcept;

    void prepare (double sampleRate, int numChannels) noexcept;
    void reset() noexcept;

    // Real-time safe. Values are clamped to dynamics::limits and the band limits.
    void setTargets (const DynamicBellSettings& settings) noexcept;
    void setDetectorLink (DetectorLink link) noexcept { detector.setLink (link); }

    // Real-time safe, in place.
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    double getDetectorLevelDb() const noexcept { return detectorActive ? detector.getLevelDb() : DynamicDetector::levelFloorDb; }
    // Reduction applied at the last tick (dB, >= 0).
    double getGainReductionDb() const noexcept { return -band.getGainModulationDb(); }
    const EqBand& getBand() const noexcept { return band; }
    double getSampleRate() const noexcept { return sampleRate; }

private:
    void controlTick() noexcept;

    EqBand band;
    DynamicDetector detector;
    dynamics::GainBallistics ballistics;
    LinearRamp thresholdDb, rangeDb, enable;
    DynamicBellSettings requested;

    double sampleRate = 44100.0;
    int rampTicks = 1;
    int samplesUntilTick = 0;
    bool detectorActive = false;
};
} // namespace labq8::dsp
