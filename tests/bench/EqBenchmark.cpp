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
// - E2b (--e2b): EIGHT bands, stereo, 3 s x 5 runs. static8 (no dynamics),
//   dyn-off8 (dynamics configured but disabled), dyn1 / dyn4 / dyn8 (that
//   many active dynamic bands, gains moving: tones amplitude-modulated across
//   the thresholds), dyn8-rest (eight detectors running, no reduction: the
//   detection cost), dyn8-auto (dyn8 plus every band and section retargeted
//   every block).
// - UI-2a (--ui2a): cost of the IN / OUT sample-peak meters (LevelMeter),
//   stereo, 1 s x 9 runs: meters (the two measurements alone, as in the
//   processor, without EQ), and static8 / dyn8 without and with the meters
//   around the EQ (runs interleaved, so both see the same machine state).
// - UI-2b (--ui2b): cost of the analyzer's capture on the audio thread
//   (SpectrumTap::write, PRE + POST, stereo), 1 s x 9 runs: taps active
//   (analyzer shown) and inactive (one flag read per block).
// - UI-2c (--ui2c): the EQ in one call per block against the processor's
//   256-sample sub-blocks (A4), without and with the dynamics telemetry
//   offered after each (DynamicsTelemetry::offer), interleaved; plus one
//   offer alone.
// - UI-2c (--ui2c-intrablock): what an end-of-block snapshot of the gain
//   reductions leaves out. The reduction of every control tick (16 samples)
//   is the reference (the EQ's output does not depend on the block size, so
//   it is the same for any host block); for each host block size it reports
//   (a) the largest difference between any tick inside a block and the
//   block's last tick, and (b) what a 30 Hz screen shows: the latest
//   completed block's value at each frame against the value at the frame's
//   instant, and the largest reduction each burst reaches on the screen
//   against its true peak.
// - Load = CPU seconds / audio seconds on one core, single-threaded.
// Build in Release; Debug numbers are printed with a warning.

#include "dsp/DynamicBellBand.h"
#include "dsp/EqCore.h"
#include "plugin/DynamicsTelemetry.h"
#include "plugin/LevelMeter.h"
#include "plugin/SpectrumTap.h"

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

enum class EightDyn { static8, dynOff8, dyn1, dyn4, dyn8, dyn8Rest, dyn8Auto };

double runEightDynamic (EightDyn kind, double fs, int blockSize, int runs, double seconds)
{
    constexpr double freqs[8] = { 60.0, 150.0, 400.0, 1000.0, 2500.0, 5000.0, 9000.0, 15000.0 };
    const auto n = (size_t) std::llround (seconds * fs);
    const auto hiss = noise (n, 303);
    std::vector<float> left (n, 0.0f);
    for (int b = 0; b < 8; ++b)
        for (size_t i = 0; i < n; ++i)
        {
            const auto env = std::pow (10.0, (-36.0 + 30.0 * (0.5 + 0.5 * std::sin (2.0 * M_PI * (0.7 + b) * (double) i / fs))) / 20.0);
            left[i] += (float) (env * std::sin (2.0 * M_PI * freqs[b] * (double) i / fs));
        }
    for (size_t i = 0; i < n; ++i)
        left[i] += 0.05f * hiss[i];
    const auto right = left;

    const int activeBands = kind == EightDyn::dyn1 ? 1 : kind == EightDyn::dyn4 ? 4 : 8;
    std::vector<double> loads;
    for (int r = 0; r < runs + 1; ++r)
    {
        EqCore core;
        for (int b = 0; b < 8; ++b)
        {
            core.setBandTargets (b, BandSettings { freqs[b], 2.0, 3.0, false, FilterType::bell });
            if (kind != EightDyn::static8)
            {
                labq8::dsp::DynamicSettings d;
                d.enabled = kind != EightDyn::dynOff8 && b < activeBands;
                d.thresholdDb = kind == EightDyn::dyn8Rest ? 20.0 : -24.0;
                d.rangeDb = 15.0;
                d.attackMs = 2.0;
                d.releaseMs = 30.0;
                core.setBandDynamics (b, d);
            }
        }
        core.prepare (fs, 2);

        auto l = left, rr = right;
        int block = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t pos = 0; pos < n; pos += (size_t) blockSize, ++block)
        {
            if (kind == EightDyn::dyn8Auto)
                for (int b = 0; b < 8; ++b)
                {
                    const auto odd = (block + b) % 2 == 1;
                    core.setBandTargets (b, BandSettings { freqs[b] * (odd ? 1.05 : 1.0), odd ? 3.0 : 2.0, odd ? 2.5 : 3.0, false, FilterType::bell });
                    labq8::dsp::DynamicSettings d;
                    d.enabled = true;
                    d.thresholdDb = odd ? -22.0 : -26.0;
                    d.rangeDb = odd ? 12.0 : 15.0;
                    d.attackMs = odd ? 1.0 : 3.0;
                    d.releaseMs = odd ? 20.0 : 40.0;
                    core.setBandDynamics (b, d);
                }
            float* ch[] = { l.data() + pos, rr.data() + pos };
            core.process (ch, 2, (int) std::min<size_t> ((size_t) blockSize, n - pos));
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

// UI-2a: what the processor adds around the EQ: one meter before, one after.
enum class Metering { metersOnly, eqOnly, eqAndMeters };

struct MeteringResult { double load; float sink; };

MeteringResult runMetering (Metering kind, bool dynamics, double fs, int blockSize, double seconds,
                            const std::vector<float>& left, const std::vector<float>& right)
{
    constexpr double freqs[8] = { 60.0, 150.0, 400.0, 1000.0, 2500.0, 5000.0, 9000.0, 15000.0 };
    const auto n = left.size();
    EqCore core;
    for (int b = 0; b < 8; ++b)
    {
        core.setBandTargets (b, BandSettings { freqs[b], 2.0, 3.0, false, FilterType::bell });
        labq8::dsp::DynamicSettings d;
        d.enabled = dynamics;
        d.thresholdDb = -24.0;
        d.rangeDb = 15.0;
        d.attackMs = 2.0;
        d.releaseMs = 30.0;
        core.setBandDynamics (b, d);
    }
    core.prepare (fs, 2);
    labq8::LevelMeter in, out;

    auto l = left, rr = right;
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t pos = 0; pos < n; pos += (size_t) blockSize)
    {
        const auto count = (int) std::min<size_t> ((size_t) blockSize, n - pos);
        float* ch[] = { l.data() + pos, rr.data() + pos };
        if (kind != Metering::eqOnly)
            in.measure (ch, 2, count);
        if (kind != Metering::metersOnly)
            core.process (ch, 2, count);
        if (kind != Metering::eqOnly)
            out.measure (ch, 2, count);
    }
    const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    return { elapsed / seconds, l[n / 2] + rr[n / 3] + in.takePeak (0) + out.takePeak (1) };
}

// UI-2b: the analyzer's capture, as the processor does it (two taps).
double runCapture (bool active, double fs, int blockSize, const std::vector<float>& left, const std::vector<float>& right)
{
    labq8::SpectrumTap pre, post;
    pre.prepare (fs, 2);
    post.prepare (fs, 2);
    pre.setActive (active);
    post.setActive (active);
    const auto n = left.size();
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t pos = 0; pos < n; pos += (size_t) blockSize)
    {
        const auto count = (int) std::min<size_t> ((size_t) blockSize, n - pos);
        const float* ch[] = { left.data() + pos, right.data() + pos };
        pre.write (ch, 2, count);
        post.write (ch, 2, count);
    }
    const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    volatile auto keep = pre.getWritten() + post.getWritten();
    (void) keep;
    return elapsed / ((double) n / fs);
}

// UI-2c: the processor around an 8-band dynamic EQ: the EQ in one call per
// block (before A4), in sub-blocks of 256 (A4), and in sub-blocks with the
// telemetry offered after each (A4 + A2, editor showing the dynamics).
enum class Telemetry { wholeBlock, subBlocks, subBlocksOffered };

void offerFrom (labq8::DynamicsTelemetry& telemetry, const EqCore& core)
{
    telemetry.offer (false, [&core] (int b, labq8::DynamicsTelemetry::Band& out)
    {
        const auto& band = core.getBand (b);
        out.staticGainDb = (float) band.getCurrentGainDb();
        out.reductionDb = (float) core.getDynamics (b).getGainReductionDb();
        out.frequencyHz = (float) band.getCurrentFrequencyHz();
        out.q = (float) band.getCurrentQ();
        out.bell = band.getActiveFilter() == FilterType::bell;
    });
}

double runTelemetry (Telemetry kind, bool dynamics, double fs, int blockSize, const std::vector<float>& left, const std::vector<float>& right)
{
    constexpr double freqs[8] = { 60.0, 150.0, 400.0, 1000.0, 2500.0, 5000.0, 9000.0, 15000.0 };
    EqCore core;
    for (int b = 0; b < 8; ++b)
    {
        core.setBandTargets (b, BandSettings { freqs[b], 2.0, 3.0, false, FilterType::bell });
        core.setBandDynamics (b, labq8::dsp::DynamicSettings { dynamics, -30.0, 12.0, 2.0, 30.0 });
    }
    core.prepare (fs, 2);
    labq8::DynamicsTelemetry telemetry;
    telemetry.setActive (kind == Telemetry::subBlocksOffered);
    auto l = left, r = right;
    const auto n = l.size();
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t pos = 0; pos < n; pos += (size_t) blockSize)
    {
        const auto count = (int) std::min<size_t> ((size_t) blockSize, n - pos);
        if (kind == Telemetry::wholeBlock)
        {
            float* ch[] = { l.data() + pos, r.data() + pos };
            core.process (ch, 2, count);
            continue;
        }
        for (int sub = 0; sub < count; sub += 256)
        {
            float* ch[] = { l.data() + pos + (size_t) sub, r.data() + pos + (size_t) sub };
            core.process (ch, 2, std::min (256, count - sub));
            offerFrom (telemetry, core); // inactive: one flag read
        }
    }
    const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    labq8::DynamicsTelemetry::Snapshot s;
    volatile auto keep = l[n / 2] + (telemetry.peek (s) ? s.bands[0].reductionDb : 0.0f);
    (void) keep;
    return elapsed / ((double) n / fs);
}

// UI-2c: one offer alone (its share of the EQ is below the run-to-run
// noise): median ns per offer over 9 runs of 200000, active or inactive.
double publishNanoseconds (bool active)
{
    EqCore core;
    for (int b = 0; b < 8; ++b)
        core.setBandDynamics (b, labq8::dsp::DynamicSettings { true, -30.0, 12.0, 2.0, 30.0 });
    core.prepare (48000.0, 2);
    labq8::DynamicsTelemetry telemetry;
    telemetry.setActive (active);
    std::vector<double> runs;
    for (int r = 0; r < 10; ++r)
    {
        constexpr int count = 200000;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < count; ++i)
            offerFrom (telemetry, core);
        if (r > 0)
            runs.push_back (std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - t0).count() / count);
    }
    std::sort (runs.begin(), runs.end());
    labq8::DynamicsTelemetry::Snapshot s;
    volatile auto keep = telemetry.peek (s) ? s.id : 0u;
    (void) keep;
    return runs[runs.size() / 2];
}

// UI-2c: the reduction of band 1 at every control tick for a signal.
struct IntraSignal { const char* name; float (*sample) (int); };

std::vector<double> tickReductions (const IntraSignal& signal, double attackMs, double releaseMs, double fs, int seconds)
{
    EqCore core;
    core.setBandTargets (0, BandSettings { 1000.0, 0.0, 1.0, false, FilterType::bell });
    core.setBandDynamics (0, labq8::dsp::DynamicSettings { true, -30.0, 12.0, attackMs, releaseMs });
    core.prepare (fs, 2);
    const auto total = (int) fs * seconds;
    std::vector<double> reductions;
    float l[16], r[16];
    for (int pos = 0; pos < total; pos += 16)
    {
        for (int i = 0; i < 16; ++i)
            l[i] = r[i] = signal.sample (pos + i);
        float* ch[] = { l, r };
        core.process (ch, 2, 16); // one tick per call: its reduction applies to these 16 samples
        reductions.push_back (core.getDynamics (0).getGainReductionDb());
    }
    return reductions;
}

constexpr double benchPi = 3.14159265358979323846;
// 1 kHz bursts at -6 dBFS, 30 ms on / 70 ms off (hard edges: the worst case).
float hardBursts (int i) { return (i % 4800) < 1440 ? (float) (0.5 * std::sin (2.0 * benchPi * 1000.0 * i / 48000.0)) : 0.0f; }
// Percussive: 1 kHz hits every 250 ms, -6 dBFS, decaying with a 40 ms time constant.
float hits (int i) { const auto t = (i % 12000) / 48000.0; return (float) (0.5 * std::exp (-t / 0.04) * std::sin (2.0 * benchPi * 1000.0 * i / 48000.0)); }
// Slow: 1 kHz at -6 dBFS with 4 Hz tremolo, 24 dB deep.
float tremolo (int i)
{
    const auto depthDb = -12.0 + 12.0 * std::sin (2.0 * benchPi * 4.0 * i / 48000.0);
    return (float) (0.5 * std::pow (10.0, depthDb / 20.0) * std::sin (2.0 * benchPi * 1000.0 * i / 48000.0));
}

void intraBlockReport()
{
    constexpr double fs = 48000.0, frame = fs / 30.0, pxPerDb = 37.0 / 8.0;
    const IntraSignal signals[] = { { "bursts", hardBursts }, { "hits", hits }, { "tremolo", tremolo } };
    const struct { double attack, release; } times[] = { { 0.5, 5.0 }, { 0.5, 50.0 }, { 10.0, 100.0 }, { 50.0, 500.0 } };
    std::printf ("UI-2c intra-block: band 1 Bell 1 kHz Q 1, 0 dB, threshold -30, range 12 dB, 48 kHz, 8 s per case\n");
    std::printf ("(a) max |tick in block - block end| dB; (b) screen at 30 Hz: max |shown - true at frame| dB and its 99th percentile;\n");
    std::printf ("(c) burst peaks: mean and worst shortfall of the largest reduction shown vs the true peak, dB (block 16 = ticks, the ideal)\n");
    std::printf ("1 dB = %.2f reference px (%.2f physical px at 60 %% on Retina, %.2f at 100 %%)\n\n", pxPerDb, pxPerDb * 1.2, pxPerDb * 2.0);
    std::printf ("%-8s %-9s %6s %8s %8s %8s %9s %9s\n", "signal", "atk/rel", "block", "(a)max", "(b)max", "(b)p99", "(c)mean", "(c)worst");
    for (const auto& signal : signals)
        for (const auto& t : times)
        {
            const auto ticks = tickReductions (signal, t.attack, t.release, fs, 8);
            const auto numTicks = (int) ticks.size();
            const auto period = signal.sample == hits ? 12000 : signal.sample == hardBursts ? 4800 : 12000;
            for (const int block : { 16, 64, 256, 512, 1024, 2048, 4096 })
            {
                const auto ticksPerBlock = block / 16;
                double intra = 0.0;
                for (int start = 0; start + ticksPerBlock <= numTicks; start += ticksPerBlock)
                {
                    const auto endValue = ticks[(size_t) (start + ticksPerBlock - 1)];
                    for (int k = start; k < start + ticksPerBlock; ++k)
                        intra = std::max (intra, std::abs (ticks[(size_t) k] - endValue));
                }
                // The screen: at a frame at sample time T it shows the last tick of the
                // latest block that ended by T; the truth is the tick in effect at T.
                std::vector<double> errors;
                std::vector<double> shownPeak ((size_t) (numTicks * 16 / period + 1), 0.0), truePeak (shownPeak.size(), 0.0);
                for (int k = 0; k < numTicks; ++k)
                    truePeak[(size_t) (k * 16 / period)] = std::max (truePeak[(size_t) (k * 16 / period)], ticks[(size_t) k]);
                for (double T = frame * 0.37; T < numTicks * 16.0; T += frame)
                {
                    const auto lastBlockEnd = (int) (std::floor (T / block) * block);
                    if (lastBlockEnd < block)
                        continue;
                    const auto shown = ticks[(size_t) (lastBlockEnd / 16 - 1)];
                    const auto truth = ticks[(size_t) std::min (numTicks - 1, (int) (T / 16.0))];
                    errors.push_back (std::abs (shown - truth));
                    const auto burst = (size_t) (T / period);
                    shownPeak[burst] = std::max (shownPeak[burst], shown);
                }
                std::sort (errors.begin(), errors.end());
                double shortfallSum = 0.0, worst = 0.0;
                int counted = 0;
                for (size_t b = 1; b + 1 < shownPeak.size(); ++b) // whole bursts only
                {
                    const auto d = truePeak[b] - shownPeak[b];
                    shortfallSum += d;
                    worst = std::max (worst, d);
                    ++counted;
                }
                char label[16];
                std::snprintf (label, sizeof (label), "%g/%g", t.attack, t.release);
                std::printf ("%-8s %-9s %6d %8.2f %8.2f %8.2f %9.2f %9.2f\n", signal.name, label, block, intra,
                             errors.back(), errors[(size_t) (0.99 * (double) (errors.size() - 1))],
                             counted > 0 ? shortfallSum / counted : 0.0, worst);
            }
        }
}

// UI-2c: burst-peak shortfall on a 30 Hz screen for the snapshot policies.
// Snapshots are taken every `granularity` samples (block ends; or sub-block
// ends; or every tick); "latest" shows the newest at each frame, "peak" the
// one with the largest reduction since the previous frame (one coherent instant).
struct Shortfall { double mean, worst; };

Shortfall burstShortfall (const std::vector<double>& ticks, int period, int granularity, bool peak)
{
    constexpr double fs = 48000.0, frame = fs / 30.0;
    const auto numTicks = (int) ticks.size();
    const auto step = granularity / 16;
    std::vector<double> shownPeak ((size_t) (numTicks * 16 / period + 1), 0.0), truePeak (shownPeak.size(), 0.0);
    for (int k = 0; k < numTicks; ++k)
        truePeak[(size_t) (k * 16 / period)] = std::max (truePeak[(size_t) (k * 16 / period)], ticks[(size_t) k]);
    int nextSnapshot = step - 1; // tick index of the next snapshot
    double held = 0.0, latest = 0.0;
    bool any = false;
    for (double T = frame * 0.37; T < numTicks * 16.0; T += frame)
    {
        while (nextSnapshot < numTicks && (nextSnapshot + 1) * 16 <= T)
        {
            const auto v = ticks[(size_t) nextSnapshot];
            latest = v;
            held = any ? std::max (held, v) : v;
            any = true;
            nextSnapshot += step;
        }
        const auto shown = peak ? (any ? held : latest) : latest;
        any = false;
        const auto burst = (size_t) (T / period);
        shownPeak[burst] = std::max (shownPeak[burst], shown);
    }
    double sum = 0.0, worst = 0.0;
    int counted = 0;
    for (size_t b = 1; b + 1 < shownPeak.size(); ++b)
    {
        const auto d = truePeak[b] - shownPeak[b];
        sum += d;
        worst = std::max (worst, d);
        ++counted;
    }
    return { counted > 0 ? sum / counted : 0.0, worst };
}

void policyReport()
{
    constexpr double fs = 48000.0;
    const IntraSignal signals[] = { { "bursts", hardBursts }, { "hits", hits }, { "tremolo", tremolo } };
    const struct { double attack, release; } times[] = { { 0.5, 5.0 }, { 0.5, 50.0 }, { 10.0, 100.0 }, { 50.0, 500.0 } };
    std::printf ("\nUI-2c policies: burst-peak shortfall on the 30 Hz screen, mean / worst dB\n");
    std::printf ("  latest = newest block-end snapshot (the implemented one); A2 = most reducing block-end snapshot since the\n");
    std::printf ("  previous frame; A4 = A2 with the processor calling the EQ in sub-blocks of <= 256; A3 = A2 per tick (DSP change)\n\n");
    std::printf ("%-8s %-9s %6s %13s %13s %13s %13s\n", "signal", "atk/rel", "block", "latest", "A2", "A4 (256)", "A3 (tick)");
    for (const auto& signal : signals)
        for (const auto& t : times)
        {
            const auto ticks = tickReductions (signal, t.attack, t.release, fs, 8);
            const auto period = signal.sample == hardBursts ? 4800 : 12000;
            for (const int block : { 256, 512, 1024, 2048, 4096 })
            {
                const auto a = burstShortfall (ticks, period, block, false);
                const auto b = burstShortfall (ticks, period, block, true);
                const auto c = burstShortfall (ticks, period, std::min (block, 256), true);
                const auto d = burstShortfall (ticks, period, 16, true);
                char label[16];
                std::snprintf (label, sizeof (label), "%g/%g", t.attack, t.release);
                std::printf ("%-8s %-9s %6d %6.2f/%6.2f %6.2f/%6.2f %6.2f/%6.2f %6.2f/%6.2f\n", signal.name, label, block,
                             a.mean, a.worst, b.mean, b.worst, c.mean, c.worst, d.mean, d.worst);
            }
        }
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

    if (argc > 1 && std::string (argv[1]) == "--e2b")
    {
        std::printf ("EIGHT bands, stereo, 3 s per run, median of 5 runs after 1 warm-up\n\n");
        std::printf ("%-11s %8s %6s %10s %14s\n", "scenario", "fs", "block", "load %", "ns/frame");
        const struct { EightDyn k; const char* name; } eight[] = {
            { EightDyn::static8, "static8" }, { EightDyn::dynOff8, "dyn-off8" }, { EightDyn::dyn1, "dyn1" },
            { EightDyn::dyn4, "dyn4" }, { EightDyn::dyn8, "dyn8" }, { EightDyn::dyn8Rest, "dyn8-rest" },
            { EightDyn::dyn8Auto, "dyn8-auto" }
        };
        for (const auto& sc : eight)
            for (const double fs : { 44100.0, 48000.0, 96000.0 })
                for (const int block : { 64, 512 })
                {
                    const auto load = runEightDynamic (sc.k, fs, block, 5, 3.0);
                    std::printf ("%-11s %8.0f %6d %10.4f %14.2f\n", sc.name, fs, block, load * 100.0, load * 1.0e9 / fs);
                }
        return 0;
    }

    if (argc > 1 && std::string (argv[1]) == "--ui2c-intrablock")
    {
        intraBlockReport();
        policyReport();
        return 0;
    }

    if (argc > 1 && std::string (argv[1]) == "--ui2c")
    {
        std::printf ("UI-2c: 8-band EQ, stereo, 1 s per run, median of 9 after 1 warm-up (interleaved)\n");
        std::printf ("  whole = one EQ call per block (before A4); A4 = sub-blocks of 256; A4+offer = with the telemetry offered after each\n\n");
        std::printf ("%-8s %8s %6s %10s %10s %12s\n", "dynamics", "fs", "block", "whole %", "A4 %", "A4+offer %");
        for (const bool dynamics : { false, true })
            for (const double fs : { 48000.0, 96000.0 })
                for (const int block : { 64, 512, 2048 })
                {
                    const auto n = (size_t) std::llround (fs);
                    const auto left = noise (n, 601), right = noise (n, 602);
                    std::vector<double> whole, sub, offered;
                    for (int r = 0; r < 10; ++r)
                    {
                        const auto a = runTelemetry (Telemetry::wholeBlock, dynamics, fs, block, left, right);
                        const auto b = runTelemetry (Telemetry::subBlocks, dynamics, fs, block, left, right);
                        const auto c = runTelemetry (Telemetry::subBlocksOffered, dynamics, fs, block, left, right);
                        if (r > 0)
                        {
                            whole.push_back (a);
                            sub.push_back (b);
                            offered.push_back (c);
                        }
                    }
                    for (auto* v : { &whole, &sub, &offered })
                        std::sort (v->begin(), v->end());
                    std::printf ("%-8s %8.0f %6d %10.4f %10.4f %12.4f\n", dynamics ? "dyn8" : "static8", fs, block,
                                 whole[whole.size() / 2] * 100.0, sub[sub.size() / 2] * 100.0, offered[offered.size() / 2] * 100.0);
                }
        const auto activeNs = publishNanoseconds (true), inactiveNs = publishNanoseconds (false);
        std::printf ("\noffer alone: %.1f ns active, %.2f ns inactive; at most one per 256 samples (fewer with smaller blocks: one per block)\n",
                     activeNs, inactiveNs);
        for (const int block : { 32, 64, 256 })
            std::printf ("  at 48 kHz, one offer per %3d samples: %.5f %% of one core active\n", block, activeNs * 48000.0 / block * 1.0e-7);
        return 0;
    }

    if (argc > 1 && std::string (argv[1]) == "--ui2b")
    {
        std::printf ("UI-2b analyzer capture (PRE + POST taps), stereo, 1 s per run, median of 9 after 1 warm-up\n\n");
        std::printf ("%-10s %8s %6s %12s %14s\n", "taps", "fs", "block", "load %", "ns/frame");
        for (const bool active : { true, false })
            for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
                for (const int block : { 32, 64, 512 })
                {
                    const auto n = (size_t) std::llround (fs);
                    const auto left = noise (n, 501), right = noise (n, 502);
                    std::vector<double> loads;
                    for (int r = 0; r < 10; ++r)
                    {
                        const auto load = runCapture (active, fs, block, left, right);
                        if (r > 0)
                            loads.push_back (load);
                    }
                    std::sort (loads.begin(), loads.end());
                    const auto m = loads[loads.size() / 2];
                    std::printf ("%-10s %8.0f %6d %12.5f %14.3f\n", active ? "active" : "inactive", fs, block, m * 100.0, m * 1.0e9 / fs);
                }
        return 0;
    }

    if (argc > 1 && std::string (argv[1]) == "--ui2a")
    {
        std::printf ("UI-2a meters, stereo, 1 s per run, median of 9 runs after 1 warm-up (interleaved)\n\n");
        std::printf ("%-15s %8s %6s %10s %10s %10s %14s\n", "scenario", "fs", "block", "EQ %", "EQ+met %", "meters %", "meters ns/fr");
        float sink = 0.0f;
        for (const bool dynamics : { false, true })
            for (const double fs : { 48000.0, 96000.0 })
                for (const int block : { 32, 64, 512 })
                {
                    const auto n = (size_t) std::llround (1.0 * fs);
                    auto left = noise (n, 404), right = noise (n, 405);
                    std::vector<double> eq, both, meters;
                    for (int r = 0; r < 10; ++r)
                    {
                        const auto a = runMetering (Metering::eqOnly, dynamics, fs, block, 1.0, left, right);
                        const auto b = runMetering (Metering::eqAndMeters, dynamics, fs, block, 1.0, left, right);
                        const auto c = runMetering (Metering::metersOnly, dynamics, fs, block, 1.0, left, right);
                        sink += a.sink + b.sink + c.sink;
                        if (r > 0)
                        {
                            eq.push_back (a.load);
                            both.push_back (b.load);
                            meters.push_back (c.load);
                        }
                    }
                    for (auto* v : { &eq, &both, &meters })
                        std::sort (v->begin(), v->end());
                    const auto m = meters[meters.size() / 2];
                    std::printf ("%-15s %8.0f %6d %10.4f %10.4f %10.5f %14.2f\n", dynamics ? "dyn8" : "static8", fs, block,
                                 eq[eq.size() / 2] * 100.0, both[both.size() / 2] * 100.0, m * 100.0, m * 1.0e9 / fs);
                }
        volatile float keep = sink;
        (void) keep;
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
