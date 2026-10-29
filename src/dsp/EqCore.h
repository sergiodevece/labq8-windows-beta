#pragma once

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

    // Real-time safe. Processes in place through all bands.
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    // Band index is clamped to [0, numBands).
    const EqBand& getBand (int bandIndex) const noexcept;

    // True when every band is idle (output == input, bit-exact).
    bool isIdle() const noexcept;

    // Composite |H| of the active paths' current (smoothed) coefficients.
    // During a crossfade the outgoing path is not included.
    double magnitudeAt (double frequencyHz) const noexcept;

private:
    std::array<EqBand, numBands> bands;
};
} // namespace labq8::dsp
