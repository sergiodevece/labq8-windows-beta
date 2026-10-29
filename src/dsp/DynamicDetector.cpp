#include "dsp/DynamicDetector.h"

#include <algorithm>
#include <cmath>

namespace labq8::dsp
{
double DynamicDetector::windowSeconds (double frequencyHz) noexcept
{
    return std::clamp (0.5 / frequencyHz, minWindowSeconds, maxWindowSeconds);
}

void DynamicDetector::prepare (double newSampleRate, int numChannels) noexcept
{
    sampleRate = sanitiseSampleRate (newSampleRate);
    numPreparedChannels = std::clamp (numChannels, 0, maxChannels);
    regionFrequency = regionQ = -1.0; // force a redesign at the new rate
    setRegion (limits::defaultFrequencyHz, limits::defaultQ);
    reset();
}

void DynamicDetector::reset() noexcept
{
    filters.fill ({});
    meanSquare.fill (0.0);
}

void DynamicDetector::setRegion (double frequencyHz, double q) noexcept
{
    frequencyHz = clampFrequency (frequencyHz, sampleRate);
    q = clampQ (q);

    if (frequencyHz == regionFrequency && q == regionQ)
        return;

    regionFrequency = frequencyHz;
    regionQ = q;
    bandPass = designBandPass (sampleRate, frequencyHz, q);
    windowCoefficient = 1.0 - std::exp (-1.0 / (windowSeconds (frequencyHz) * sampleRate));
}

double DynamicDetector::filter (ChannelFilter& f, double x) const noexcept
{
    // Two TDF-II sections with the same band-pass (b1 == 0).
    const auto& c = bandPass;
    const auto y1 = c.b0 * x + f.first.s1;
    f.first.s1 = -c.a1 * y1 + f.first.s2;
    f.first.s2 = c.b2 * x - c.a2 * y1;

    const auto y2 = c.b0 * y1 + f.second.s1;
    f.second.s1 = -c.a1 * y2 + f.second.s2;
    f.second.s2 = c.b2 * y1 - c.a2 * y2;
    return y2;
}

void DynamicDetector::process (const float* const* channels, int numChannels, int start, int numSamples) noexcept
{
    const auto active = std::min ({ numChannels, numPreparedChannels, maxChannels });
    if (channels == nullptr || active <= 0 || numSamples <= 0)
        return;

    auto sample = [&] (int ch, int i)
    {
        if (channels[ch] == nullptr)
            return 0.0;
        const double x = channels[ch][start + i];
        return std::isfinite (x) ? x : 0.0;
    };

    const auto c = windowCoefficient;
    auto ms0 = meanSquare[0], ms1 = meanSquare[1];

    for (int i = 0; i < numSamples; ++i)
    {
        double power = 0.0;

        if (link == DetectorLink::mono || active == 1)
        {
            double x = 0.0;
            for (int ch = 0; ch < active; ++ch)
                x += sample (ch, i);
            const auto y = filter (filters[0], x / active);
            power = y * y;
        }
        else
        {
            for (int ch = 0; ch < active; ++ch)
            {
                const auto y = filter (filters[(size_t) ch], sample (ch, i));
                power = link == DetectorLink::maxLinked ? std::max (power, y * y) : power + y * y;
            }
            if (link == DetectorLink::energyLinked)
                power /= active;
        }

        ms0 += c * (power - ms0);
        ms1 += c * (ms0 - ms1);
    }

    meanSquare = { ms0, ms1 };
}

void DynamicDetector::sanitise() noexcept
{
    auto clean = [] (double& v)
    {
        if (! std::isfinite (v) || std::abs (v) < denormalFlushThreshold)
            v = 0.0;
    };

    for (auto& f : filters)
        for (auto* section : { &f.first, &f.second })
        {
            clean (section->s1);
            clean (section->s2);
        }

    for (auto& v : meanSquare)
        clean (v);
}

double DynamicDetector::getLevelDb() const noexcept
{
    const auto twoMs = 2.0 * meanSquare[1];
    return twoMs > 0.0 ? std::max (levelFloorDb, 10.0 * std::log10 (twoMs)) : levelFloorDb;
}
} // namespace labq8::dsp
