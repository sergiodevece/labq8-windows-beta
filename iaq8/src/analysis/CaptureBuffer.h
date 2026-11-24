#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace iaq8::analysis
{
// Collects the useful signal (blocks above signalFloorDbfs) that the analysis
// will read. Written by the audio thread; armed, cancelled, queried, copied
// and prepared from other threads. Atomics only: write() never allocates,
// locks or waits.
//
// Thread contract
//  - prepare(): message thread, while the audio thread is not running (the
//    host's prepareToPlay contract). It is the only call that allocates.
//  - write(): audio thread only (single writer). Stores samples only while the
//    state is `capturing`; blocks under the floor are not stored, so the
//    stored audio is exactly the counted audio.
//  - arm()/cancel(): any non-audio thread.
//  - getters: any thread.
//  - copyOut(): non-audio thread, only while the state is not `capturing`.
//
// Memory ordering. `used` (samples stored per channel) is modified by the
// writer only, through a compare-exchange from the value it read at the start
// of the block; arm() resets it to 0, which makes an in-flight block's
// compare-exchange fail, so that block is discarded and cannot corrupt the new
// capture. The writer stores samples first and then publishes them with the
// release compare-exchange on `used`; copyOut() acquire-loads `used` and reads
// only [0, used), so everything it reads happened-before. An in-flight block
// after cancel() writes only beyond the published `used`.
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
        state.store ((int) State::idle, std::memory_order_seq_cst);
        used.store (0, std::memory_order_seq_cst);

        sampleRate = newSampleRate;
        numChannels = std::max (1, newNumChannels);
        capacity = (int) std::ceil (maxSeconds * sampleRate);
        targetSamples = (int) std::ceil (targetSeconds * sampleRate);

        data.assign ((size_t) numChannels, std::vector<float> ((size_t) capacity, 0.0f));
        floorPower = std::pow (10.0, signalFloorDbfs / 10.0);
    }

    // idle/done -> capturing, from empty. Ignored while already capturing.
    // The count is cleared before the state changes: while not capturing the
    // writer does not touch it, and an in-flight block left over from cancel()
    // fails its compare-exchange on the cleared count.
    void arm() noexcept
    {
        if (state.load (std::memory_order_acquire) == (int) State::capturing)
            return;
        used.store (0, std::memory_order_seq_cst);
        state.store ((int) State::capturing, std::memory_order_seq_cst);
    }

    // capturing -> idle; what was captured stays readable.
    void cancel() noexcept
    {
        int expected = (int) State::capturing;
        state.compare_exchange_strong (expected, (int) State::idle);
    }

    // Audio thread.
    void write (const float* const* ch, int numChannelsIn, int numSamples) noexcept
    {
        if (state.load (std::memory_order_acquire) != (int) State::capturing
            || numSamples <= 0 || numChannelsIn <= 0 || capacity == 0)
            return;

        // Block power over the channels used.
        const int inCh = std::min (numChannelsIn, numChannels);
        double sum = 0.0;
        for (int c = 0; c < inCh; ++c)
            for (int i = 0; i < numSamples; ++i)
                sum += (double) ch[c][i] * (double) ch[c][i];
        if (sum / ((double) numSamples * inCh) < floorPower)
            return;

        const int start = used.load (std::memory_order_acquire);
        const int n = std::min (numSamples, capacity - start);
        if (n <= 0)
            return;

        for (int c = 0; c < numChannels; ++c)
        {
            const float* src = ch[c < inCh ? c : 0]; // missing channels repeat channel 0
            std::copy (src, src + n, data[(size_t) c].data() + start);
        }

        int expected = start;
        if (! used.compare_exchange_strong (expected, start + n, std::memory_order_acq_rel))
            return; // re-armed meanwhile: block discarded

        if (start + n >= targetSamples || start + n >= capacity)
        {
            int capturing = (int) State::capturing;
            state.compare_exchange_strong (capturing, (int) State::done, std::memory_order_acq_rel);
        }
    }

    State getState() const noexcept { return (State) state.load (std::memory_order_acquire); }
    double getCapturedSeconds() const noexcept
    {
        return sampleRate > 0.0 ? (double) used.load (std::memory_order_acquire) / sampleRate : 0.0;
    }
    bool hasEnough() const noexcept { return getCapturedSeconds() >= minSeconds; }

    // Returns samples per channel (0 while capturing or empty).
    int copyOut (std::vector<std::vector<float>>& dest) const
    {
        dest.clear();
        if (getState() == State::capturing)
            return 0;
        const int n = used.load (std::memory_order_acquire);
        dest.resize ((size_t) numChannels);
        for (int c = 0; c < numChannels; ++c)
            dest[(size_t) c].assign (data[(size_t) c].begin(), data[(size_t) c].begin() + n);
        return n;
    }

    double getSampleRate() const noexcept { return sampleRate; }
    int getNumChannels() const noexcept { return numChannels; }

private:
    std::vector<std::vector<float>> data;
    double sampleRate = 0.0;
    int numChannels = 1;
    int capacity = 0;
    int targetSamples = 0;
    double floorPower = 1.0e-6;
    std::atomic<int> state { (int) State::idle };
    std::atomic<int> used { 0 };
};
} // namespace iaq8::analysis
