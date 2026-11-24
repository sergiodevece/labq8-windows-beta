// Tests of the lock-free capture of useful signal (analysis input).

// Exact float comparisons are intentional (counts, untouched samples).
#pragma clang diagnostic ignored "-Wfloat-equal"

#include "AllocationGuard.h"
#include "TestHarness.h"
#include "TestSignals.h"

#include "analysis/CaptureBuffer.h"
#include "plugin/Iaq8Processor.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace iaq8;
using namespace labq8::test::signals;
using labq8::test::ScopedAllocationCounter;
using labq8::test::str;

namespace
{
constexpr double kFs = 48000.0;
constexpr int kBlock = 512;

// Feeds the same signal to `channels` channels in blocks of kBlock.
void feed (analysis::CaptureBuffer& c, const Signal& x, int channels)
{
    std::vector<const float*> ptrs ((size_t) channels);
    for (size_t pos = 0; pos < x.size(); pos += (size_t) kBlock)
    {
        const auto n = (int) std::min ((size_t) kBlock, x.size() - pos);
        for (auto& p : ptrs)
            p = x.data() + pos;
        c.write (ptrs.data(), channels, n);
    }
}

Signal noise (double seconds, std::uint32_t seed) { return makeNoise (samples (seconds, kFs), seed); }
Signal silence (double seconds) { return Signal (samples (seconds, kFs), 0.0f); }
Signal cat (Signal a, const Signal& b) { a.insert (a.end(), b.begin(), b.end()); return a; }
} // namespace

LQ_TEST ("capture: counts only blocks above -60 dBFS")
{
    analysis::CaptureBuffer c;
    c.prepare (kFs, 2);
    c.arm();
    feed (c, cat (cat (noise (4.0, 1), silence (4.0)), noise (4.0, 2)), 2);

    LQ_CHECK (std::abs (c.getCapturedSeconds() - 8.0) < 0.02, str ("captured: ", c.getCapturedSeconds()));
    LQ_CHECK (c.hasEnough(), "hasEnough at 8 s");
    LQ_CHECK (c.getState() == analysis::CaptureBuffer::State::capturing, "still capturing below 12 s");

    // A -50 dBFS block counts, a -70 dBFS block does not.
    analysis::CaptureBuffer d;
    d.prepare (kFs, 1);
    d.arm();
    Signal loud (kBlock, 0.00316f), quiet (kBlock, 0.000316f);
    const float* l = loud.data();
    const float* q = quiet.data();
    d.write (&q, 1, kBlock);
    LQ_CHECK (d.getCapturedSeconds() == 0.0, "-70 dBFS block ignored");
    d.write (&l, 1, kBlock);
    LQ_CHECK (std::abs (d.getCapturedSeconds() - kBlock / kFs) < 1.0e-9, "-50 dBFS block counted");
}

LQ_TEST ("capture: stops by itself at 12 s of signal")
{
    analysis::CaptureBuffer c;
    c.prepare (kFs, 2);
    c.arm();
    feed (c, noise (15.0, 3), 2);

    LQ_CHECK (c.getState() == analysis::CaptureBuffer::State::done, "done");
    std::vector<std::vector<float>> out;
    const auto n = c.copyOut (out);
    const auto expected = (int) samples (12.0, kFs);
    LQ_CHECK (std::abs (n - expected) <= kBlock, str ("samples: ", n, " expected ", expected));
    LQ_CHECK (out.size() == 2 && (int) out[0].size() == n && (int) out[1].size() == n, "shape");
    LQ_CHECK (c.getNumChannels() == 2 && c.getSampleRate() == kFs, "format");

    // The stored signal is the input, in order.
    const auto x = noise (15.0, 3);
    bool same = true;
    for (int i = 0; i < n; ++i)
        same = same && out[0][(size_t) i] == x[(size_t) i];
    LQ_CHECK (same, "copy equals input");

    // done is final until re-armed; arm clears.
    c.arm();
    LQ_CHECK (c.getState() == analysis::CaptureBuffer::State::capturing && c.getCapturedSeconds() == 0.0, "re-arm clears");
}

LQ_TEST ("capture: cancel keeps what was captured and stops writing")
{
    analysis::CaptureBuffer c;
    c.prepare (kFs, 1);
    c.arm();
    feed (c, noise (7.0, 4), 1);
    c.cancel();
    LQ_CHECK (c.getState() == analysis::CaptureBuffer::State::idle, "idle");
    const auto before = c.getCapturedSeconds();
    feed (c, noise (2.0, 5), 1);
    LQ_CHECK (c.getCapturedSeconds() == before, "no writes after cancel");
    LQ_CHECK (c.hasEnough(), "enough after cancel at 7 s");
    std::vector<std::vector<float>> out;
    LQ_CHECK (c.copyOut (out) == (int) std::lround (before * kFs), "copyOut after cancel");
}

LQ_TEST ("capture: write does not allocate")
{
    analysis::CaptureBuffer c;
    c.prepare (kFs, 2);
    c.arm();
    const auto x = noise (3.0, 6);
    std::vector<const float*> ptrs (2);
    long allocations = 0;
    for (size_t pos = 0; pos + kBlock <= x.size(); pos += kBlock)
    {
        ptrs[0] = ptrs[1] = x.data() + pos;
        ScopedAllocationCounter counter;
        c.write (ptrs.data(), 2, kBlock);
        allocations += counter.count();
    }
    LQ_CHECK (allocations == 0, str ("allocations: ", allocations));
    LQ_CHECK (c.getCapturedSeconds() > 2.9, "wrote");
}

LQ_TEST ("capture: prepare while capturing resets to idle")
{
    analysis::CaptureBuffer c;
    c.prepare (kFs, 2);
    c.arm();
    feed (c, noise (3.0, 7), 2);
    LQ_CHECK (c.getCapturedSeconds() > 2.0, "captured something");

    c.prepare (44100.0, 1);
    LQ_CHECK (c.getState() == analysis::CaptureBuffer::State::idle, "idle after prepare");
    LQ_CHECK (c.getCapturedSeconds() == 0.0 && ! c.hasEnough(), "cleared");
    LQ_CHECK (c.getNumChannels() == 1 && c.getSampleRate() == 44100.0, "new format");
    std::vector<std::vector<float>> out;
    LQ_CHECK (c.copyOut (out) == 0, "nothing to copy");

    // Writes while idle are ignored.
    feed (c, noise (1.0, 8), 1);
    LQ_CHECK (c.getCapturedSeconds() == 0.0, "idle ignores write");
}

LQ_TEST ("capture: mono bus captures one channel")
{
    Iaq8AudioProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::mono());
    layout.outputBuses.add (juce::AudioChannelSet::mono());
    p.setBusesLayout (layout);
    p.setRateAndBufferSizeDetails (kFs, kBlock);
    p.prepareToPlay (kFs, kBlock);

    auto& c = p.getCapture();
    LQ_CHECK (c.getNumChannels() == 1, str ("channels: ", c.getNumChannels()));
    c.arm();

    juce::AudioBuffer<float> buffer (1, kBlock);
    juce::MidiBuffer midi;
    const auto x = noise (2.0, 9);
    for (size_t pos = 0; pos + kBlock <= x.size(); pos += kBlock)
    {
        buffer.copyFrom (0, 0, x.data() + pos, kBlock);
        p.processBlock (buffer, midi);
    }
    LQ_CHECK (c.getCapturedSeconds() > 1.9, str ("captured: ", c.getCapturedSeconds()));
    c.cancel(); // copyOut needs a non-capturing state
    std::vector<std::vector<float>> out;
    c.copyOut (out);
    LQ_CHECK (out.size() == 1 && (int) out[0].size() >= kBlock, "one channel out");
    if (out.empty() || (int) out[0].size() < kBlock)
        return;
    // Captured is the input (before the EQ), not the output.
    LQ_CHECK (std::equal (out[0].begin(), out[0].begin() + kBlock, x.begin()), "pre-EQ input captured");
}
