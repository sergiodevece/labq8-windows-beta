#include "dsp/EqCore.h"

#include <algorithm>

namespace labq8::dsp
{
void EqCore::prepare (double sampleRate, int numChannels) noexcept
{
    for (auto& band : bands)
        band.prepare (sampleRate, numChannels);

    for (auto& section : dynamics)
        section.prepare (sampleRate, numChannels);

    reset();
}

void EqCore::reset() noexcept
{
    for (auto& band : bands)
    {
        band.setGainModulationDb (0.0);
        band.reset();
    }

    for (auto& section : dynamics)
        section.reset();

    samplesUntilTick = 0;
}

void EqCore::updateEngaged (int bandIndex, const BandSettings& settings) noexcept
{
    dynamics[(size_t) bandIndex].setEngaged (filterTypeFromIndex (static_cast<int> (settings.type)) == FilterType::bell
                                             && ! settings.bypassed);
}

void EqCore::setBandTargets (int bandIndex, const BandSettings& settings) noexcept
{
    if (bandIndex >= 0 && bandIndex < numBands)
    {
        bands[(size_t) bandIndex].setTargets (settings);
        updateEngaged (bandIndex, settings);
    }
}

void EqCore::setBandTargets (int bandIndex, const BellSettings& settings) noexcept
{
    if (bandIndex >= 0 && bandIndex < numBands)
    {
        bands[(size_t) bandIndex].setTargets (settings);
        updateEngaged (bandIndex, BandSettings { settings.frequencyHz, settings.gainDb, settings.q, settings.bypassed, FilterType::bell });
    }
}

void EqCore::setBandDynamics (int bandIndex, const DynamicSettings& settings) noexcept
{
    if (bandIndex >= 0 && bandIndex < numBands)
        dynamics[(size_t) bandIndex].setTargets (settings);
}

void EqCore::setDetectorLink (DetectorLink link) noexcept
{
    for (auto& section : dynamics)
        section.setDetectorLink (link);
}

bool EqCore::hasActiveDynamics() const noexcept
{
    return std::any_of (dynamics.begin(), dynamics.end(), [] (const DynamicSection& d) { return d.isActive(); });
}

void EqCore::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numSamples <= 0)
        return;

    if (hasActiveDynamics())
    {
        processDynamic (channels, numChannels, numSamples);
        return;
    }

    // Static path (E2a code): band by band over the whole block.
    for (auto& band : bands)
        band.process (channels, numChannels, numSamples);

    // Keep the grid counter in step with the bands (EqBand: a tick happens
    // when the counter is 0 at the start of a segment, then it restarts at 16).
    if (numSamples <= samplesUntilTick)
    {
        samplesUntilTick -= numSamples;
    }
    else
    {
        const auto remainder = (numSamples - samplesUntilTick) % tickInterval;
        samplesUntilTick = remainder == 0 ? 0 : tickInterval - remainder;
    }
}

void EqCore::processDynamic (float* const* channels, int numChannels, int numSamples) noexcept
{
    const auto activeChannels = std::clamp (numChannels, 0, EqBand::maxChannels);
    float* segment[EqBand::maxChannels] {};

    for (int position = 0; position < numSamples;)
    {
        if (samplesUntilTick == 0)
        {
            // Every section, active or not (an inactive one returns 0 at once),
            // so a band whose dynamics just finished fading out gets exactly 0.
            for (int b = 0; b < numBands; ++b)
            {
                auto& band = bands[(size_t) b];
                band.setGainModulationDb (dynamics[(size_t) b].tick (band.getCurrentFrequencyHz(), band.getCurrentQ()));
            }
            samplesUntilTick = tickInterval;
        }

        const auto n = std::min (samplesUntilTick, numSamples - position);

        // 2. Detection on the original input of this segment, before any band.
        for (auto& section : dynamics)
            section.processDetector (channels, activeChannels, position, n);

        // 3. The cascade, in place.
        for (int ch = 0; ch < activeChannels; ++ch)
            segment[ch] = channels[ch] != nullptr ? channels[ch] + position : nullptr;

        for (auto& band : bands)
            band.process (segment, activeChannels, n);

        samplesUntilTick -= n;
        position += n;
    }
}

const EqBand& EqCore::getBand (int bandIndex) const noexcept
{
    return bands[(size_t) std::clamp (bandIndex, 0, numBands - 1)];
}

const DynamicSection& EqCore::getDynamics (int bandIndex) const noexcept
{
    return dynamics[(size_t) std::clamp (bandIndex, 0, numBands - 1)];
}

bool EqCore::isIdle() const noexcept
{
    return ! hasActiveDynamics()
        && std::all_of (bands.begin(), bands.end(), [] (const EqBand& b) { return b.isIdle(); });
}

double EqCore::magnitudeAt (double frequencyHz) const noexcept
{
    double magnitude = 1.0;

    for (const auto& band : bands)
        magnitude *= dsp::magnitudeAt (band.getCoefficients(), frequencyHz, band.getSampleRate());

    return magnitude;
}
} // namespace labq8::dsp
