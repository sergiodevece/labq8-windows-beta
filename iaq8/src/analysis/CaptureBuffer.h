#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

namespace iaq8::analysis
{
// Collects the useful signal (blocks above signalFloorDbfs) that the analysis
// will read. Written by the audio thread; armed, cancelled, queried, copied
// and prepared from other threads. Atomics only: write() never allocates,
// locks or waits.
//
// Thread contract
//  - prepare(): message thread, never concurrently with write() (the host's
//    prepareToPlay contract). It allocates. It may run while another thread is
//    inside copyOut(): it waits (yield, bounded by one copy) until that copy
//    ends, and blocks new readers/arm() while it reallocates.
//  - write(): audio thread only (single writer). Stores samples only while the
//    state is `capturing`; blocks under the floor, or not finite, are not
//    stored, so the stored audio is exactly the counted audio.
//  - arm()/cancel()/copyOut()/getters: any non-audio thread.
//
// Everything that decides who may touch the samples lives in ONE atomic word:
//   bits  0..31  used   samples stored per channel
//   bits 32..33  state  idle / capturing / done
//   bit  34      reading   a copyOut() is in progress
//   bit  35      preparing a prepare() is reallocating
//   bits 36..63  epoch  incremented by arm() and prepare()
// write() reads the word, copies its block into [used, used+n) and publishes
// with ONE compare-exchange that moves used (and state, when the target is
// reached) from exactly the word it read. Any arm(), prepare(), cancel() or
// reader in between changes the word, the exchange fails and the block is
// discarded: a late block can neither land in a new capture nor flip it to
// `done`. Samples are published by that release exchange; a reader acquires
// the word, so it sees everything below `used`.
// copyOut() and arm() succeed only from idle/done with neither flag set, via
// compare-exchange; while `reading` is set arm() refuses (returns false) and
// write() does not store (state is not capturing), so [0, used) is stable for
// the whole copy. prepare() waits for `reading` to clear before it sets
// `preparing`, and copyOut()/arm() refuse while `preparing` is set, so the
// vectors are never reallocated under a reader.
class CaptureBuffer
{
public:
    static constexpr double targetSeconds = 12.0, minSeconds = 6.0, maxSeconds = 20.0;
    static constexpr double signalFloorDbfs = -60.0; // blocks below this RMS are not counted
    enum class State { idle, capturing, done };

    // Allocates maxSeconds per channel and resets to idle (also when called
    // while capturing: the capture is discarded).
    void prepare (double newSampleRate, int newNumChannels)
    {
        auto w = word.load (std::memory_order_acquire);
        for (;;)
        {
            if (reading (w) || preparing (w))
            {
                std::this_thread::yield();
                w = word.load (std::memory_order_acquire);
                continue;
            }
            if (word.compare_exchange_weak (w, pack (0, State::idle, false, true, epoch (w) + 1),
                                            std::memory_order_acq_rel))
                break;
        }

        const int channels = std::max (1, newNumChannels);
        const int cap = (int) std::ceil (maxSeconds * newSampleRate);
        data.assign ((size_t) channels, std::vector<float> ((size_t) cap, 0.0f));
        capacity = cap;
        targetSamples = (int) std::ceil (targetSeconds * newSampleRate);
        floorPower = std::pow (10.0, signalFloorDbfs / 10.0);
        sampleRate.store (newSampleRate, std::memory_order_relaxed);
        numChannels.store (channels, std::memory_order_relaxed);

        word.fetch_and (~preparingBit, std::memory_order_release);
    }

    // idle/done -> capturing, from empty. Returns false (and does nothing)
    // while already capturing, while a copyOut() is reading or while prepare()
    // is reallocating.
    bool arm() noexcept
    {
        auto w = word.load (std::memory_order_acquire);
        while (state (w) != State::capturing && ! reading (w) && ! preparing (w))
            if (word.compare_exchange_weak (w, pack (0, State::capturing, false, false, epoch (w) + 1),
                                            std::memory_order_acq_rel))
                return true;
        return false;
    }

    // capturing -> idle; what was captured stays readable.
    void cancel() noexcept
    {
        auto w = word.load (std::memory_order_acquire);
        while (state (w) == State::capturing)
            if (word.compare_exchange_weak (w, pack (used (w), State::idle, reading (w), preparing (w), epoch (w)),
                                            std::memory_order_acq_rel))
                return;
    }

    // Audio thread.
    void write (const float* const* ch, int numChannelsIn, int numSamples) noexcept
    {
        auto w = word.load (std::memory_order_acquire);
        if (state (w) != State::capturing || numSamples <= 0 || numChannelsIn <= 0 || capacity == 0)
            return;

        const int channels = numChannels.load (std::memory_order_relaxed);
        const int inCh = std::min (numChannelsIn, channels);
        double sum = 0.0;
        for (int c = 0; c < inCh; ++c)
            for (int i = 0; i < numSamples; ++i)
                sum += (double) ch[c][i] * (double) ch[c][i];
        const double power = sum / ((double) numSamples * inCh);
        if (! (power >= floorPower && std::isfinite (power))) // NaN/Inf and quiet blocks: not signal
            return;

        const int start = used (w);
        const int n = std::min (numSamples, capacity - start);
        if (n <= 0)
            return;

        for (int c = 0; c < channels; ++c)
        {
            const float* src = ch[c < inCh ? c : 0]; // missing channels repeat channel 0
            std::copy (src, src + n, data[(size_t) c].data() + start);
        }

        const int total = start + n;
        const auto next = (total >= targetSamples || total >= capacity) ? State::done : State::capturing;
        word.compare_exchange_strong (w, pack (total, next, false, false, epoch (w)),
                                      std::memory_order_acq_rel); // fails: discarded
    }

    State getState() const noexcept { return state (word.load (std::memory_order_acquire)); }
    double getCapturedSeconds() const noexcept
    {
        const auto fs = sampleRate.load (std::memory_order_relaxed);
        return fs > 0.0 ? (double) used (word.load (std::memory_order_acquire)) / fs : 0.0;
    }
    bool hasEnough() const noexcept { return getCapturedSeconds() >= minSeconds; }

    // Returns samples per channel. 0 (dest empty) while capturing, while
    // prepare() runs or while another copyOut() is in progress; 0 with one
    // empty vector per channel when nothing is stored (e.g. after prepare()).
    // Safe against a concurrent arm()/prepare()/cancel(): they wait or refuse
    // until it ends. May throw std::bad_alloc (dest is then unspecified and
    // the buffer is left as if the copy had not happened).
    int copyOut (std::vector<std::vector<float>>& dest) const
    {
        dest.clear();
        auto w = word.load (std::memory_order_acquire);
        while (state (w) != State::capturing && ! reading (w) && ! preparing (w))
            if (word.compare_exchange_weak (w, pack (used (w), state (w), true, false, epoch (w)),
                                            std::memory_order_acq_rel))
            {
                // Cleared however the copy ends: a throwing allocation below
                // must not leave `reading` set (prepare() would wait forever).
                const ReadingGuard guard { word };
                const int n = used (w);
                const auto channels = data.size(); // stable: prepare() waits for us
                dest.resize (channels);
                for (size_t c = 0; c < channels; ++c)
                    dest[c].assign (data[c].begin(), data[c].begin() + n);
                return n;
            }
        return 0;
    }

    double getSampleRate() const noexcept { return sampleRate.load (std::memory_order_relaxed); }
    int getNumChannels() const noexcept { return numChannels.load (std::memory_order_relaxed); }

private:
    static constexpr std::uint64_t readingBit = 1ull << 34, preparingBit = 1ull << 35;

    // write() runs on the audio thread: the word must never fall back to a lock.
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free);

    struct ReadingGuard
    {
        std::atomic<std::uint64_t>& w;
        ~ReadingGuard() { w.fetch_and (~readingBit, std::memory_order_release); }
    };

    static constexpr std::uint64_t pack (int usedSamples, State s, bool isReading, bool isPreparing,
                                         std::uint64_t ep) noexcept
    {
        return (std::uint64_t) (std::uint32_t) usedSamples | ((std::uint64_t) s << 32)
               | (isReading ? readingBit : 0) | (isPreparing ? preparingBit : 0) | ((ep & 0xfffffffull) << 36);
    }
    static constexpr int used (std::uint64_t w) noexcept { return (int) (std::uint32_t) w; }
    static constexpr State state (std::uint64_t w) noexcept { return (State) ((w >> 32) & 3u); }
    static constexpr bool reading (std::uint64_t w) noexcept { return (w & readingBit) != 0; }
    static constexpr bool preparing (std::uint64_t w) noexcept { return (w & preparingBit) != 0; }
    static constexpr std::uint64_t epoch (std::uint64_t w) noexcept { return w >> 36; }

    std::vector<std::vector<float>> data; // reallocated only under `preparing`
    int capacity = 0;                     // written under `preparing`
    int targetSamples = 0;
    double floorPower = 1.0e-6;
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<int> numChannels { 1 };
    mutable std::atomic<std::uint64_t> word { 0 }; // idle, empty
};
} // namespace iaq8::analysis
