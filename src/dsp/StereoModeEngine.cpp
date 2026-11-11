#include "dsp/StereoModeEngine.h"

#include "dsp/MidSide.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace labq8::dsp
{
void StereoModeEngine::prepare (double sampleRate, int numChannels, double fadeSeconds) noexcept
{
    numPreparedChannels = std::clamp (numChannels, 0, maxChannels);
    core (StereoBank::stereo).prepare (sampleRate, numChannels);
    core (StereoBank::mid).prepare (sampleRate, 1);
    core (StereoBank::side).prepare (sampleRate, 1);

    const auto rate = std::isfinite (sampleRate) && sampleRate > 0.0 ? sampleRate : 44100.0;
    fadeLength = std::max (1, static_cast<int> (std::lround (fadeSeconds * rate)));
    reset();
}

void StereoModeEngine::reset() noexcept
{
    for (auto& c : cores)
        c.reset();

    heardMode = requestedMode;
    fading = false;
    fadePosition = 0;
    flatGridPosition = 0;
    midSideFlat = true;
}

void StereoModeEngine::setBandTargets (StereoBank bank, int bandIndex, const BandSettings& settings) noexcept
{
    core (bank).setBandTargets (bandIndex, settings);
}

void StereoModeEngine::setBandDynamics (StereoBank bank, int bandIndex, const DynamicSettings& settings) noexcept
{
    core (bank).setBandDynamics (bandIndex, settings);
}

const EqCore& StereoModeEngine::getCore (StereoBank bank) const noexcept
{
    return cores[(size_t) bank];
}

void StereoModeEngine::processMidSide (float* const* channels, int numChannels, int numSamples) noexcept
{
    auto& mid = core (StereoBank::mid);
    auto& side = core (StereoBank::side);

    if (numChannels == 1)
    {
        // Mono bus: M is the signal, S is 0. The MID core works in place
        // (an idle core leaves it untouched); the SIDE bank has no input.
        mid.process (channels, 1, numSamples);
        midSideFlat = mid.isIdle();
        return;
    }

    float* m[1] = { midBuffer.data() };
    float* s[1] = { sideBuffer.data() };

    for (int position = 0; position < numSamples;)
    {
        // Enter the flat path only on the grid; leave it at once.
        const bool bothIdle = mid.isIdle() && side.isIdle();
        if (flatGridPosition == 0)
            midSideFlat = bothIdle;
        else if (midSideFlat && ! bothIdle)
            midSideFlat = false;

        const auto n = std::min (numSamples - position, flatGridSamples - flatGridPosition);
        auto* left = channels[0] + position;
        auto* right = channels[1] + position;

        if (midSideFlat)
        {
            // Idle cores neither read nor write their buffer; they only
            // advance their control grid, which must stay on time.
            mid.process (m, 1, n);
            side.process (s, 1, n);
        }
        else
        {
            midside::encode (left, right, m[0], s[0], n);
            mid.process (m, 1, n);
            side.process (s, 1, n);
            midside::decode (m[0], s[0], left, right, n);
        }

        flatGridPosition = (flatGridPosition + n) % flatGridSamples;
        position += n;
    }
}

void StereoModeEngine::processPath (StereoMode mode, float* const* channels, int numChannels, int numSamples) noexcept
{
    if (mode == StereoMode::stereo)
        core (StereoBank::stereo).process (channels, numChannels, numSamples);
    else
        processMidSide (channels, numChannels, numSamples);
}

void StereoModeEngine::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numSamples <= 0)
        return;

    const auto activeChannels = std::min ({ numChannels, numPreparedChannels, maxChannels });
    if (activeChannels <= 0)
        return;
    for (int ch = 0; ch < activeChannels; ++ch)
        if (channels[ch] == nullptr)
            return;

    float* segment[maxChannels] {};
    float* copied[maxChannels] { copy[0].data(), copy[1].data() };

    for (int position = 0; position < numSamples;)
    {
        // A request is honoured at the next sample: a new fade, or the
        // current one reversed in place.
        if (requestedMode != heardMode && ! fading)
        {
            fading = true;
            fadePosition = 0;
        }
        else if (requestedMode == heardMode && fading)
        {
            heardMode = other (heardMode);
            fadePosition = fadeLength - fadePosition;
        }

        auto n = std::min (numSamples - position, chunkSamples);
        if (fading)
            n = std::min (n, fadeLength - fadePosition);

        for (int ch = 0; ch < activeChannels; ++ch)
        {
            segment[ch] = channels[ch] + position;
            std::memcpy (copied[ch], segment[ch], sizeof (float) * (size_t) n);
        }

        // The heard path in place (exactly the pre-E4b call in Stereo), the
        // other one on the copy: standby, or the incoming path of a fade.
        processPath (heardMode, segment, activeChannels, n);
        processPath (other (heardMode), copied, activeChannels, n);

        if (fading)
        {
            for (int ch = 0; ch < activeChannels; ++ch)
            {
                auto* out = segment[ch];
                const auto* in = copied[ch];
                for (int i = 0; i < n; ++i)
                {
                    const auto w = static_cast<double> (fadePosition + i + 1) / fadeLength;
                    const auto a = static_cast<double> (out[i]);
                    out[i] = static_cast<float> (a + w * (static_cast<double> (in[i]) - a));
                }
            }

            fadePosition += n;
            if (fadePosition >= fadeLength)
            {
                heardMode = other (heardMode);
                fading = false;
                fadePosition = 0;
            }
        }

        position += n;
    }
}
} // namespace labq8::dsp
