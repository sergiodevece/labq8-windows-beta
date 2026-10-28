// labq8_bench: CPU cost of the 8-band EQ core under reproducible conditions.
//
// - Fixed white-noise input (mt19937, fixed seeds), stereo, 10 s per run.
// - One warm-up run, then the median of 7 runs.
// - Scenarios: idle (all bands 0 dB), static (8 active bands), automating
//   (all 8 bands retargeted every block, so every band ramps continuously).
// - Load = CPU seconds / audio seconds on one core, single-threaded.
// Build in Release; Debug numbers are printed with a warning.

#include "dsp/EqCore.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#if defined(__APPLE__)
 #include <sys/sysctl.h>
#endif

using labq8::dsp::BellSettings;
using labq8::dsp::EqCore;

namespace
{
using BandSet = std::array<BellSettings, EqCore::numBands>;

BandSet makeSet (double offset, bool neutral)
{
    BandSet s {};
    for (int b = 0; b < EqCore::numBands; ++b)
        s[(size_t) b] = { 50.0 * std::pow (2.2, b) * (1.0 + offset),
                          neutral ? 0.0 : (b % 2 ? -1.0 : 1.0) * (6.0 + offset),
                          1.0 + b * 0.5, false };
    return s;
}

std::vector<float> noise (size_t n, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-0.5f, 0.5f);
    std::vector<float> v (n);
    for (auto& x : v)
        x = d (rng);
    return v;
}

enum class Scenario { idle, staticBands, automating };

double run (Scenario scenario, double fs, int blockSize, int runs, double seconds)
{
    const auto n = (size_t) std::llround (seconds * fs);
    const auto left = noise (n, 300), right = noise (n, 301);
    const auto a = makeSet (0.0, scenario == Scenario::idle);
    const auto b = makeSet (0.25, false);
    std::vector<double> loads;

    for (int r = 0; r < runs + 1; ++r)
    {
        EqCore core;
        for (int band = 0; band < EqCore::numBands; ++band)
            core.setBandTargets (band, a[(size_t) band]);
        core.prepare (fs, 2);

        auto l = left, rr = right;
        int block = 0;
        const auto t0 = std::chrono::steady_clock::now();

        for (size_t pos = 0; pos < n; pos += (size_t) blockSize, ++block)
        {
            if (scenario == Scenario::automating)
            {
                const auto& t = (block % 2 == 0) ? b : a;
                for (int band = 0; band < EqCore::numBands; ++band)
                    core.setBandTargets (band, t[(size_t) band]);
            }
            float* ch[] = { l.data() + pos, rr.data() + pos };
            core.process (ch, 2, (int) std::min<size_t> ((size_t) blockSize, n - pos));
        }

        const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        if (r > 0)
            loads.push_back (elapsed / seconds);

        // Keep the result observable so the optimiser cannot drop the work.
        volatile float sink = l[n / 2] + rr[n / 3];
        (void) sink;
    }

    std::sort (loads.begin(), loads.end());
    return loads[loads.size() / 2];
}

std::string machine()
{
#if defined(__APPLE__)
    char brand[256] = {};
    size_t size = sizeof (brand);
    if (sysctlbyname ("machdep.cpu.brand_string", brand, &size, nullptr, 0) == 0)
        return brand;
#endif
    return "unknown CPU";
}
} // namespace

int main()
{
#if defined(__aarch64__) || defined(__arm64__)
    const char* arch = "arm64";
#elif defined(__x86_64__)
    const char* arch = "x86_64";
#else
    const char* arch = "other";
#endif

    std::printf ("labq8_bench  |  %s  |  %s  |  %s build\n", machine().c_str(), arch,
#ifdef NDEBUG
                 "Release"
#else
                 "DEBUG (numbers not representative)"
#endif
    );
    std::printf ("8 bands, stereo, 10 s per run, median of 7 runs after 1 warm-up\n\n");
    std::printf ("%-11s %8s %6s %10s %14s\n", "scenario", "fs", "block", "load %", "ns/frame");

    const struct { Scenario s; const char* name; } scenarios[] = {
        { Scenario::idle, "idle" }, { Scenario::staticBands, "static" }, { Scenario::automating, "automating" }
    };

    for (const auto& sc : scenarios)
        for (const double fs : { 44100.0, 48000.0, 96000.0 })
            for (const int block : { 32, 64, 512 })
            {
                const auto load = run (sc.s, fs, block, 7, 10.0);
                std::printf ("%-11s %8.0f %6d %10.3f %14.1f\n", sc.name, fs, block, load * 100.0, load * 1.0e9 / fs);
            }

    return 0;
}
