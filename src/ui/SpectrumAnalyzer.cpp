#include "ui/SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace labq8::ui
{
int SpectrumAnalyzer::fftOrderFor (double sampleRate)
{
    return sampleRate <= 50000.0 ? 13 : sampleRate <= 100000.0 ? 14 : 15;
}

void SpectrumAnalyzer::prepare (double sampleRate, const std::vector<double>& pointFrequencies, int fftOrder)
{
    rate = sampleRate;
    const auto order = fftOrder > 0 ? fftOrder : fftOrderFor (sampleRate);
    size = 1 << order;
    fft = std::make_unique<juce::dsp::FFT> (order);

    // Periodic 4-term Blackman-Harris.
    window.resize ((size_t) size);
    windowSum = 0.0;
    for (int n = 0; n < size; ++n)
    {
        const auto t = 2.0 * juce::MathConstants<double>::pi * n / size;
        const auto w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2.0 * t) - 0.01168 * std::cos (3.0 * t);
        window[(size_t) n] = (float) w;
        windowSum += w;
    }
    work.assign ((size_t) size * 2, 0.0f);
    power.assign ((size_t) size / 2 + 1, 0.0f);
    binsDb.assign ((size_t) size / 2 + 1, (float) restDb);

    // Each point's band: from the geometric middle with the previous point to
    // the one with the next.
    const auto binHz = sampleRate / size;
    points.assign (pointFrequencies.size(), {});
    for (size_t i = 0; i < pointFrequencies.size(); ++i)
    {
        const auto f = pointFrequencies[i];
        const auto previous = i > 0 ? pointFrequencies[i - 1] : f * f / pointFrequencies[std::min (i + 1, pointFrequencies.size() - 1)];
        const auto next = i + 1 < pointFrequencies.size() ? pointFrequencies[i + 1] : f * f / previous;
        auto& p = points[i];
        p.valid = f < 0.497 * sampleRate;
        p.first = (int) std::ceil (std::sqrt (previous * f) / binHz);
        p.last = std::min ((int) std::floor (std::sqrt (f * next) / binHz), size / 2);
        p.position = (float) (f / binHz);
    }
}

void SpectrumAnalyzer::analyse (const float* const* channels, int numChannels)
{
    std::fill (power.begin(), power.end(), 0.0f);
    const auto scale = 2.0 / windowSum; // |X| of a sine of amplitude A at its bin: A * windowSum / 2
    for (int ch = 0; ch < numChannels; ++ch)
    {
        for (int n = 0; n < size; ++n)
            work[(size_t) n] = channels[ch][n] * window[(size_t) n];
        std::fill (work.begin() + size, work.end(), 0.0f);
        fft->performRealOnlyForwardTransform (work.data(), true);
        for (int k = 0; k <= size / 2; ++k)
        {
            const auto re = (double) work[(size_t) (2 * k)] * scale, im = (double) work[(size_t) (2 * k + 1)] * scale;
            power[(size_t) k] += (float) ((re * re + im * im) / numChannels);
        }
    }
    for (size_t k = 0; k < power.size(); ++k)
        binsDb[k] = power[k] > 0.0f ? (float) std::max (restDb, 10.0 * std::log10 ((double) power[k])) : (float) restDb;
}

void SpectrumAnalyzer::toDisplay (std::vector<float>& pointsDb) const
{
    pointsDb.resize (points.size());
    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto& p = points[i];
        if (! p.valid)
        {
            pointsDb[i] = (float) restDb;
            continue;
        }
        if (p.last >= p.first)
            pointsDb[i] = *std::max_element (binsDb.begin() + p.first, binsDb.begin() + p.last + 1);
        else
        {
            const auto k = std::min ((int) p.position, size / 2 - 1);
            const auto t = p.position - (float) k;
            pointsDb[i] = binsDb[(size_t) k] + t * (binsDb[(size_t) k + 1] - binsDb[(size_t) k]);
        }
    }
}

bool SpectrumBallistics::update (const std::vector<float>* analysed, size_t numPoints, double seconds)
{
    if (levelDb.size() != numPoints)
        reset (numPoints);
    const auto fall = (float) (fallDbPerSecond * std::max (0.0, seconds));
    bool moved = false;
    for (size_t i = 0; i < numPoints; ++i)
    {
        const auto previous = levelDb[i];
        auto level = std::max ((float) SpectrumAnalyzer::restDb, previous - fall);
        if (analysed != nullptr)
            level = std::max (level, (*analysed)[i]);
        levelDb[i] = level;
        // Changes below the floor are invisible.
        moved = moved || (! juce::exactlyEqual (level, previous) && std::max (level, previous) > (float) SpectrumAnalyzer::floorDb);
    }
    return moved;
}

bool SpectrumBallistics::atRest() const noexcept
{
    return std::all_of (levelDb.begin(), levelDb.end(), [] (float v) { return v <= (float) SpectrumAnalyzer::floorDb; });
}

void SpectrumBallistics::reset (size_t numPoints)
{
    levelDb.assign (numPoints, (float) SpectrumAnalyzer::restDb);
}
} // namespace labq8::ui
