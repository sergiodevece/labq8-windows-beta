#include "dsp/AssistEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace iaq8::dsp
{
void AssistEngine::prepare (double sampleRate, int numChannels, int maxBlockSize)
{
    channelCount = std::clamp (numChannels, 1, maxChannels);
    maxBlock = std::max (1, maxBlockSize);
    appliedCore.prepare (sampleRate, channelCount);
    previewCore.prepare (sampleRate, channelCount);
    scratch.assign ((size_t) (2 * channelCount * maxBlock), 0.0f);
    fadeSamples = std::max (1, (int) std::lround (fadeSeconds * sampleRate));
    fadePos = (listen == Listen::preview && previewActive) ? 1.0f : 0.0f;
    fadeStep = 0.0f;
    applyTo (appliedCore, appliedSet);
    applyTo (previewCore, previewActive ? previewSet : appliedSet);
}

void AssistEngine::applyTo (labq8::dsp::EqCore& core, const BandSet& bands) noexcept
{
    for (size_t i = 0; i < bands.size(); ++i)
        core.setBandTargets ((int) i, bands[i]);
}

void AssistEngine::setApplied (const BandSet& bands) noexcept
{
    appliedSet = bands;
    applyTo (appliedCore, appliedSet);
    if (! previewActive)
        applyTo (previewCore, appliedSet); // keeps the idle preview path in step
}

void AssistEngine::setPreview (const BandSet& bands) noexcept
{
    previewSet = bands;
    previewActive = true;
    applyTo (previewCore, previewSet);
}

void AssistEngine::clearPreview() noexcept
{
    previewActive = false;
    applyTo (previewCore, appliedSet);
}

void AssistEngine::setListen (Listen newListen) noexcept
{
    listen = newListen;
}

void AssistEngine::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channelCount == 0 || numChannels <= 0 || numSamples <= 0)
        return; // not prepared, or nothing to do

    const auto nCh = std::min (numChannels, channelCount);
    for (int pos = 0; pos < numSamples; pos += maxBlock)
    {
        float* chunk[maxChannels];
        for (int c = 0; c < nCh; ++c)
            chunk[c] = channels[c] + pos;
        processChunk (chunk, nCh, std::min (maxBlock, numSamples - pos));
    }
}

void AssistEngine::processChunk (float* const* channels, int nCh, int n) noexcept
{
    float* a[maxChannels];
    float* p[maxChannels];
    for (int c = 0; c < nCh; ++c)
    {
        a[c] = scratch.data() + (size_t) c * (size_t) maxBlock;
        p[c] = scratch.data() + (size_t) (channelCount + c) * (size_t) maxBlock;
        std::memcpy (a[c], channels[c], sizeof (float) * (size_t) n);
        std::memcpy (p[c], channels[c], sizeof (float) * (size_t) n);
    }
    appliedCore.process (a, nCh, n);
    previewCore.process (p, nCh, n);

    const bool wantPreview = listen == Listen::preview && previewActive;
    const auto target = wantPreview ? 1.0f : 0.0f;
    fadeStep = fadePos < target ? 1.0f / (float) fadeSamples
             : fadePos > target ? -1.0f / (float) fadeSamples : 0.0f;

    for (int i = 0; i < n; ++i)
    {
        if (fadeStep > 0.0f)
            fadePos = std::min (target, fadePos + fadeStep);
        else if (fadeStep < 0.0f)
            fadePos = std::max (target, fadePos + fadeStep);
        if ((fadeStep > 0.0f && fadePos >= target) || (fadeStep < 0.0f && fadePos <= target))
            fadeStep = 0.0f;

        for (int c = 0; c < nCh; ++c)
        {
            if (fadePos <= 0.0f)
                channels[c][i] = a[c][i];
            else if (fadePos >= 1.0f)
                channels[c][i] = p[c][i] * compensationGain;
            else
                channels[c][i] = a[c][i] * (1.0f - fadePos) + p[c][i] * compensationGain * fadePos;
        }
    }
}
} // namespace iaq8::dsp
