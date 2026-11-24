#pragma once

#include "dsp/EqCore.h"

#include <array>
#include <vector>

namespace iaq8::dsp
{
using BandSet = std::array<labq8::dsp::BandSettings, 8>;

enum class Listen { applied, preview };

// Two LABQ8 EqCores side by side: the applied path and the preview path.
// Both always process their own copy of the input (scratch buffers are
// allocated in prepare only), so the inactive path stays warm and a later
// loudness matcher can measure both outputs every block. The A/B switch is a
// 20 ms per-sample linear crossfade; the compensation gain applies to the
// preview path only (0 dB until the loudness matcher exists).
class AssistEngine
{
public:
    static constexpr double fadeSeconds = 0.020;
    static constexpr int maxChannels = labq8::dsp::EqCore::maxChannels;

    void prepare (double sampleRate, int numChannels, int maxBlockSize);

    void setApplied (const BandSet& bands) noexcept;
    void setPreview (const BandSet& bands) noexcept;
    void clearPreview() noexcept; // Listen::preview then plays applied
    void setListen (Listen listen) noexcept;

    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    bool hasPreview() const noexcept { return previewActive; }
    double getCompensationDb() const noexcept { return compensationDb; }

private:
    void applyTo (labq8::dsp::EqCore& core, const BandSet& bands) noexcept;
    void processChunk (float* const* channels, int numChannels, int n) noexcept;

    labq8::dsp::EqCore appliedCore, previewCore;
    BandSet appliedSet {}, previewSet {};
    bool previewActive = false;
    Listen listen = Listen::applied;

    std::vector<float> scratch; // [applied | preview] x channels x maxBlock
    int maxBlock = 0;
    int channelCount = 0;

    int fadeSamples = 1;
    float fadePos = 0.0f;   // 0 = applied, 1 = preview
    float fadeStep = 0.0f;
    float compensationGain = 1.0f;
    double compensationDb = 0.0;
};
} // namespace iaq8::dsp
