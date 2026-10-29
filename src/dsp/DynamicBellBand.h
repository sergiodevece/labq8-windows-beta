#pragma once

#include "dsp/DynamicSection.h"
#include "dsp/EqBand.h"

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

// One dynamic Bell band (E2a prototype, kept for single-band tests): a static
// EqBand (type Bell) whose gain is reduced by a DynamicSection. EqCore uses
// the same DynamicSection for its eight bands (E2b).
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
    static_assert (DynamicSection::controlIntervalSamples == EqBand::updateIntervalSamples);

    DynamicBellBand() noexcept;

    void prepare (double sampleRate, int numChannels) noexcept;
    void reset() noexcept;

    // Real-time safe. Values are clamped to dynamics::limits and the band limits.
    void setTargets (const DynamicBellSettings& settings) noexcept;
    void setDetectorLink (DetectorLink link) noexcept { section.setDetectorLink (link); }

    // Real-time safe, in place.
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    double getDetectorLevelDb() const noexcept { return section.getDetectorLevelDb(); }
    // Reduction applied at the last tick (dB, >= 0).
    double getGainReductionDb() const noexcept { return section.getGainReductionDb(); }
    const EqBand& getBand() const noexcept { return band; }
    double getSampleRate() const noexcept { return sampleRate; }

private:
    EqBand band;
    DynamicSection section;
    DynamicBellSettings requested;

    double sampleRate = 44100.0;
    int samplesUntilTick = 0;
};
} // namespace labq8::dsp
