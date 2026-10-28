#pragma once

#include "dsp/BellBand.h"

#include <array>

namespace labq8::dsp
{
// Eight independent Bell bands processed in series (band 1 first).
//
// Each band keeps its own smoothing, coefficient and per-channel state, so
// every BellBand guarantee carries over: no allocation or locks in process(),
// output independent of host block size, bypassed or 0 dB bands are idle and
// leave the buffer untouched. Processing band by band over the whole block is
// bit-identical to a per-sample cascade because each band only sees its own
// input sequence.
class EqCore
{
public:
    static constexpr int numBands    = 8;
    static constexpr int maxChannels = BellBand::maxChannels;

    void prepare (double sampleRate, int numChannels) noexcept;
    void reset() noexcept;

    // Real-time safe. Out-of-range band indices are ignored.
    void setBandTargets (int bandIndex, const BellSettings& settings) noexcept;

    // Real-time safe. Processes in place through all bands.
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    // Band index is clamped to [0, numBands).
    const BellBand& getBand (int bandIndex) const noexcept;

    // True when every band is idle (output == input, bit-exact).
    bool isIdle() const noexcept;

    // Composite |H| of the current (smoothed) coefficients.
    double magnitudeAt (double frequencyHz) const noexcept;

private:
    std::array<BellBand, numBands> bands;
};
} // namespace labq8::dsp
