#pragma once

#include "dsp/BiquadDesign.h"
#include "dsp/EqCore.h"

#include <array>
#include <vector>

namespace iaq8::dsp
{
// Matches the loudness of the preview path to the applied path.
//
// Both path outputs are K-weighted (BS.1770 approximation: high shelf then
// low cut), squared, summed over channels and averaged with an exponential
// window equivalent to a sliding window of windowSeconds (tau = windowSeconds / 2). The compensation is 10*log10(msApplied/msPreview),
// clamped to +-maxCompensationDb and smoothed over smoothingSeconds. When
// either path is below silenceDb the last target is held.
//
// Everything runs per sample on absolute sample positions, so the result does
// not depend on how the host splits the audio into blocks. No allocation, no
// locks: safe on the audio thread after prepare().
class LoudnessMatcher
{
public:
    static constexpr double windowSeconds = 3.0, maxCompensationDb = 6.0, smoothingSeconds = 0.5;
    static constexpr double silenceDb = -70.0; // K-weighted mean square below this: hold
    static constexpr double silenceMeanSquare = 1.0e-7; // 10^(silenceDb / 10)
    static constexpr int maxChannels = labq8::dsp::EqCore::maxChannels;

    void prepare (double sampleRate, int numChannels);

    // K-weighted mean square of a whole signal: same filters as the running
    // meter, power summed over channels, averaged over the length (offline
    // renders). 0 for empty input.
    static double wholeSignalMeanSquare (const std::vector<std::vector<float>>& channels, double sampleRate);

    void push (const float* const* applied, const float* const* preview, int numChannels, int numSamples) noexcept
    {
        push (applied, preview, numChannels, numSamples, nullptr);
    }

    // As above; when gainOut is not null it receives the linear compensation
    // gain after each sample (numSamples values), i.e. the value that
    // getCompensationDb() has at that sample.
    void push (const float* const* applied, const float* const* preview, int numChannels, int numSamples,
               float* gainOut) noexcept;

    double getCompensationDb() const noexcept { return smoothedDb; }
    void reset() noexcept;

    // Forgets the loudness history (mean squares) but keeps the current
    // compensation, so a new preview is measured from fresh data.
    void restartMeasurement() noexcept { meanSquareApplied = meanSquarePreview = 0.0; }

private:
    static void designWeighting (double sampleRate, labq8::dsp::BiquadCoefficients& shelf, labq8::dsp::BiquadCoefficients& cut);

    struct Biquad
    {
        double z1 = 0.0, z2 = 0.0;
        double process (const labq8::dsp::BiquadCoefficients& c, double x) noexcept
        {
            const double y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return y;
        }
    };
    struct PathState
    {
        std::array<Biquad, maxChannels> shelf, cut;
    };

    double weightedPower (PathState& s, const float* const* in, int channels, int i) noexcept;
    void updateTarget() noexcept;

    static constexpr int hop = 32; // target is recomputed every `hop` samples

    labq8::dsp::BiquadCoefficients shelfCoeffs, cutCoeffs;
    PathState appliedState, previewState;
    double meanSquareApplied = 0.0, meanSquarePreview = 0.0;
    double windowAlpha = 0.0, smoothingBeta = 1.0;
    double targetDb = 0.0, smoothedDb = 0.0;
    double silenceLinear = 1.0e-7;
    int hopCounter = 0;
    int channelCount = 0;
};
} // namespace iaq8::dsp
