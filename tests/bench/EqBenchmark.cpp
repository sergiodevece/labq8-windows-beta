// labq8_bench: CPU cost of the 8-band EQ core under reproducible conditions.
//
// - Fixed white-noise input (mt19937, fixed seeds), stereo, 10 s per run.
// - One warm-up run, then the median of 7 runs.
// - Scenarios (E1a, unchanged so results compare across phases): idle (all
//   bands 0 dB), static (8 active Bells), automating (all 8 bands retargeted
//   every block, so every band ramps continuously).
// - E1b scenarios: mixed (8 active bands of all six types), bypass (8 mixed
//   bands bypassed: Bell/shelves at 0 dB, cuts/notch on the dry path),
//   typechange (each band changes type every 250 ms, staggered), xfade-all
//   (worst case: all 8 bands crossfading all the time).
// - E2a (--e2a): ONE band, stereo, 3 s x 5 runs. static (EqBand Bell), dyn-off (dynamic
//   band with dynamics disabled), dyn-rest (enabled, signal below the
//   threshold), dyn-active (level modulated at 3 Hz across the threshold, so
//   the gain and coefficients change at every tick), plus the cost of one
//   designBell call (the per-tick coefficient update).
// - Load = CPU seconds / audio seconds on one core, single-threaded.
// Build in Release; Debug numbers are printed with a warning.

#include "dsp/DynamicBellBand.h"
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

using labq8::dsp::BandSettings;
using labq8::dsp::BellSettings;
using labq8::dsp::EqCore;
using labq8::dsp::FilterType;

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

enum class Scenario { idle, staticBands, automating, mixed, bypassed, typeChange, crossfadeAll };

// Types for the mixed scenarios: every topology appears.
constexpr int mixedTypes[EqCore::numBands] = { 3, 1, 0, 5, 0, 2, 0, 4 };

BandSettings mixedBand (int b, double offset, bool bypass, int typeShift = 0)
{
    const auto s = makeSet (offset, false)[(size_t) b];
    return { s.frequencyHz, s.gainDb, s.q, bypass, labq8::dsp::filterTypeFromIndex ((mixedTypes[b] + typeShift) % 6) };
}

bool isE1bScenario (Scenario s) { return s >= Scenario::mixed; }

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
        {
            if (isE1bScenario (scenario))
                core.setBandTargets (band, mixedBand (band, 0.0, scenario == Scenario::bypassed));
            else
                core.setBandTargets (band, a[(size_t) band]);
        }
        core.prepare (fs, 2);

        auto l = left, rr = right;
        int block = 0;
        const auto fade = (size_t) std::llround (0.02 * fs), quarter = (size_t) std::llround (0.25 * fs);
        int shift = 0;
        const auto t0 = std::chrono::steady_clock::now();

        for (size_t pos = 0; pos < n; pos += (size_t) blockSize, ++block)
        {
            if (scenario == Scenario::automating)
            {
                const auto& t = (block % 2 == 0) ? b : a;
                for (int band = 0; band < EqCore::numBands; ++band)
                    core.setBandTargets (band, t[(size_t) band]);
            }
            else if (scenario == Scenario::typeChange)
            {
                // Band k changes type when its staggered 250 ms slot comes round.
                for (int band = 0; band < EqCore::numBands; ++band)
                    if ((pos + (size_t) band * quarter / 8) % quarter < (size_t) blockSize)
                        core.setBandTargets (band, mixedBand (band, 0.0, false, ++shift));
            }
            else if (scenario == Scenario::crossfadeAll)
            {
                if (pos % fade < (size_t) blockSize) // retrigger every fade: always fading
                {
                    ++shift;
                    for (int band = 0; band < EqCore::numBands; ++band)
                        core.setBandTargets (band, mixedBand (band, 0.0, false, shift));
                }
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

enum class OneBand { staticBell, dynOff, dynRest, dynActive };

double runOneBand (OneBand kind, double fs, int blockSize, int runs, double seconds)
{
    const auto n = (size_t) std::llround (seconds * fs);
    const auto hiss = noise (n, 302);
    std::vector<float> left (n), right (n);
    for (size_t i = 0; i < n; ++i)
    {
        const auto env = 0.05 + 0.45 * (0.5 + 0.5 * std::sin (2.0 * M_PI * 3.0 * (double) i / fs));
        left[i] = (float) (env * std::sin (2.0 * M_PI * 1000.0 * (double) i / fs)) + 0.1f * hiss[i];
        right[i] = 0.8f * left[i];
    }

    labq8::dsp::DynamicBellSettings settings;
    settings.frequencyHz = 1000.0;
    settings.q = 2.0;
    settings.staticGainDb = 6.0;
    settings.dynamicEnabled = kind != OneBand::dynOff;
    settings.thresholdDb = kind == OneBand::dynRest ? 20.0 : -20.0;
    settings.rangeDb = 12.0;
    settings.attackMs = 2.0;
    settings.releaseMs = 30.0;

    std::vector<double> loads;
    for (int r = 0; r < runs + 1; ++r)
    {
        labq8::dsp::EqBand staticBand;
        labq8::dsp::DynamicBellBand dynBand;
        staticBand.setTargets (BandSettings { 1000.0, 6.0, 2.0, false, FilterType::bell });
        staticBand.prepare (fs, 2);
        dynBand.setTargets (settings);
        dynBand.prepare (fs, 2);

        auto l = left, rr = right;
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t pos = 0; pos < n; pos += (size_t) blockSize)
        {
            float* ch[] = { l.data() + pos, rr.data() + pos };
            const auto len = (int) std::min<size_t> ((size_t) blockSize, n - pos);
            if (kind == OneBand::staticBell)
                staticBand.process (ch, 2, len);
            else
                dynBand.process (ch, 2, len);
        }
        const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        if (r > 0)
            loads.push_back (elapsed / seconds);
        volatile float sink = l[n / 2] + rr[n / 3];
        (void) sink;
    }

    std::sort (loads.begin(), loads.end());
    return loads[loads.size() / 2];
}

double designBellNanoseconds()
{
    constexpr int calls = 2'000'000;
    volatile double sink = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < calls; ++i)
    {
        const auto c = labq8::dsp::designBell (48000.0, 1000.0, -12.0 + 1.0e-5 * (i % 1000), 2.0);
        sink = sink + c.b0;
    }
    const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    return elapsed * 1.0e9 / calls;
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

int main (int argc, char** argv)
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
    if (argc > 1 && std::string (argv[1]) == "--e2a")
    {
        std::printf ("ONE band, stereo, 3 s per run, median of 5 runs after 1 warm-up\n\n");
        std::printf ("%-11s %8s %6s %10s %14s\n", "scenario", "fs", "block", "load %", "ns/frame");
        const struct { OneBand k; const char* name; } oneBand[] = {
            { OneBand::staticBell, "static" }, { OneBand::dynOff, "dyn-off" },
            { OneBand::dynRest, "dyn-rest" }, { OneBand::dynActive, "dyn-active" }
        };
        for (const auto& sc : oneBand)
            for (const double fs : { 48000.0, 96000.0 })
                for (const int block : { 64, 512 })
                {
                    const auto load = runOneBand (sc.k, fs, block, 5, 3.0);
                    std::printf ("%-11s %8.0f %6d %10.4f %14.2f\n", sc.name, fs, block, load * 100.0, load * 1.0e9 / fs);
                }
        std::printf ("\ndesignBell: %.1f ns per call (one per tick while the gain moves: fs / 16 calls per second)\n",
                     designBellNanoseconds());
        return 0;
    }

    std::printf ("8 bands, stereo, 10 s per run, median of 7 runs after 1 warm-up\n\n");
    const bool e1bOnly = argc > 1 && std::string (argv[1]) == "--e1b";
    std::printf ("%-11s %8s %6s %10s %14s\n", "scenario", "fs", "block", "load %", "ns/frame");

    const struct { Scenario s; const char* name; } scenarios[] = {
        { Scenario::idle, "idle" }, { Scenario::staticBands, "static" }, { Scenario::automating, "automating" },
        { Scenario::mixed, "mixed" }, { Scenario::bypassed, "bypass" }, { Scenario::typeChange, "typechange" },
        { Scenario::crossfadeAll, "xfade-all" }
    };

    for (const auto& sc : scenarios)
    {
        if (e1bOnly && ! isE1bScenario (sc.s))
            continue;

        for (const double fs : { 44100.0, 48000.0, 96000.0 })
            for (const int block : { 32, 64, 512 })
            {
                const auto load = run (sc.s, fs, block, 7, 10.0);
                std::printf ("%-11s %8.0f %6d %10.3f %14.1f\n", sc.name, fs, block, load * 100.0, load * 1.0e9 / fs);
            }
    }

    return 0;
}
