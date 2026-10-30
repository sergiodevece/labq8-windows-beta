#pragma once

#include <atomic>
#include <cmath>
#include <limits>

namespace labq8
{
// UI-2a: sample-peak level of up to two channels, handed from the audio
// thread to the editor without locks or allocations. Read-only on the audio:
// the samples are never modified.
//
// The audio thread folds the peak of each block into a per-channel maximum;
// the editor takes that maximum (and resets it to 0) at its own refresh
// rate. A peak is therefore never lost, whatever the block size or the
// refresh rate, and silence reads exactly 0.
class LevelMeter
{
public:
    static constexpr int maxChannels = 2;
    static_assert (std::atomic<float>::is_always_lock_free && std::atomic<int>::is_always_lock_free,
                   "the meters must be lock-free");

    // Largest |sample| of a block (linear, 0 for silence). A NaN or infinite
    // sample gives +inf: shown as above the top of the scale.
    static float blockPeak (const float* samples, int numSamples) noexcept
    {
        float peak = 0.0f, nonFinite = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const auto magnitude = std::fabs (samples[i]);
            peak = magnitude > peak ? magnitude : peak;
            nonFinite += samples[i] * 0.0f; // stays 0 unless a sample is NaN or infinite
        }
        return std::isnan (nonFinite) ? std::numeric_limits<float>::infinity() : peak;
    }

    // Audio thread: measures channels [0, numChannels) of a block.
    void measure (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        const auto n = numChannels < maxChannels ? numChannels : maxChannels;
        channelCount.store (n, std::memory_order_relaxed);
        for (int ch = 0; ch < n; ++ch)
            accumulate (ch, blockPeak (channels[ch], numSamples));
    }

    // Audio thread: raises the channel's maximum to `peak`. The loop only
    // repeats when the editor takes the value at the same moment.
    void accumulate (int channel, float peak) noexcept
    {
        auto& slot = peaks[channel];
        auto current = slot.load (std::memory_order_relaxed);
        while (peak > current && ! slot.compare_exchange_weak (current, peak, std::memory_order_relaxed))
        {
        }
    }

    // Editor: the maximum since the previous take (linear), then 0.
    float takePeak (int channel) noexcept { return peaks[channel].exchange (0.0f, std::memory_order_relaxed); }

    // Channels of the last measured block: 1 (mono) or 2 (stereo).
    int getNumChannels() const noexcept { return channelCount.load (std::memory_order_relaxed); }

    void reset() noexcept
    {
        for (auto& p : peaks)
            p.store (0.0f, std::memory_order_relaxed);
    }

private:
    std::atomic<float> peaks[maxChannels] { 0.0f, 0.0f };
    std::atomic<int> channelCount { 0 };
};
} // namespace labq8
