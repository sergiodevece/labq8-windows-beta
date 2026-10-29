#include "dsp/EqCore.h"

#include <algorithm>

namespace labq8::dsp
{
void EqCore::prepare (double sampleRate, int numChannels) noexcept
{
    for (auto& band : bands)
        band.prepare (sampleRate, numChannels);
}

void EqCore::reset() noexcept
{
    for (auto& band : bands)
        band.reset();
}

void EqCore::setBandTargets (int bandIndex, const BandSettings& settings) noexcept
{
    if (bandIndex >= 0 && bandIndex < numBands)
        bands[(size_t) bandIndex].setTargets (settings);
}

void EqCore::setBandTargets (int bandIndex, const BellSettings& settings) noexcept
{
    if (bandIndex >= 0 && bandIndex < numBands)
        bands[(size_t) bandIndex].setTargets (settings);
}

void EqCore::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    for (auto& band : bands)
        band.process (channels, numChannels, numSamples);
}

const EqBand& EqCore::getBand (int bandIndex) const noexcept
{
    return bands[(size_t) std::clamp (bandIndex, 0, numBands - 1)];
}

bool EqCore::isIdle() const noexcept
{
    return std::all_of (bands.begin(), bands.end(), [] (const EqBand& b) { return b.isIdle(); });
}

double EqCore::magnitudeAt (double frequencyHz) const noexcept
{
    double magnitude = 1.0;

    for (const auto& band : bands)
        magnitude *= dsp::magnitudeAt (band.getCoefficients(), frequencyHz, band.getSampleRate());

    return magnitude;
}
} // namespace labq8::dsp
