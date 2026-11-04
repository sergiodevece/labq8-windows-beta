// Real-time properties: no heap allocation on the audio path, and a CPU
// budget measured under reproducible conditions (fixed signals and seeds,
// warm-up run, median of repeated runs, Release build only).

#include "AllocationGuard.h"
#include "TestHarness.h"
#include "TestSignals.h"

#include "dsp/EqCore.h"
#include "parameters/ParameterIDs.h"
#include "plugin/PluginProcessor.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>

using labq8::test::ScopedAllocationCounter;
using labq8::test::str;
using namespace labq8::test::signals;
namespace params = labq8::params;

namespace
{
[[maybe_unused]] std::array<labq8::dsp::BellSettings, labq8::dsp::EqCore::numBands> activeSet (double offset)
{
    std::array<labq8::dsp::BellSettings, labq8::dsp::EqCore::numBands> s {};
    for (int b = 0; b < labq8::dsp::EqCore::numBands; ++b)
        s[(size_t) b] = { 50.0 * std::pow (2.2, b) * (1.0 + offset), (b % 2 ? -1.0 : 1.0) * (6.0 + offset), 1.0 + b * 0.5, false };
    return s;
}

// Seconds of CPU per second of audio (fraction of one core), median of runs.
[[maybe_unused]] double measureCoreLoad (double fs, int blockSize, bool automate, int runs = 5, double seconds = 10.0)
{
    const auto numSamples = samples (seconds, fs);
    const auto left = makeNoise (numSamples, 200), right = makeNoise (numSamples, 201);
    std::vector<double> loads;

    for (int run = 0; run < runs + 1; ++run) // run 0 is warm-up
    {
        labq8::dsp::EqCore core;
        const auto a = activeSet (0.0), b = activeSet (0.25);
        for (int band = 0; band < labq8::dsp::EqCore::numBands; ++band)
            core.setBandTargets (band, a[(size_t) band]);
        core.prepare (fs, 2);

        auto l = left, r = right;
        int blockIndex = 0;
        const auto start = std::chrono::steady_clock::now();

        for (size_t pos = 0; pos < numSamples; pos += (size_t) blockSize, ++blockIndex)
        {
            if (automate) // flip all 8 bands every block: ramps never settle
            {
                const auto& t = (blockIndex % 2 == 0) ? b : a;
                for (int band = 0; band < labq8::dsp::EqCore::numBands; ++band)
                    core.setBandTargets (band, t[(size_t) band]);
            }

            const auto n = static_cast<int> (std::min<size_t> ((size_t) blockSize, numSamples - pos));
            float* ch[] = { l.data() + pos, r.data() + pos };
            core.process (ch, 2, n);
        }

        const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        if (run > 0)
            loads.push_back (elapsed / seconds);
    }

    std::sort (loads.begin(), loads.end());
    return loads[loads.size() / 2];
}
} // namespace

LQ_TEST ("rt: the allocation counter detects allocations (self-test)")
{
    // Pointers escape through a volatile so the optimiser cannot elide the
    // new/delete pairs (C++14 allows that for unobservable allocations).
    static void* volatile escape = nullptr;

    ScopedAllocationCounter counter;
    auto p = std::make_unique<int> (42);
    escape = p.get();
    std::vector<float> v (16);
    escape = v.data();
    LQ_CHECK (counter.count() == 2, str ("expected 2 counted allocations, got ", counter.count()));
    LQ_CHECK (escape != nullptr, "escape pointer");
}

LQ_TEST ("rt: no heap allocation while processing (EQ core and processor)")
{
    // EQ core: ramps moving in all 8 bands, odd block sizes, stereo.
    for (const auto fs : sampleRates)
    {
        labq8::dsp::EqCore core;
        const auto a = activeSet (0.0), b = activeSet (0.3);
        for (int band = 0; band < 8; ++band)
            core.setBandTargets (band, a[(size_t) band]);
        core.prepare (fs, 2);
        auto l = makeNoise (48000, 210), r = makeNoise (48000, 211);

        ScopedAllocationCounter counter;
        size_t pos = 0;
        for (int block = 0; pos < l.size(); ++block)
        {
            const auto& t = (block % 3 == 0) ? b : a;
            for (int band = 0; band < 8; ++band)
                core.setBandTargets (band, t[(size_t) band]);
            const auto n = std::min<size_t> ((size_t) (1 + (block * 37) % 700), l.size() - pos);
            float* ch[] = { l.data() + pos, r.data() + pos };
            core.process (ch, 2, (int) n);
            pos += n;
        }
        LQ_CHECK (counter.count() == 0, str ("EqCore allocated ", counter.count(), " times, fs=", fs));
    }

    // Processor: host automation between blocks (outside the counted region).
    labq8::LabQ8AudioProcessor p;
    p.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    long allocations = 0;

    for (int block = 0; block < 400; ++block)
    {
        for (int band = 0; band < params::numBands; ++band)
        {
            auto* gain = p.getValueTreeState().getParameter (params::bandIDs[(size_t) band].gain);
            gain->setValueNotifyingHost ((float) ((block + band) % 10) / 10.0f);
        }
        const auto noise = makeNoise (512, (std::uint32_t) block);
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, 0, noise.data(), 512);

        ScopedAllocationCounter counter;
        p.processBlock (buffer, midi);
        allocations += counter.count();
    }
    LQ_CHECK (allocations == 0, str ("processBlock allocated ", allocations, " times"));
}

LQ_TEST ("perf: 8-band EQ core CPU load (stereo, reproducible)")
{
#ifndef NDEBUG
    ctx.skip ("Debug build: timings are not meaningful; run the Release test binary");
    return;
#else
    // Budget: worst case (all 8 bands ramping every block, 96 kHz stereo,
    // 64-sample blocks) must use < 5% of one core. E1a measured ~0.3% on the
    // development machine (docs/ARCHITECTURE.md), so this only trips on a real
    // regression (e.g. per-sample coefficient design or work in idle bands).
    constexpr double budget = 0.05;

    for (const bool automate : { false, true })
        for (const auto fs : sampleRates)
        {
            const auto load = measureCoreLoad (fs, 64, automate);
            ctx.note (str (automate ? "automating" : "static    ", "  fs=", fs, "  block=64  load=",
                           load * 100.0, "% of one core  (", load * 1.0e9 / fs, " ns per stereo frame)"));
            LQ_CHECK (load < budget, str ("CPU load ", load * 100.0, "% over budget at fs=", fs));
        }
#endif
}
