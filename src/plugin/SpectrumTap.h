#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>

namespace labq8
{
// UI-2b: the audio of one analysis point (PRE: before the EQ, POST: after
// it), handed from the audio thread to the editor's spectrum analyzer
// without locks or allocations. Read-only on the audio.
//
// One ring of `capacity` samples per channel, allocated once (never again,
// not even when the sample rate changes). The audio thread copies each block
// in when the analyzer is shown (`setActive`); the editor copies out the
// latest samples at its own refresh rate.
//
// Coherence (a seqlock without waiting): the writer announces how far it is
// about to write (`claimed`) before touching the ring, and publishes how far
// it has written (`written`) after. The reader takes `written`, copies, and
// then checks `claimed`: if the writer may have overwritten anything it
// copied, the copy is refused and the editor simply tries again next frame.
// The samples are relaxed atomics, so no access is a data race.
class SpectrumTap
{
public:
    static constexpr int capacity = 1 << 16; // per channel: 32768 at 192 kHz fits twice
    static constexpr int maxChannels = 2;

    SpectrumTap()
    {
        for (auto& ring : rings)
        {
            ring = std::make_unique<std::atomic<float>[]> ((size_t) capacity);
            for (int i = 0; i < capacity; ++i)
                ring[(size_t) i].store (0.0f, std::memory_order_relaxed);
        }
    }

    // Editor: whether the analyzer is shown. When not, the audio thread only
    // reads this flag.
    void setActive (bool shouldBeActive) noexcept { active.store (shouldBeActive, std::memory_order_relaxed); }
    bool isActive() const noexcept { return active.load (std::memory_order_relaxed); }

    // prepareToPlay (never concurrent with write): a new stream. The editor
    // sees the new generation and ignores everything written before it.
    void prepare (double sampleRate, int numChannels) noexcept
    {
        const auto end = written.load (std::memory_order_relaxed);
        rate.store (sampleRate, std::memory_order_relaxed);
        channels.store (std::clamp (numChannels, 0, maxChannels), std::memory_order_relaxed);
        start.store (end, std::memory_order_relaxed);
        generation.fetch_add (1, std::memory_order_release);
    }

    // Audio thread: appends a block (channels [0, numChannels)).
    void write (const float* const* data, int numChannels, int numSamples) noexcept
    {
        if (! active.load (std::memory_order_relaxed) || numSamples <= 0)
            return;
        const auto n = std::min (numChannels, maxChannels);
        const auto w = written.load (std::memory_order_relaxed);
        claimed.store (w + (std::uint64_t) numSamples, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        for (int ch = 0; ch < n; ++ch)
        {
            auto* ring = rings[(size_t) ch].get();
            for (int i = 0; i < numSamples; ++i)
                ring[(w + (std::uint64_t) i) & mask].store (data[ch][i], std::memory_order_relaxed);
        }
        written.store (w + (std::uint64_t) numSamples, std::memory_order_release);
    }

    struct Frame
    {
        std::uint64_t end = 0;       // samples written so far (the copy ends here)
        std::uint64_t start = 0;     // first sample of the current generation
        std::uint32_t generation = 0;
        double sampleRate = 0.0;
        int channels = 0;
    };

    // Editor: copies samples [end - count, end) of each channel into dest
    // (count <= capacity / 2); samples before `from` (or before the
    // generation started) are given as 0. Returns false, with dest
    // unspecified, when the writer may have overwritten part of the copy.
    bool copyLatest (float* const* dest, int numChannels, int count, std::uint64_t from, Frame& frame) const noexcept
    {
        frame.generation = generation.load (std::memory_order_acquire);
        frame.sampleRate = rate.load (std::memory_order_relaxed);
        frame.channels = channels.load (std::memory_order_relaxed);
        frame.start = start.load (std::memory_order_relaxed);
        frame.end = written.load (std::memory_order_acquire);

        const auto first = std::max (from, frame.start);
        const auto begin = frame.end - std::min<std::uint64_t> (frame.end, (std::uint64_t) count);
        const auto zeros = (int) (count - (int) (frame.end - begin)); // before the very first sample
        const auto n = std::min (numChannels, maxChannels);
        for (int ch = 0; ch < n; ++ch)
        {
            const auto* ring = rings[(size_t) ch].get();
            for (int i = 0; i < zeros; ++i)
                dest[ch][i] = 0.0f;
            for (int i = zeros; i < count; ++i)
            {
                const auto index = begin + (std::uint64_t) (i - zeros);
                dest[ch][i] = index < first ? 0.0f : ring[index & mask].load (std::memory_order_relaxed);
            }
        }
        std::atomic_thread_fence (std::memory_order_acquire);
        return untouched (claimed.load (std::memory_order_relaxed), begin)
            && generation.load (std::memory_order_relaxed) == frame.generation;
    }

    // Whether a copy whose oldest sample is `begin` is intact when the writer
    // has claimed up to sample `claimedEnd` (exclusive): sample `begin` is
    // overwritten by sample begin + capacity.
    static constexpr bool untouched (std::uint64_t claimedEnd, std::uint64_t begin) noexcept
    {
        return claimedEnd <= begin + (std::uint64_t) capacity;
    }

    std::uint64_t getWritten() const noexcept { return written.load (std::memory_order_acquire); }

private:
    static constexpr std::uint64_t mask = (std::uint64_t) capacity - 1;

    std::unique_ptr<std::atomic<float>[]> rings[maxChannels];
    std::atomic<std::uint64_t> written { 0 }, claimed { 0 }, start { 0 };
    std::atomic<std::uint32_t> generation { 0 };
    std::atomic<double> rate { 0.0 };
    std::atomic<int> channels { 0 };
    std::atomic<bool> active { false };

    static_assert (std::atomic<float>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free
                       && std::atomic<double>::is_always_lock_free, "the spectrum tap must be lock-free");
};
} // namespace labq8
