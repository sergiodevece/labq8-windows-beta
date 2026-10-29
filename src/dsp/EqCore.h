#pragma once

#include "dsp/DynamicSection.h"
#include "dsp/EqBand.h"

#include <array>

namespace labq8::dsp
{
// Eight independent bands (EqBand: six topologies each) processed in series,
// band 1 first.
//
// Each band keeps its own smoothing, crossfade, coefficient and per-channel
// state, so every band guarantee carries over: no allocation or locks in
// process(), output independent of host block size, idle bands leave the
// buffer untouched. Processing band by band over the whole block is
// bit-identical to a per-sample cascade because each band only sees its own
// input sequence. With every band set to Bell this is E1a's EqCore, bit for bit.
//
// Dynamics (E2b). Each band has its own DynamicSection (detector, threshold,
// range, attack, release, enable, envelope). It acts only while the band is
// a Bell and not bypassed; otherwise its reduction fades out in 20 ms.
//
// Common original signal. All eight detectors must read the core INPUT, not
// the output of earlier bands, and processing is in place. While any section
// is active, process() works in segments of at most 16 samples aligned with
// the bands' absolute control grid:
//   1. at a tick boundary, every section computes its modulation and hands
//      it to its band (before the band's own tick in step 3);
//   2. every active detector reads the segment, still untouched;
//   3. bands 1..8 filter the segment in place, in cascade.
// The segment is the only storage needed (the buffer itself), so there is no
// copy, no allocation and no latency; detection is per sample, so results do
// not depend on the host block size. With no section active, process() is
// the E2a code path (band by band over the whole block) and only advances the
// grid counter, so the static path is unchanged.
class EqCore
{
public:
    static constexpr int numBands    = 8;
    static constexpr int maxChannels = EqBand::maxChannels;

    void prepare (double sampleRate, int numChannels) noexcept;
    void reset() noexcept;

    // Real-time safe. Out-of-range band indices are ignored.
    void setBandTargets (int bandIndex, const BandSettings& settings) noexcept;
    // E1a interface: a Bell band.
    void setBandTargets (int bandIndex, const BellSettings& settings) noexcept;

    // Real-time safe. Internal dynamics of one band (no host parameter yet).
    void setBandDynamics (int bandIndex, const DynamicSettings& settings) noexcept;
    // Stereo link of every detector (default: maxLinked).
    void setDetectorLink (DetectorLink link) noexcept;

    // Real-time safe. Processes in place through all bands.
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    // Band index is clamped to [0, numBands).
    const EqBand& getBand (int bandIndex) const noexcept;
    const DynamicSection& getDynamics (int bandIndex) const noexcept;
    bool hasActiveDynamics() const noexcept;

    // True when every band is idle (output == input, bit-exact).
    bool isIdle() const noexcept;

    // Composite |H| of the active paths' current (smoothed) coefficients.
    // During a crossfade the outgoing path is not included.
    double magnitudeAt (double frequencyHz) const noexcept;

private:
    static_assert (DynamicSection::controlIntervalSamples == EqBand::updateIntervalSamples);
    static_assert (DynamicSection::maxChannels == EqBand::maxChannels);
    static constexpr int tickInterval = EqBand::updateIntervalSamples;

    void processDynamic (float* const* channels, int numChannels, int numSamples) noexcept;
    void updateEngaged (int bandIndex, const BandSettings& settings) noexcept;

    std::array<EqBand, numBands> bands;
    std::array<DynamicSection, numBands> dynamics;
    int samplesUntilTick = 0; // mirrors every band's control grid
};
} // namespace labq8::dsp
