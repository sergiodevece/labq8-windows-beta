#pragma once

#include "dsp/BiquadDesign.h"

#include <array>

namespace labq8::dsp
{
// Requested settings for one Bell band, in host units. Values may be out of
// range or non-finite: BellBand sanitises them.
struct BellSettings
{
    double frequencyHz = 1000.0;
    double gainDb      = 0.0;
    double q           = 1.0;
    bool bypassed      = false;
};

// One minimum-phase Bell band, up to two channels, real-time safe.
//
// - No allocation, locks or I/O after construction; all state is fixed-size.
// - Parameters ramp linearly over rampTimeSeconds (frequency and Q in log2
//   domain, gain in dB). Coefficients are recomputed every
//   updateIntervalSamples while a ramp is moving. The update grid follows an
//   absolute sample counter, so the output does not depend on host block size.
// - Bypass ramps the effective gain to 0 dB, where the Bell is an exact
//   identity; once the residual state has decayed the band goes idle and
//   leaves the buffer untouched (bit-exact dry signal, no CPU).
// - Non-finite input samples are treated as 0 so they never corrupt the
//   filter state; state that is non-finite or denormal-small is cleared.
class BellBand
{
public:
    static constexpr int maxChannels             = 2;
    static constexpr int updateIntervalSamples   = 16;
    static constexpr double rampTimeSeconds      = 0.02;
    static constexpr double idleStateThreshold   = 1.0e-9; // about -180 dBFS
    static constexpr double denormalFlushThreshold = 1.0e-30;

    BellBand() noexcept;

    // Call from prepareToPlay (not real-time). Snaps all ramps to the current
    // targets and clears the filter state.
    void prepare (double sampleRate, int numChannels) noexcept;

    // Clears state and snaps ramps to targets without changing sample rate.
    void reset() noexcept;

    // Real-time safe. Starts ramps towards the new settings; repeated calls
    // with unchanged settings are free.
    void setTargets (const BellSettings& settings) noexcept;

    // Real-time safe. Processes in place. Channels beyond maxChannels (or
    // beyond the prepared count) are left untouched.
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    bool isIdle() const noexcept { return idle; }
    double getSampleRate() const noexcept { return sampleRate; }
    const BiquadCoefficients& getCoefficients() const noexcept { return coefficients; }

    // Current (smoothed) values actually used by the filter.
    double getCurrentFrequencyHz() const noexcept;
    double getCurrentGainDb() const noexcept { return gainDb.current; }
    double getCurrentQ() const noexcept;

private:
    struct LinearRamp
    {
        double current = 0.0, target = 0.0, step = 0.0;
        int remainingTicks = 0;

        void snap (double value) noexcept;
        void setTarget (double value, int ticks) noexcept;
        bool tick() noexcept; // returns true while moving
        bool isSettled() const noexcept { return remainingTicks == 0; }
    };

    struct ChannelState
    {
        double s1 = 0.0, s2 = 0.0;
    };

    void applyRequested (bool snap) noexcept;
    void tick() noexcept;
    void updateCoefficients() noexcept;
    void processSegment (float* data, int numSamples, ChannelState& state) const noexcept;

    BellSettings requested;
    LinearRamp log2Frequency, gainDb, log2Q;
    BiquadCoefficients coefficients;
    std::array<ChannelState, maxChannels> states {};

    double sampleRate = 44100.0;
    int numPreparedChannels = maxChannels;
    int rampTicks = 1;
    int samplesUntilTick = 0;
    bool idle = true;
};
} // namespace labq8::dsp
