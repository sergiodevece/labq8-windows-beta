#pragma once

#include "dsp/DynamicDetector.h"
#include "dsp/DynamicGainLaw.h"
#include "dsp/LinearRamp.h"

namespace labq8::dsp
{
// Dynamic settings of one band (E2a/E2b). Internal: no AU/VST3 parameter
// exists for any of them yet (E2c).
struct DynamicSettings
{
    bool enabled       = false;
    double thresholdDb = -24.0; // detector level (sine-peak dBFS) where reduction starts
    double rangeDb     = 6.0;   // maximum reduction
    double attackMs    = 10.0;
    double releaseMs   = 100.0;
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

// The dynamic part of one band, independent of the filter it drives:
// detector, transfer law, ballistics and the 20 ms ramps of threshold, range
// and the enable fade. Real-time safe: fixed-size state, no allocation.
//
// The owner calls, on its 16-sample control grid:
//   tick (f, Q)            once per tick, before the tick's samples; returns
//                          the gain modulation in dB (<= 0; exactly 0 when
//                          at rest or off) for the filter;
//   processDetector (...)  on the tick's samples of the detection signal.
//
// Enable fade target = settings.enabled && engaged. "Engaged" is set by the
// owner (E2b: the band is a Bell and not bypassed), so a type change or a
// bypass fades the reduction out over 20 ms like switching dynamics off.
// Fully off (fade at 0): the detector is not run and tick() returns 0.
class DynamicSection
{
public:
    static constexpr int maxChannels = DynamicDetector::maxChannels;
    static constexpr int controlIntervalSamples = 16;
    static constexpr double rampTimeSeconds = 0.02;

    DynamicSection() noexcept;

    void prepare (double sampleRate, int numChannels) noexcept;
    void reset() noexcept;

    // Real-time safe. Values are clamped to dynamics::limits.
    void setTargets (const DynamicSettings& settings) noexcept;
    void setEngaged (bool shouldBeEngaged) noexcept;
    void setDetectorLink (DetectorLink link) noexcept { detector.setLink (link); }

    // Real-time safe. See the class comment.
    double tick (double frequencyHz, double q) noexcept;
    void processDetector (const float* const* channels, int numChannels, int start, int numSamples) noexcept;

    // True while the detector runs (enabled, or fading out).
    bool isActive() const noexcept { return detectorActive; }
    const DynamicSettings& getSettings() const noexcept { return requested; }
    double getGainReductionDb() const noexcept { return reductionDb; }
    double getDetectorLevelDb() const noexcept { return detectorActive ? detector.getLevelDb() : DynamicDetector::levelFloorDb; }

private:
    void updateEnable() noexcept;

    DynamicDetector detector;
    dynamics::GainBallistics ballistics;
    LinearRamp thresholdDb, rangeDb, enable;
    DynamicSettings requested;

    double sampleRate = 44100.0;
    double reductionDb = 0.0;
    int rampTicks = 1;
    bool engaged = true;
    bool detectorActive = false;
};
} // namespace labq8::dsp
