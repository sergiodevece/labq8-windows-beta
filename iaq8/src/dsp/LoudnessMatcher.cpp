#include "dsp/LoudnessMatcher.h"

#include <algorithm>
#include <cmath>

namespace iaq8::dsp
{
void LoudnessMatcher::prepare (double sampleRate, int numChannels)
{
    const auto fs = labq8::dsp::sanitiseSampleRate (sampleRate);
    channelCount = std::clamp (numChannels, 1, maxChannels);
    designWeighting (fs, shelfCoeffs, cutCoeffs);
    windowAlpha = std::exp (-2.0 / (windowSeconds * fs)) /* tau = window / 2 */;
    smoothingBeta = 1.0 - std::exp (-1.0 / (smoothingSeconds * fs));
    silenceLinear = std::pow (10.0, silenceDb / 10.0);
    reset();
}

void LoudnessMatcher::designWeighting (double fs, labq8::dsp::BiquadCoefficients& shelf, labq8::dsp::BiquadCoefficients& cut)
{
    shelf = labq8::dsp::designHighShelf (fs, 1681.97, 3.9998, 0.7071);
    cut = labq8::dsp::designLowCut (fs, 38.135, 0.5003);
}

double LoudnessMatcher::wholeSignalMeanSquare (const std::vector<std::vector<float>>& channels, double sampleRate)
{
    labq8::dsp::BiquadCoefficients shelf, cut;
    designWeighting (labq8::dsp::sanitiseSampleRate (sampleRate), shelf, cut);
    size_t length = 0;
    for (const auto& ch : channels)
        length = std::max (length, ch.size());
    if (length == 0)
        return 0.0;
    double sum = 0.0;
    for (const auto& ch : channels)
    {
        Biquad a, b;
        for (const auto v : ch)
        {
            const auto y = b.process (cut, a.process (shelf, (double) v));
            sum += y * y;
        }
    }
    return sum / (double) length;
}

void LoudnessMatcher::reset() noexcept
{
    appliedState = {};
    previewState = {};
    meanSquareApplied = meanSquarePreview = 0.0;
    targetDb = smoothedDb = 0.0;
    hopCounter = 0;
}

double LoudnessMatcher::weightedPower (PathState& s, const float* const* in, int channels, int i) noexcept
{
    double sum = 0.0;
    for (int c = 0; c < channels; ++c)
    {
        const auto y = s.cut[(size_t) c].process (cutCoeffs, s.shelf[(size_t) c].process (shelfCoeffs, (double) in[c][i]));
        sum += y * y;
    }
    return sum;
}

void LoudnessMatcher::updateTarget() noexcept
{
    if (meanSquareApplied < silenceLinear || meanSquarePreview < silenceLinear)
        return; // hold the last target
    targetDb = std::clamp (10.0 * std::log10 (meanSquareApplied / meanSquarePreview),
                           -maxCompensationDb, maxCompensationDb);
}

void LoudnessMatcher::push (const float* const* applied, const float* const* preview, int numChannels,
                            int numSamples, float* gainOut) noexcept
{
    if (channelCount == 0 || numChannels <= 0 || numSamples <= 0)
        return;
    const auto nCh = std::min (numChannels, channelCount);
    constexpr double dbToNeper = 0.115129254649702284; // ln(10) / 20

    for (int i = 0; i < numSamples; ++i)
    {
        const auto pa = weightedPower (appliedState, applied, nCh, i);
        const auto pp = weightedPower (previewState, preview, nCh, i);
        meanSquareApplied = windowAlpha * meanSquareApplied + (1.0 - windowAlpha) * pa;
        meanSquarePreview = windowAlpha * meanSquarePreview + (1.0 - windowAlpha) * pp;
        if (++hopCounter >= hop)
        {
            hopCounter = 0;
            updateTarget();
        }
        smoothedDb += (targetDb - smoothedDb) * smoothingBeta;
        if (gainOut != nullptr)
            gainOut[i] = (float) std::exp (smoothedDb * dbToNeper);
    }
}
} // namespace iaq8::dsp
