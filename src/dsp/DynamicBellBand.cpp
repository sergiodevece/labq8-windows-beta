#include "dsp/DynamicBellBand.h"

#include <algorithm>

namespace labq8::dsp
{
DynamicBellBand::DynamicBellBand() noexcept
{
    prepare (sampleRate, maxChannels);
}

void DynamicBellBand::prepare (double newSampleRate, int numChannels) noexcept
{
    sampleRate = sanitiseSampleRate (newSampleRate);
    band.prepare (sampleRate, numChannels);
    section.prepare (sampleRate, numChannels);
    reset();
}

void DynamicBellBand::reset() noexcept
{
    band.setGainModulationDb (0.0);
    band.reset();
    section.reset();
    samplesUntilTick = 0;
}

void DynamicBellBand::setTargets (const DynamicBellSettings& settings) noexcept
{
    requested = settings;
    band.setTargets (BandSettings { settings.frequencyHz, settings.staticGainDb, settings.q, false, FilterType::bell });
    section.setTargets (DynamicSettings { settings.dynamicEnabled, settings.thresholdDb, settings.rangeDb,
                                          settings.attackMs, settings.releaseMs });
}

void DynamicBellBand::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numSamples <= 0)
        return;

    const auto activeChannels = std::clamp (numChannels, 0, maxChannels);
    float* segment[maxChannels] {};

    for (int position = 0; position < numSamples;)
    {
        if (samplesUntilTick == 0)
        {
            band.setGainModulationDb (section.tick (band.getCurrentFrequencyHz(), band.getCurrentQ()));
            samplesUntilTick = controlIntervalSamples;
        }

        const auto n = std::min (samplesUntilTick, numSamples - position);

        // Detection reads the input before the band filters it in place.
        section.processDetector (channels, activeChannels, position, n);

        for (int ch = 0; ch < activeChannels; ++ch)
            segment[ch] = channels[ch] != nullptr ? channels[ch] + position : nullptr;

        band.process (segment, activeChannels, n);

        samplesUntilTick -= n;
        position += n;
    }
}
} // namespace labq8::dsp
