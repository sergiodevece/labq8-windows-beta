#pragma once

#include <algorithm>
#include <utility>
#include <array>
#include <atomic>
#include <cstdint>

namespace labq8
{
// UI-2c: what the eight dynamic sections of the EQ are doing, handed from
// the audio thread to the editor without locks or allocations. Read-only on
// the audio: the processor copies values the DSP has already computed (it
// never asks the DSP to compute anything for the screen).
//
// Snapshots. The processor runs the EQ in sub-blocks of at most 256 samples
// and offers a snapshot after each one: all eight bands at that instant. The
// editor always draws the response of a single instant, never values of
// different moments put together.
//
// Which instant (A2, approved 04-11-2026). Between two reads by the editor
// the telemetry keeps the offered snapshot with the largest total effective
// reduction (sum over the bands); once the editor has read it, the next
// offer replaces it whatever its value. So a peak shorter than a screen
// frame is still shown as the real instant it was, and when the reduction
// falls the screen follows it one frame later. Measured in
// labq8_bench --ui2c-intrablock (docs/UI_IMPLEMENTATION.md, UI-2c).
//
// Coherence: a seqlock. The writer makes the sequence odd, writes, then
// makes it even again (release); the reader takes an even sequence
// (acquire), copies, and accepts the copy only if the sequence did not
// move. The editor marks what it read by the snapshot's id; a writer that
// sees its kept snapshot marked replaces it. Every field is a relaxed
// atomic, so no access is a data race. One writer at a time (the audio
// callback, or prepareToPlay, which the host never runs concurrently).
//
// Per band, exactly what the band applied at the last control tick of the
// sub-block:
//   - the Bell path's static gain, frequency and effective Q as the DSP has
//     smoothed them (EqBand::getCurrentGainDb / Frequency / Q);
//   - the gain reduction of its dynamic section (>= 0 dB, after attack,
//     release, range and the 20 ms enable fade; DynamicSection);
//   - whether the band's filter is a Bell (the only topology it acts on).
// The effective gain is clamp(static - reduction, -24, +24), the
// designer's limit (effectiveReductionDb).
class DynamicsTelemetry
{
public:
    static constexpr int numBands = 8;
    static constexpr double gainLimitDb = 24.0; // dsp::limits::maxGainDb
    static_assert (std::atomic<float>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free,
                   "the telemetry must be lock-free");

    struct Band
    {
        float staticGainDb = 0.0f, reductionDb = 0.0f, frequencyHz = 1000.0f, q = 1.0f;
        bool bell = false;
    };

    struct Snapshot
    {
        std::uint64_t id = 0;         // offer the kept values come from (0: none yet)
        std::uint64_t offered = 0;    // offers so far (moves while audio is processed)
        std::uint32_t generation = 0; // prepareToPlay count
        bool hostBypassed = false;    // the host's bypass: nothing processed
        // E4d: the bank the values come from (0 STEREO, 1 MID, 2 SIDE:
        // params::Bank), and whether they are valid. Not available = the
        // bank's path is not heard (standby) or a mode change is fading:
        // the data is temporarily missing, it is NOT a reduction of 0 dB.
        int bank = 0;
        bool available = true;
        std::array<Band, numBands> bands {};
    };

    // E4d: what an offer carries.
    enum class State { live, hostBypassed, unavailable };

    // The reduction the band really applies: the designer clamps the total
    // gain to +-24 dB (dsp::clampGainDb), so with a low static gain less
    // than the section's reduction is heard. >= 0; 0 for non-Bell filters.
    static double effectiveReductionDb (const Band& b) noexcept
    {
        if (! b.bell || ! (b.reductionDb > 0.0f))
            return 0.0;
        const auto g = (double) b.staticGainDb;
        return g - std::clamp (g - (double) b.reductionDb, -gainLimitDb, gainLimitDb);
    }

    // E4d: the bank the editor shows (0 STEREO, 1 MID, 2 SIDE). The audio
    // thread offers that bank's sections and tags every snapshot with it.
    void setWatchedBank (int bank) noexcept { watched.store (std::clamp (bank, 0, 2), std::memory_order_relaxed); }
    int getWatchedBank() const noexcept { return watched.load (std::memory_order_relaxed); }

    // Editor: whether the screen shows the dynamics. When not, the audio
    // thread only reads this flag.
    void setActive (bool shouldBeActive) noexcept { active.store (shouldBeActive, std::memory_order_relaxed); }
    bool isActive() const noexcept { return active.load (std::memory_order_relaxed); }

    // prepareToPlay: a new stream. The editor drops what it was showing.
    void prepare() noexcept
    {
        beginWrite();
        generation.store (generation.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        writeValues ({}, false, nextId(), getWatchedBank(), true);
        keptTotal = 0.0;
        keptState = State::live;
        keptBank = getWatchedBank();
        endWrite();
    }

    // Audio thread, after each sub-block of the EQ. `read (band, Band&)`
    // fills one band from the DSP.
    template <typename ReadBand>
    void offer (bool hostBypassed, ReadBand&& read) noexcept
    {
        offer (hostBypassed ? State::hostBypassed : State::live, 0, std::forward<ReadBand> (read));
    }

    // E4d: `bank` tags the snapshot; `read` is only called for a live offer.
    // An unavailable offer always replaces the kept one (a mode change must
    // not leave an older reduction on the screen); a live one replaces a
    // kept snapshot of another bank or state, as well as the usual rule.
    template <typename ReadBand>
    void offer (State state, int bank, ReadBand&& read) noexcept
    {
        if (! active.load (std::memory_order_relaxed))
            return;
        std::array<Band, numBands> values {};
        double total = 0.0;
        if (state == State::live)
            for (int b = 0; b < numBands; ++b)
            {
                read (b, values[(size_t) b]);
                total += effectiveReductionDb (values[(size_t) b]);
            }

        const auto id = nextId();
        const auto keptId = kept.load (std::memory_order_relaxed);
        const auto replace = keptId == 0 || consumed.load (std::memory_order_acquire) == keptId
                          || state != State::live || keptState != State::live || bank != keptBank || total > keptTotal;
        beginWrite();
        if (replace)
        {
            writeValues (values, state == State::hostBypassed, id, bank, state != State::unavailable);
            keptTotal = total;
            keptState = state;
            keptBank = bank;
        }
        offers.store (id, std::memory_order_relaxed);
        endWrite();
    }

    // Editor: the kept snapshot, marked as read (the next offer replaces
    // it). False when the writer was busy at every attempt; the editor tries
    // again next frame.
    bool take (Snapshot& out) noexcept
    {
        if (! peek (out))
            return false;
        consumed.store (out.id, std::memory_order_release);
        return true;
    }

    // The same without marking it (tests, tools).
    bool peek (Snapshot& out) const noexcept
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto before = sequence.load (std::memory_order_acquire);
            if ((before & 1u) != 0)
                continue;
            out.id = kept.load (std::memory_order_relaxed);
            out.offered = offers.load (std::memory_order_relaxed);
            out.generation = generation.load (std::memory_order_relaxed);
            out.hostBypassed = bypassed.load (std::memory_order_relaxed);
            out.bank = bankOfKept.load (std::memory_order_relaxed);
            out.available = availableKept.load (std::memory_order_relaxed);
            for (int b = 0; b < numBands; ++b)
            {
                const auto& slot = bands[(size_t) b];
                out.bands[(size_t) b] = { slot.staticGain.load (std::memory_order_relaxed), slot.reduction.load (std::memory_order_relaxed),
                                          slot.frequency.load (std::memory_order_relaxed), slot.q.load (std::memory_order_relaxed),
                                          slot.bell.load (std::memory_order_relaxed) };
            }
            std::atomic_thread_fence (std::memory_order_acquire);
            if (sequence.load (std::memory_order_relaxed) == before)
                return true;
        }
        return false;
    }

private:
    std::uint64_t nextId() noexcept { return ++lastId; }

    void writeValues (const std::array<Band, numBands>& values, bool hostBypassed, std::uint64_t id, int bank, bool available) noexcept
    {
        bankOfKept.store (bank, std::memory_order_relaxed);
        availableKept.store (available, std::memory_order_relaxed);
        for (int b = 0; b < numBands; ++b)
        {
            auto& slot = bands[(size_t) b];
            const auto& v = values[(size_t) b];
            slot.staticGain.store (v.staticGainDb, std::memory_order_relaxed);
            slot.reduction.store (v.reductionDb, std::memory_order_relaxed);
            slot.frequency.store (v.frequencyHz, std::memory_order_relaxed);
            slot.q.store (v.q, std::memory_order_relaxed);
            slot.bell.store (v.bell, std::memory_order_relaxed);
        }
        bypassed.store (hostBypassed, std::memory_order_relaxed);
        kept.store (id, std::memory_order_relaxed);
    }

    void beginWrite() noexcept
    {
        sequence.store (sequence.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
    }

    void endWrite() noexcept
    {
        sequence.store (sequence.load (std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    struct Slot
    {
        std::atomic<float> staticGain { 0.0f }, reduction { 0.0f }, frequency { 1000.0f }, q { 1.0f };
        std::atomic<bool> bell { false };
    };

    std::array<Slot, numBands> bands;
    std::atomic<std::uint32_t> sequence { 0 }, generation { 0 };
    std::atomic<std::uint64_t> kept { 0 }, offers { 0 }, consumed { 0 };
    std::atomic<bool> bypassed { false }, active { false };
    std::atomic<int> watched { 0 }, bankOfKept { 0 };
    std::atomic<bool> availableKept { true };
    // Writer only.
    std::uint64_t lastId = 0;
    double keptTotal = 0.0;
    State keptState = State::live;
    int keptBank = 0;
};
} // namespace labq8
