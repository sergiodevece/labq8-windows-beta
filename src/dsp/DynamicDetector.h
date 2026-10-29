#pragma once

#include "dsp/BiquadDesign.h"

#include <array>

namespace labq8::dsp
{
// How the channels of a multichannel input feed one detector.
enum class DetectorLink
{
    mono,         // average of the channels, filtered once
    maxLinked,    // each channel filtered; the louder channel drives (default)
    energyLinked, // each channel filtered; mean energy of the channels drives
};

// Level detector of the dynamic engine (E2a). No JUCE, no allocation.
//
// Detection signal: the band input (pre-EQ), never the band output.
//
// Region: two cascaded RBJ band-pass filters (designBandPass, constant 0 dB
// peak) at the band frequency and Q: 4th order, exactly 0 dB at f0 for every
// f0 and Q, 12 dB/oct skirts.
//
// Level: mean square of the band-passed signal, smoothed by two cascaded
// one-pole filters with time constant windowSeconds(f0) = 0.5 / f0 (clamped
// to [0.1, 25] ms): about one period of f0, enough to remove the 2 f0 ripple
// of a sine (+/-0.1 dB) without blurring transients more than necessary.
//
// Scale: getLevelDb() = 10 log10(2 * mean square), i.e. dBFS of a sine with
// that RMS ("sine-peak dBFS": a full-scale sine at f0 reads 0 dBFS, a sine of
// amplitude A at f0 reads 20 log10(A)). Floor: levelFloorDb (silence).
//
// Attack and release are not here: they act on the gain reduction
// (dynamics::GainBallistics), so the detector stays a pure level meter.
class DynamicDetector
{
public:
    static constexpr int maxChannels = 2;
    static constexpr double levelFloorDb = -160.0;
    static constexpr double minWindowSeconds = 1.0e-4;
    static constexpr double maxWindowSeconds = 0.025;
    static constexpr double denormalFlushThreshold = 1.0e-30;

    static double windowSeconds (double frequencyHz) noexcept;

    void prepare (double sampleRate, int numChannels) noexcept;
    void reset() noexcept;

    void setLink (DetectorLink newLink) noexcept { link = newLink; }
    DetectorLink getLink() const noexcept { return link; }

    // Real-time safe. Recomputes the filters only when f0 or Q change.
    void setRegion (double frequencyHz, double q) noexcept;

    // Real-time safe. Reads channels[ch][start .. start + numSamples).
    // Non-finite samples count as 0.
    void process (const float* const* channels, int numChannels, int start, int numSamples) noexcept;

    // Clears non-finite state and flushes values below denormalFlushThreshold.
    // Called once per control tick by the owner.
    void sanitise() noexcept;

    double getMeanSquare() const noexcept { return meanSquare[1]; }
    double getLevelDb() const noexcept;

private:
    struct Section
    {
        double s1 = 0.0, s2 = 0.0;
    };

    struct ChannelFilter
    {
        Section first, second;
    };

    double filter (ChannelFilter& f, double x) const noexcept;

    BiquadCoefficients bandPass;
    std::array<ChannelFilter, maxChannels> filters {};
    std::array<double, 2> meanSquare {};
    double windowCoefficient = 1.0;
    double sampleRate = 44100.0, regionFrequency = -1.0, regionQ = -1.0;
    int numPreparedChannels = maxChannels;
    DetectorLink link = DetectorLink::maxLinked;
};
} // namespace labq8::dsp
