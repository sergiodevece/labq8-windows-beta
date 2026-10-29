// E2b eight-band dynamic engine tests (numbers refer to the E2b test list):
// eight simultaneous dynamic Bells with their own threshold / range (1, 6),
// independent detectors and no feedback (2, 3), one dynamic band among
// static ones (4), automation of eight sections with any block size (5, 12),
// independent attack / release (7), max-linked stereo and panning (8, 9),
// type changes (10), enable and bypass during playback (11), extreme
// frequency and Q (13), NaN / Inf / allocations (14), above 0 dBFS (15),
// several bands modulating against a per-sample reference (16), CPU.

#pragma clang diagnostic ignored "-Wfloat-equal" // bit-exactness is a tested property

#include "AllocationGuard.h"
#include "TestHarness.h"
#include "TestSignals.h"

#include "dsp/DynamicBellBand.h"
#include "dsp/EqCore.h"

#include <chrono>
#include <limits>
#include <random>

using namespace labq8::dsp;
using namespace labq8::test::signals;
using labq8::test::ScopedAllocationCounter;
using labq8::test::str;

namespace
{
constexpr int tick = EqBand::updateIntervalSamples;
constexpr int numBands = EqCore::numBands;
constexpr double bandFreqs[numBands] = { 60.0, 150.0, 400.0, 1000.0, 2500.0, 5000.0, 9000.0, 15000.0 };

double dbToAmp (double db) { return std::pow (10.0, db / 20.0); }

BandSettings bell (int b, double gain = 0.0, double q = 4.0) { return { bandFreqs[b], gain, q, false, FilterType::bell }; }

DynamicSettings dynOn (double threshold, double range, double attack = 5.0, double release = 50.0)
{
    return { true, threshold, range, attack, release };
}

using Traces = std::array<std::vector<double>, numBands>;

// Processes tick by tick (mono or stereo) and records each band's reduction.
Traces runTraces (EqCore& core, std::vector<Signal>& x)
{
    Traces gr;
    const auto n = x[0].size();
    for (size_t pos = 0; pos < n; pos += tick)
    {
        float* ch[2] = { x[0].data() + pos, x.size() > 1 ? x[1].data() + pos : nullptr };
        core.process (ch, (int) x.size(), (int) std::min<size_t> (tick, n - pos));
        for (int b = 0; b < numBands; ++b)
            gr[(size_t) b].push_back (core.getDynamics (b).getGainReductionDb());
    }
    return gr;
}

void runBlocks (EqCore& core, std::vector<Signal>& x, int block)
{
    const auto n = x[0].size();
    for (size_t pos = 0; pos < n; pos += (size_t) block)
    {
        float* ch[2] = { x[0].data() + pos, x.size() > 1 ? x[1].data() + pos : nullptr };
        core.process (ch, (int) x.size(), (int) std::min<size_t> ((size_t) block, n - pos));
    }
}

Signal tones (double fs, size_t n, const double (&levelsDb)[numBands])
{
    Signal s (n, 0.0f);
    for (int b = 0; b < numBands; ++b)
        if (bandFreqs[b] < 0.45 * fs)
            for (size_t i = 0; i < n; ++i)
                s[i] += (float) (dbToAmp (levelsDb[b]) * std::sin (2.0 * std::numbers::pi * bandFreqs[b] * (double) i / fs + b));
    return s;
}

double maxFourthDifference (const Signal& y, size_t begin, size_t end)
{
    double m = 0.0;
    for (auto i = std::max<size_t> (begin, 4); i < std::min (end, y.size()); ++i)
        m = std::max (m, std::abs ((double) y[i] - 4.0 * y[i - 1] + 6.0 * y[i - 2] - 4.0 * y[i - 3] + y[i - 4]));
    return m;
}

struct CoreEvent
{
    size_t position;
    int band;
    bool isDynamics;
    BandSettings settings;
    DynamicSettings dynamics;
};

std::vector<CoreEvent> randomCoreEvents (double fs, size_t until, unsigned seed, bool typeChanges)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    std::uniform_int_distribution<int> gap (1, 100);
    std::vector<CoreEvent> events;
    for (size_t pos = 0; pos < until; pos += (size_t) gap (rng))
    {
        const int b = (int) (u (rng) * 7.999);
        if (u (rng) < 0.5)
            events.push_back ({ pos, b, true, {}, { u (rng) < 0.85, -60.0 + 50.0 * u (rng), 24.0 * u (rng), 0.5 + 40.0 * u (rng), 5.0 + 300.0 * u (rng) } });
        else
            events.push_back ({ pos, b, false,
                                { 30.0 * std::pow (600.0, u (rng)), -12.0 + 24.0 * u (rng), 0.2 * std::pow (60.0, u (rng)), u (rng) < 0.1,
                                  typeChanges && u (rng) < 0.3 ? filterTypeFromIndex ((int) (u (rng) * 5.999)) : FilterType::bell },
                                {} });
    }
    (void) fs;
    return events;
}

template <typename NextBlock>
std::vector<Signal> renderCore (double fs, std::vector<Signal> x, const std::vector<CoreEvent>& events, NextBlock nextBlock)
{
    EqCore core;
    for (int b = 0; b < numBands; ++b)
    {
        core.setBandTargets (b, bell (b, 3.0, 2.0));
        core.setBandDynamics (b, dynOn (-30.0, 9.0));
    }
    core.prepare (fs, (int) x.size());
    const auto n = x[0].size();
    size_t pos = 0, next = 0;
    while (pos < n)
    {
        while (next < events.size() && events[next].position <= pos)
        {
            const auto& e = events[next++];
            if (e.isDynamics)
                core.setBandDynamics (e.band, e.dynamics);
            else
                core.setBandTargets (e.band, e.settings);
        }
        auto len = std::min ((size_t) nextBlock(), n - pos);
        if (next < events.size())
            len = std::min (len, events[next].position - pos);
        float* ch[2] = { x[0].data() + pos, x.size() > 1 ? x[1].data() + pos : nullptr };
        core.process (ch, (int) x.size(), (int) len);
        pos += len;
    }
    return x;
}
} // namespace

//==============================================================================
LQ_TEST ("dyn8: eight dynamic Bells at once, each with its own threshold and range (1, 6)")
{
    constexpr double fs = 48000.0;
    const double levels[numBands]     = { -20.0, -26.0, -32.0, -18.0, -24.0, -28.0, -22.0, -30.0 };
    const double thresholds[numBands] = { -30.0, -30.0, -40.0, -24.0, -36.0, -26.0, -28.0, -50.0 };
    const double ranges[numBands]     = { 12.0, 3.0, 6.0, 24.0, 9.0, 12.0, 4.0, 15.0 };
    const double gains[numBands]      = { 0.0, 3.0, -3.0, 0.0, 6.0, 0.0, -6.0, 2.0 };

    EqCore core;
    for (int b = 0; b < numBands; ++b)
    {
        core.setBandTargets (b, bell (b, gains[b]));
        core.setBandDynamics (b, dynOn (thresholds[b], ranges[b]));
    }
    core.prepare (fs, 1);
    std::vector<Signal> x { tones (fs, samples (1.0, fs), levels) };
    const auto input = x[0];
    const auto gr = runTraces (core, x);

    // Steady reduction per band: the law for that band's own tone. 0.15 dB:
    // detector ripple (0.11 dB) plus neighbouring tones leaking through the
    // 4th-order band-pass (Q 4, >= 1.3 octaves apart: below -35 dB).
    double steady[numBands] {};
    for (int b = 0; b < numBands; ++b)
    {
        const auto expected = dynamics::gainReductionDb (levels[b], thresholds[b], ranges[b]);
        const auto& g = gr[(size_t) b];
        double lo = 1e9, hi = -1e9;
        for (auto t = g.size() / 2; t < g.size(); ++t)
        {
            lo = std::min (lo, g[t]);
            hi = std::max (hi, g[t]);
        }
        steady[b] = 0.5 * (lo + hi);
        LQ_CHECK (std::abs (lo - expected) < 0.15 && std::abs (hi - expected) < 0.15 && hi <= ranges[b],
                  str ("band ", b + 1, ": reduction ", lo, " .. ", hi, " expected ", expected));
    }

    // Output at each tone = the cascade of the eight Bells at their steady
    // gains (static gain - own reduction), within 0.15 dB.
    for (int b = 0; b < numBands; ++b)
    {
        double expectedDb = 0.0;
        for (int k = 0; k < numBands; ++k)
            expectedDb += toDb (magnitudeAt (designBell (fs, bandFreqs[k], gains[k] - steady[k], 4.0), bandFreqs[b], fs));
        const auto begin = x[0].size() / 2;
        const auto measured = toDb (fitAmplitude (x[0], begin, x[0].size(), bandFreqs[b], fs)
                                    / fitAmplitude (input, begin, input.size(), bandFreqs[b], fs));
        LQ_CHECK (std::abs (measured - expectedDb) < 0.15, str ("tone ", bandFreqs[b], " Hz: ", measured, " dB, cascade predicts ", expectedDb));
    }
}

LQ_TEST ("dyn8: detectors read the common original input: independent of every other band, no feedback (2, 3)")
{
    constexpr double fs = 48000.0;
    const double levels[numBands] = { -18.0, -18.0, -18.0, -18.0, -18.0, -18.0, -18.0, -18.0 };
    const auto n = samples (0.8, fs);
    std::vector<Signal> base { tones (fs, n, levels) };
    const auto noise = makeNoise (n, 201, 0.05f);
    for (size_t i = 0; i < n; ++i)
        base[0][i] += noise[i];

    // Reference: band 4 (1 kHz) dynamic, every other band flat and static.
    auto traceOfBand4 = [&] (auto configureOthers)
    {
        EqCore core;
        core.setBandTargets (3, bell (3, 0.0));
        core.setBandDynamics (3, dynOn (-30.0, 12.0, 3.0, 80.0));
        configureOthers (core);
        core.prepare (fs, 1);
        auto x = base;
        return runTraces (core, x)[3];
    };
    const auto alone = traceOfBand4 ([] (EqCore&) {});

    // Other bands boost the same frequency by +24 dB (before and after band 4
    // in the cascade), cut it, or are dynamic themselves.
    const auto boosted = traceOfBand4 ([] (EqCore& c)
    {
        c.setBandTargets (0, { 1000.0, 24.0, 4.0, false, FilterType::bell });
        c.setBandTargets (7, { 1000.0, 24.0, 1.0, false, FilterType::bell });
    });
    const auto cutAndDynamic = traceOfBand4 ([] (EqCore& c)
    {
        c.setBandTargets (2, { 1000.0, -24.0, 1.0, false, FilterType::bell });
        for (const int b : { 0, 1, 4, 5, 6, 7 })
        {
            c.setBandTargets (b, { bandFreqs[b], 6.0, 2.0, false, FilterType::bell });
            c.setBandDynamics (b, dynOn (-40.0, 24.0, 1.0, 20.0));
        }
    });
    LQ_CHECK (boosted == alone, "band 4's reduction must not see the +24 dB boosts of bands 1 and 8 (no feedback)");
    LQ_CHECK (cutAndDynamic == alone, "band 4's reduction must not depend on what the other bands do");

    // And it equals the E2a single band on the same input (the same
    // DynamicSection, fed the same samples).
    DynamicBellBand single;
    single.setTargets ({ 1000.0, 4.0, 0.0, true, -30.0, 12.0, 3.0, 80.0 });
    single.prepare (fs, 1);
    auto x = base;
    std::vector<double> singleTrace;
    for (size_t pos = 0; pos < n; pos += tick)
    {
        float* ch[] = { x[0].data() + pos };
        single.process (ch, 1, (int) std::min<size_t> (tick, n - pos));
        singleTrace.push_back (single.getGainReductionDb());
    }
    LQ_CHECK (singleTrace == alone, "band 4 inside the core must behave exactly like the E2a DynamicBellBand");
    LQ_CHECK (*std::max_element (alone.begin(), alone.end()) > 5.0, "the reference reduction is substantial");
}

LQ_TEST ("dyn8: one dynamic band among seven static ones equals an independent per-segment reference (4)")
{
    for (const auto fs : { 44100.0, 96000.0 })
        for (const int dynBand : { 0, 3, 7 })
        {
            const auto n = samples (0.6, fs);
            const double levels[numBands] = { -12.0, -20.0, -15.0, -10.0, -18.0, -14.0, -16.0, -22.0 };
            auto input = tones (fs, n, levels);
            const double gains[numBands] = { 4.0, -3.0, 2.0, -6.0, 5.0, -2.0, 3.0, -4.0 };
            const auto dynSettings = dynOn (-25.0, 10.0, 2.0, 40.0);

            EqCore core;
            for (int b = 0; b < numBands; ++b)
                core.setBandTargets (b, bell (b, gains[b], 2.0));
            core.setBandDynamics (dynBand, dynSettings);
            core.prepare (fs, 1);
            std::vector<Signal> x { input };
            runBlocks (core, x, 333);

            // Reference built from parts: eight EqBands in cascade per 16-sample
            // segment, one DynamicSection fed the untouched segment first.
            std::array<EqBand, numBands> bands;
            DynamicSection section;
            for (int b = 0; b < numBands; ++b)
            {
                bands[(size_t) b].setTargets (bell (b, gains[b], 2.0));
                bands[(size_t) b].prepare (fs, 1);
            }
            section.setTargets (dynSettings);
            section.prepare (fs, 1);
            auto y = input;
            for (size_t pos = 0; pos < n; pos += tick)
            {
                auto& band = bands[(size_t) dynBand];
                band.setGainModulationDb (section.tick (band.getCurrentFrequencyHz(), band.getCurrentQ()));
                const auto len = (int) std::min<size_t> (tick, n - pos);
                const float* in[] = { y.data() + pos };
                section.processDetector (in, 1, 0, len);
                float* ch[] = { y.data() + pos };
                for (auto& b : bands)
                    b.process (ch, 1, len);
            }
            LQ_CHECK (x[0] == y, str ("dynamic band ", dynBand + 1, " fs=", fs, ": core differs from the reference"));
        }

    // Dynamics switched on after 1003 samples of the static path: the core
    // must still tick on the bands' absolute grid (multiples of 16).
    constexpr double fs = 48000.0;
    constexpr size_t switchOn = 1003;
    const auto n = samples (0.3, fs);
    const double levels[numBands] = { -12.0, -20.0, -15.0, -10.0, -18.0, -14.0, -16.0, -22.0 };
    const auto input = tones (fs, n, levels);
    const auto dynSettings = dynOn (-25.0, 10.0, 2.0, 40.0);

    EqCore core;
    for (int b = 0; b < numBands; ++b)
        core.setBandTargets (b, bell (b, 1.0, 2.0));
    core.prepare (fs, 1);
    auto x = input;
    {
        float* ch[] = { x.data() };
        core.process (ch, 1, (int) switchOn);
        core.setBandDynamics (3, dynSettings);
        float* rest[] = { x.data() + switchOn };
        core.process (rest, 1, (int) (n - switchOn));
    }

    std::array<EqBand, numBands> bands;
    DynamicSection section;
    for (int b = 0; b < numBands; ++b)
    {
        bands[(size_t) b].setTargets (bell (b, 1.0, 2.0));
        bands[(size_t) b].prepare (fs, 1);
    }
    section.prepare (fs, 1);
    auto y = input;
    for (size_t pos = 0; pos < n;)
    {
        if (pos == switchOn)
            section.setTargets (dynSettings);
        if (pos % tick == 0)
            bands[3].setGainModulationDb (section.tick (bands[3].getCurrentFrequencyHz(), bands[3].getCurrentQ()));
        auto end = std::min (n, pos - pos % tick + tick);
        if (pos < switchOn && switchOn < end)
            end = switchOn;
        const auto len = (int) (end - pos);
        const float* in[] = { y.data() + pos };
        section.processDetector (in, 1, 0, len);
        float* ch[] = { y.data() + pos };
        for (auto& b : bands)
            b.process (ch, 1, len);
        pos = end;
    }
    LQ_CHECK (x == y, "dynamics switched on mid-stream must stay on the absolute tick grid");
}

LQ_TEST ("dyn8: automating all eight sections and bands is block-size independent, finite and allocation-free (5, 12, 14)")
{
    for (const auto fs : { 44100.0, 96000.0 })
        for (const int channels : { 1, 2 })
        {
            const auto n = samples (1.0, fs);
            const double levels[numBands] = { -10.0, -14.0, -18.0, -12.0, -16.0, -20.0, -15.0, -25.0 };
            std::vector<Signal> input { tones (fs, n, levels), makeNoise (n, 301, 0.3f) };
            for (size_t i = 0; i < n; ++i)
                input[1][i] += 0.5f * input[0][i];
            input.resize ((size_t) channels);
            const auto events = randomCoreEvents (fs, n / 2, 302, false);

            const auto reference = renderCore (fs, input, events, [] { return 512; });
            bool finite = true;
            for (const auto& s : reference)
                finite = finite && allFinite (s);
            LQ_CHECK (finite, "finite output");

            std::mt19937 rng (303);
            std::uniform_int_distribution<int> random (1, 700);
            for (const int block : { 1, 7, 16, 64, 480, 4096, -1 })
            {
                const auto out = renderCore (fs, input, events, [&] { return block > 0 ? block : random (rng); });
                LQ_CHECK (out == reference, str ("block ", block, " differs, fs=", fs, " channels=", channels));
            }
        }

    // No allocation while processing with eight active sections and automation.
    constexpr double fs = 48000.0;
    EqCore core;
    for (int b = 0; b < numBands; ++b)
    {
        core.setBandTargets (b, bell (b, 2.0));
        core.setBandDynamics (b, dynOn (-30.0, 12.0));
    }
    core.prepare (fs, 2);
    auto l = makeNoise (samples (1.0, fs), 304), r = makeNoise (samples (1.0, fs), 305);
    const auto events = randomCoreEvents (fs, l.size(), 306, true);
    long allocations = 0;
    {
        ScopedAllocationCounter counter;
        size_t next = 0;
        for (size_t pos = 0; pos < l.size(); pos += 64)
        {
            while (next < events.size() && events[next].position <= pos)
            {
                const auto& e = events[next++];
                if (e.isDynamics)
                    core.setBandDynamics (e.band, e.dynamics);
                else
                    core.setBandTargets (e.band, e.settings);
            }
            float* ch[] = { l.data() + pos, r.data() + pos };
            core.process (ch, 2, (int) std::min<size_t> (64, l.size() - pos));
        }
        allocations = counter.count();
    }
    LQ_CHECK (allocations == 0, str (allocations, " allocations with eight dynamic sections"));
}

LQ_TEST ("dyn8: attack and release are independent per band (7)")
{
    constexpr double fs = 48000.0;
    const double attack[numBands]  = { 20.0, 2.0, 10.0, 5.0, 40.0, 1.0, 15.0, 8.0 };
    const double release[numBands] = { 300.0, 30.0, 100.0, 60.0, 500.0, 20.0, 200.0, 80.0 };
    EqCore core;
    for (int b = 0; b < numBands; ++b)
    {
        core.setBandTargets (b, bell (b));
        core.setBandDynamics (b, dynOn (-40.0, 24.0, attack[b], release[b]));
    }
    core.prepare (fs, 1);

    // Every tone steps from -70 to -20 dBFS (target 20 dB) at the same time
    // and back at the same time.
    const auto up = samples (0.1, fs), down = samples (0.7, fs), n = samples (6.0, fs);
    Signal s (n, 0.0f);
    for (int b = 0; b < numBands; ++b)
        for (size_t i = 0; i < n; ++i)
            s[i] += (float) ((i >= up && i < down ? dbToAmp (-20.0) : dbToAmp (-70.0))
                             * std::sin (2.0 * std::numbers::pi * bandFreqs[b] * (double) i / fs));
    std::vector<Signal> x { s };
    const auto gr = runTraces (core, x);

    for (int b = 0; b < numBands; ++b)
    {
        const auto& g = gr[(size_t) b];
        const auto peak = g[down / tick - 1];
        double tA = -1, tR = -1;
        for (auto t = up / tick; t < g.size() && tA < 0; ++t)
            if (g[t] >= 0.632 * peak)
                tA = (double) ((t + 1) * tick - up) / fs;
        for (auto t = down / tick; t < g.size() && tR < 0; ++t)
            if (g[t] <= 0.368 * peak)
                tR = (double) ((t + 1) * tick - down) / fs;
        // Detector delay: the two band-pass sections settle with time constant
        // Q / (pi f0) each and the level window adds ~1 / f0, so the bound is
        // 3 Q / (pi f0) + 2 / f0 + 2 ms (60 Hz, Q 4: about 100 ms; 1 kHz: 6 ms).
        const auto delay = 3.0 * 4.0 / (std::numbers::pi * bandFreqs[b]) + 2.0 / bandFreqs[b] + 0.002;
        LQ_CHECK (tA >= attack[b] * 1e-3 - tick / fs && tA <= attack[b] * 1e-3 + delay,
                  str ("band ", b + 1, ": attack ", attack[b], " ms measured ", tA * 1e3, " ms"));
        LQ_CHECK (tR >= release[b] * 1e-3 - tick / fs && tR <= release[b] * 1e-3 + delay + 0.002,
                  str ("band ", b + 1, ": release ", release[b], " ms measured ", tR * 1e3, " ms"));
    }
}

LQ_TEST ("dyn8: max-linked stereo, panning and L/R differences (8, 9)")
{
    constexpr double fs = 48000.0, f0 = 1000.0;
    const auto n = samples (0.6, fs);
    const auto a = 0.25;

    auto steadyReduction = [&] (const Signal& l, const Signal& r, DetectorLink link, Signal* outL = nullptr, Signal* outR = nullptr)
    {
        EqCore core;
        core.setBandTargets (3, { f0, 0.0, 2.0, false, FilterType::bell });
        core.setBandDynamics (3, dynOn (-24.0, 24.0, 2.0, 50.0));
        core.setDetectorLink (link);
        core.prepare (fs, 2);
        std::vector<Signal> x { l, r };
        const auto gr = runTraces (core, x)[3];
        if (outL != nullptr) *outL = x[0];
        if (outR != nullptr) *outR = x[1];
        double sum = 0.0;
        for (auto t = gr.size() / 2; t < gr.size(); ++t)
            sum += gr[t];
        return sum / (double) (gr.size() - gr.size() / 2);
    };

    // (8) Same amplitude in one channel or in both: the same reduction.
    const auto tone = makeSine (f0, fs, n, a);
    const Signal silence (n, 0.0f);
    const auto both = steadyReduction (tone, tone, DetectorLink::maxLinked);
    const auto leftOnly = steadyReduction (tone, silence, DetectorLink::maxLinked);
    const auto rightOnly = steadyReduction (silence, tone, DetectorLink::maxLinked);
    LQ_CHECK (std::abs (both - leftOnly) < 0.02 && std::abs (both - rightOnly) < 0.02,
              str ("max link: centred ", both, ", left ", leftOnly, ", right ", rightOnly, " dB"));

    // (9) Pan laws: reading = louder channel. Constant-power pan moves the
    // onset by up to 3.01 dB (centre); the energy link is pan-invariant for it.
    double worstMax = 0.0, worstEnergy = 0.0;
    const auto hard = steadyReduction (tone, silence, DetectorLink::maxLinked);
    const auto hardEnergy = steadyReduction (tone, silence, DetectorLink::energyLinked);
    for (int k = 0; k <= 8; ++k)
    {
        const auto theta = 0.5 * std::numbers::pi * k / 8.0;
        Signal l (n), r (n);
        for (size_t i = 0; i < n; ++i)
        {
            l[i] = (float) (std::cos (theta) * tone[i]);
            r[i] = (float) (std::sin (theta) * tone[i]);
        }
        worstMax = std::max (worstMax, std::abs (steadyReduction (l, r, DetectorLink::maxLinked) - hard));
        worstEnergy = std::max (worstEnergy, std::abs (steadyReduction (l, r, DetectorLink::energyLinked) - hardEnergy));
    }
    LQ_CHECK (worstMax < 3.01 + 0.05 && worstMax > 2.9, str ("max link, constant-power pan: onset moves by ", worstMax, " dB (theory 3.01)"));
    LQ_CHECK (worstEnergy < 0.05, str ("energy link, constant-power pan: ", worstEnergy, " dB"));
    ctx.note (str ("constant-power pan sweep: max link moves the reduction by up to ", worstMax, " dB (centre), energy link by ", worstEnergy, " dB"));

    // Image: identical gain on L and R. Same content at different levels keeps
    // its L/R ratio exactly; different content in R is reduced by L's reduction.
    Signal outL, outR;
    const auto quiet = makeSine (f0, fs, n, a * 0.3);
    steadyReduction (tone, quiet, DetectorLink::maxLinked, &outL, &outR);
    const auto ratioIn = toDb (a / (a * 0.3));
    const auto ratioOut = toDb (fitAmplitude (outL, n / 2, n, f0, fs) / fitAmplitude (outR, n / 2, n, f0, fs));
    LQ_CHECK (std::abs (ratioOut - ratioIn) < 0.01, str ("L/R ratio ", ratioIn, " dB in, ", ratioOut, " dB out"));

    const auto rNoise = makeNoise (n, 401, 0.02f);
    Signal outR2;
    const auto reduction = steadyReduction (tone, rNoise, DetectorLink::maxLinked, nullptr, &outR2);
    Signal rStatic = rNoise;
    {
        EqBand e;
        e.setTargets ({ f0, 0.0, 2.0, false, FilterType::bell });
        e.prepare (fs, 1);
        float* ch[] = { rStatic.data() };
        e.process (ch, 1, (int) n);
    }
    // R's 1 kHz region is reduced by L's reduction (cross-channel action).
    const auto rRegion = [&] (const Signal& s)
    {
        DynamicDetector d;
        d.prepare (fs, 1);
        d.setRegion (f0, 2.0);
        const float* ch[] = { s.data() };
        double sum = 0.0;
        int count = 0;
        for (size_t pos = 0; pos + 16 <= n; pos += 16)
        {
            d.process (ch, 1, (int) pos, 16);
            if (pos > n / 2)
            {
                sum += d.getMeanSquare();
                ++count;
            }
        }
        return 10.0 * std::log10 (sum / count);
    };
    const auto rChange = rRegion (outR2) - rRegion (rStatic);
    ctx.note (str ("tone only in L reduced by ", reduction, " dB: the 1 kHz region of unrelated content in R drops by ", -rChange, " dB (linked)"));
    LQ_CHECK (rChange < -0.5 * reduction, "linked: R receives L's reduction");
}

LQ_TEST ("dyn8: type changes away from a dynamic Bell leave no modulation and no discontinuity (10)")
{
    constexpr double fs = 48000.0, f0 = 1000.0;
    const auto n = samples (0.8, fs), change = samples (0.3, fs);
    const auto input = makeSine (f0, fs, n, 0.5);

    for (const auto type : { FilterType::lowShelf, FilterType::highShelf, FilterType::lowCut, FilterType::highCut, FilterType::notch })
    {
        // Dynamic: Bell with 12 dB of reduction, then the type change.
        EqCore dynamic, reference;
        dynamic.setBandTargets (3, { f0, 3.0, 2.0, false, FilterType::bell });
        dynamic.setBandDynamics (3, dynOn (-24.0, 12.0, 2.0, 50.0));
        reference.setBandTargets (3, { f0, 3.0, 2.0, false, FilterType::bell });
        dynamic.prepare (fs, 1);
        reference.prepare (fs, 1);
        std::vector<Signal> x { input }, y { input };
        std::vector<double> modulation;
        for (size_t pos = 0; pos < n; pos += tick)
        {
            if (pos == change - change % tick)
            {
                dynamic.setBandTargets (3, { f0, 3.0, 2.0, false, type });
                reference.setBandTargets (3, { f0, 3.0, 2.0, false, type });
            }
            float* c1[] = { x[0].data() + pos };
            float* c2[] = { y[0].data() + pos };
            dynamic.process (c1, 1, tick);
            reference.process (c2, 1, tick);
            modulation.push_back (dynamic.getBand (3).getGainModulationDb());
        }
        const auto fadeEnd = (change - change % tick) / tick + (size_t) std::lround (0.02 * fs / tick) + 1;
        LQ_CHECK (modulation[(change - change % tick) / tick - 1] < -11.0, "reduction active before the change");
        bool zeroAfter = true;
        for (auto t = fadeEnd; t < modulation.size(); ++t)
            zeroAfter = zeroAfter && modulation[t] == 0.0;
        LQ_CHECK (zeroAfter && ! dynamic.getDynamics (3).isActive(), str (int (type), ": modulation must be exactly 0 after the 20 ms fade"));

        // After the type crossfade only the new path sounds; it never received
        // any modulation, so it equals the static band doing the same change.
        const auto xfadeEnd = change - change % tick + samples (0.02, fs) + tick;
        bool same = true;
        for (auto i = xfadeEnd; i < n; ++i)
            same = same && x[0][i] == y[0][i];
        LQ_CHECK (same, str (int (type), ": after the crossfade the new topology must be the static one exactly"));

        // No discontinuity during the change: excess 4th difference over the
        // steady state below the E1b crossfade bound (3e-3, a -60 dBFS step).
        const auto steady = maxFourthDifference (x[0], samples (0.2, fs), samples (0.29, fs));
        const auto during = maxFourthDifference (x[0], change - 64, xfadeEnd + 64);
        LQ_CHECK (during <= std::max (steady, maxFourthDifference (x[0], xfadeEnd + 64, n)) + 3e-3,
                  str (int (type), ": 4th difference ", during, " vs steady ", steady));
    }

    // Fast changes between all types with dynamics on: finite, and the
    // modulation is 0 whenever the band has not been a Bell for 20 ms + 1 tick.
    EqCore core;
    core.setBandTargets (3, { f0, 0.0, 2.0, false, FilterType::bell });
    core.setBandDynamics (3, dynOn (-30.0, 18.0, 1.0, 30.0));
    core.prepare (fs, 1);
    auto x = makeSine (f0, fs, samples (2.0, fs), 0.5);
    std::mt19937 rng (501);
    std::uniform_int_distribution<int> type (0, 5), gap (1, 60);
    size_t nextChange = 0, lastBell = 0;
    FilterType current = FilterType::bell;
    bool ok = true;
    for (size_t t = 0, pos = 0; pos < x.size(); pos += tick, ++t)
    {
        if (t == nextChange)
        {
            current = filterTypeFromIndex (type (rng));
            core.setBandTargets (3, { f0, 0.0, 2.0, false, current });
            nextChange = t + (size_t) gap (rng);
        }
        if (current == FilterType::bell)
            lastBell = t;
        float* ch[] = { x.data() + pos };
        core.process (ch, 1, tick);
        if (t > lastBell + (size_t) std::lround (0.02 * fs / tick) + 1)
            ok = ok && core.getBand (3).getGainModulationDb() == 0.0;
    }
    LQ_CHECK (ok && allFinite (x), "fast type changes: no modulation left on non-Bell types, finite output");
}

LQ_TEST ("dyn8: enabling, disabling and bypassing dynamics during playback is smooth and exact (11)")
{
    constexpr double fs = 48000.0, f0 = 1000.0;
    const auto n = samples (1.5, fs);
    auto input = makeSine (f0, fs, n, 0.5);
    EqCore core;
    core.setBandTargets (3, { f0, 0.0, 2.0, false, FilterType::bell });
    core.setBandDynamics (3, dynOn (-24.0, 12.0, 2.0, 40.0));
    core.prepare (fs, 1);
    auto x = input;
    std::vector<double> modulation;
    const auto period = samples (0.15, fs);
    for (size_t pos = 0; pos < n; pos += tick)
    {
        if (pos % period < tick)
            core.setBandDynamics (3, { (pos / period) % 2 == 0, -24.0, 12.0, 2.0, 40.0 });
        float* ch[] = { x.data() + pos };
        core.process (ch, 1, tick);
        modulation.push_back (core.getBand (3).getGainModulationDb());
    }
    // Every "off" period ends with exactly 0 modulation (fade 20 ms).
    bool zero = true;
    for (size_t start = period; start < n; start += 2 * period)
        zero = zero && modulation[(start + period) / tick - 2] == 0.0;
    LQ_CHECK (zero, "dynamics off: modulation back to exactly 0");
    // Smooth: per tick the gain moves no more than the attack (12 dB range,
    // 2 ms: 12 (1 - e^(-tick / 2 ms)) = 1.83 dB) plus the 20 ms fade
    // (0.4 dB), and the output has no discontinuity beyond the -60 dBFS-step bound.
    double maxStep = 0.0;
    for (size_t t = 1; t < modulation.size(); ++t)
        maxStep = std::max (maxStep, std::abs (modulation[t] - modulation[t - 1]));
    const auto bound = 12.0 * (1.0 - std::exp (-tick / (0.002 * fs))) + 12.0 * tick / (0.02 * fs);
    LQ_CHECK (maxStep < bound, str ("largest modulation step per tick ", maxStep, " dB (bound ", bound, ")"));
    const auto steady = maxFourthDifference (input, 0, n);
    LQ_CHECK (maxFourthDifference (x, 64, n) <= steady + 3e-3, "no discontinuity when toggling");

    // Bypass a dynamic Bell at 0 dB static gain: after the fades the band is
    // idle and the output is the input, bit for bit.
    EqCore bypassed;
    bypassed.setBandTargets (3, { f0, 0.0, 2.0, false, FilterType::bell });
    bypassed.setBandDynamics (3, dynOn (-24.0, 12.0, 2.0, 40.0));
    bypassed.prepare (fs, 1);
    std::vector<Signal> y { makeSine (f0, fs, samples (0.3, fs), 0.5) };
    runBlocks (bypassed, y, 64);
    LQ_CHECK (bypassed.getDynamics (3).getGainReductionDb() > 11.0, "reducing before the bypass");
    bypassed.setBandTargets (3, { f0, 0.0, 2.0, true, FilterType::bell });
    std::vector<Signal> settle { makeSine (f0, fs, samples (0.5, fs), 0.5) };
    runBlocks (bypassed, settle, 64);
    std::vector<Signal> after { makeSine (f0, fs, samples (0.2, fs), 0.5) };
    const auto expected = after[0];
    runBlocks (bypassed, after, 64);
    LQ_CHECK (after[0] == expected && bypassed.isIdle(), "bypassed dynamic band: bit-exact pass-through once faded");
}

LQ_TEST ("dyn8: extreme frequency and Q, above 0 dBFS, NaN and Inf: bounded and finite (13, 14, 15)")
{
    for (const auto fs : { 44100.0, 192000.0 })
    {
        EqCore core;
        const double freqs[numBands] = { 20.0, 20.0, 20000.0, 20000.0, 1000.0, 1000.0, 0.49 * fs, 50.0 };
        const double qs[numBands] = { 0.1, 18.0, 0.1, 18.0, 0.1, 18.0, 18.0, 18.0 };
        for (int b = 0; b < numBands; ++b)
        {
            core.setBandTargets (b, { freqs[b], b % 2 ? 12.0 : -6.0, qs[b], false, FilterType::bell });
            core.setBandDynamics (b, dynOn (-20.0, 24.0, 0.5, 5.0));
        }
        core.prepare (fs, 2);
        const auto n = samples (2.0, fs);
        auto l = makeNoise (n, 601, 4.0f), r = makeNoise (n, 602, 4.0f); // about +6 dBFS peaks
        for (size_t i = 0; i < n; ++i)
            l[i] += (float) (4.0 * std::sin (2.0 * std::numbers::pi * 20.0 * (double) i / fs));
        const auto nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        for (size_t i = 0; i < 64; ++i)
        {
            l[n / 3 + i] = nan;
            r[n / 2 + i] = inf;
            l[2 * n / 3 + i] = -1.0e30f;
        }
        l.resize (n + samples (3.0, fs), 0.0f);
        r.resize (l.size(), 0.0f);
        bool bounded = true;
        for (size_t pos = 0; pos < l.size(); pos += 128)
        {
            float* ch[] = { l.data() + pos, r.data() + pos };
            core.process (ch, 2, (int) std::min<size_t> (128, l.size() - pos));
            for (int b = 0; b < numBands; ++b)
            {
                const auto g = core.getDynamics (b).getGainReductionDb();
                bounded = bounded && std::isfinite (g) && g >= 0.0 && g <= 24.0;
            }
        }
        LQ_CHECK (allFinite (l) && allFinite (r) && bounded, str ("finite and bounded at fs=", fs));
        // The slowest pole (20 Hz, Q 18, +12 dB: pole Q 36, tau 0.57 s) needs
        // seconds: check that the tail decays, not that it is silent.
        const auto end = l.size();
        const auto early = peak (l, end - samples (1.5, fs), end - samples (1.45, fs));
        const auto late = peak (l, end - samples (0.05, fs));
        LQ_CHECK (late < 0.2f * early || late < 1.0e-6f, str ("decays after the input stops, fs=", fs, ": ", early, " -> ", late));
    }

    // Above 0 dBFS: no clipping and the law holds at +12 dBFS.
    constexpr double fs = 48000.0;
    EqCore core;
    core.setBandTargets (3, { 1000.0, 0.0, 2.0, false, FilterType::bell });
    core.setBandDynamics (3, dynOn (6.0, 12.0, 2.0, 50.0));
    core.prepare (fs, 1);
    std::vector<Signal> x { makeSine (1000.0, fs, samples (0.5, fs), dbToAmp (12.0)) };
    const auto gr = runTraces (core, x)[3];
    const auto out = toDb (fitAmplitude (x[0], x[0].size() / 2, x[0].size(), 1000.0, fs));
    LQ_CHECK (std::abs (gr.back() - 6.0) < 0.12 && std::abs (out - 6.0) < 0.12 && peak (x[0], x[0].size() / 2) > 1.9f,
              str ("+12 dBFS tone, threshold +6: reduction ", gr.back(), " dB, output ", out, " dBFS (unclipped)"));
}

LQ_TEST ("dyn8: several bands modulating at once match a per-sample reference; no extra artefacts (16)")
{
    // Eight tones, each amplitude-modulated at its own rate (0.7..9 Hz) across
    // its band's threshold, so all eight gains move at once. Reference: the
    // recorded per-tick reductions applied per sample (gain interpolated over
    // each tick, every Bell redesigned every sample), in cascade, with float
    // rounding between bands like the core.
    constexpr double fs = 48000.0;
    const auto n = samples (1.0, fs);
    Signal s (n, 0.0f);
    for (int b = 0; b < numBands; ++b)
        for (size_t i = 0; i < n; ++i)
        {
            const auto env = dbToAmp (-36.0 + 30.0 * (0.5 + 0.5 * std::sin (2.0 * std::numbers::pi * (0.7 + b) * (double) i / fs)));
            s[i] += (float) (env * std::sin (2.0 * std::numbers::pi * bandFreqs[b] * (double) i / fs));
        }
    const double attack[numBands] = { 1.0, 2.0, 5.0, 0.5, 3.0, 10.0, 1.0, 2.0 };
    EqCore core;
    for (int b = 0; b < numBands; ++b)
    {
        core.setBandTargets (b, bell (b, 2.0, 3.0));
        core.setBandDynamics (b, dynOn (-24.0, 15.0, attack[b], 30.0));
    }
    core.prepare (fs, 1);
    std::vector<Signal> x { s };
    const auto gr = runTraces (core, x);

    Signal ideal = s;
    for (int b = 0; b < numBands; ++b)
    {
        double s1 = 0, s2 = 0;
        const auto& g = gr[(size_t) b];
        for (size_t i = 0; i < n; ++i)
        {
            const auto t = i / tick;
            const auto w = (double) (i % tick + 1) / tick;
            const auto prev = t == 0 ? 0.0 : g[t - 1];
            const auto c = designBell (fs, bandFreqs[b], 2.0 - (prev + w * (g[t] - prev)), 3.0);
            const double in = ideal[i];
            const auto o = c.b0 * in + s1;
            s1 = c.b1 * in - c.a1 * o + s2;
            s2 = c.b2 * in - c.a2 * o;
            ideal[i] = (float) o;
        }
    }
    double diff = 0.0, sig = 0.0;
    for (size_t i = n / 10; i < n; ++i)
    {
        diff += ((double) x[0][i] - ideal[i]) * ((double) x[0][i] - ideal[i]);
        sig += (double) ideal[i] * ideal[i];
    }
    const auto residual = 10.0 * std::log10 (diff / sig);
    int moving = 0;
    for (int b = 0; b < numBands; ++b)
    {
        const auto& g = gr[(size_t) b];
        moving += *std::max_element (g.begin() + (long) (g.size() / 10), g.end()) - *std::min_element (g.begin() + (long) (g.size() / 10), g.end()) > 6.0 ? 1 : 0;
    }
    LQ_CHECK (moving == numBands, str (moving, " of 8 bands swing by more than 6 dB"));
    // Same criterion as the single band (E2a): below -55 dB of the signal.
    LQ_CHECK (residual < -55.0 && allFinite (x[0]), str ("eight modulated bands vs per-sample reference: ", residual, " dB"));
    ctx.note (str ("eight bands modulating at once: residual vs per-sample reference ", residual, " dB"));
}

LQ_TEST ("dyn8 perf: eight dynamic bands, stereo, Release")
{
#ifndef NDEBUG
    ctx.skip ("Debug build: timings are not meaningful; run the Release test binary");
    return;
#else
    // Budget: the E1a/E1b 5 % of one core for the eight-band core, now with
    // all eight dynamic sections active and their gains moving.
    constexpr double fs = 96000.0, seconds = 5.0;
    const auto n = samples (seconds, fs);
    Signal left (n, 0.0f);
    for (int b = 0; b < numBands; ++b)
        for (size_t i = 0; i < n; ++i)
        {
            const auto env = dbToAmp (-36.0 + 30.0 * (0.5 + 0.5 * std::sin (2.0 * std::numbers::pi * (0.7 + b) * (double) i / fs)));
            left[i] += (float) (env * std::sin (2.0 * std::numbers::pi * bandFreqs[b] * (double) i / fs));
        }
    auto right = left;
    std::vector<double> loads;
    for (int r = 0; r < 6; ++r)
    {
        EqCore core;
        for (int b = 0; b < numBands; ++b)
        {
            core.setBandTargets (b, bell (b, 2.0, 3.0));
            core.setBandDynamics (b, dynOn (-24.0, 15.0, 2.0, 30.0));
        }
        core.prepare (fs, 2);
        auto l = left, rr = right;
        const auto start = std::chrono::steady_clock::now();
        for (size_t pos = 0; pos < n; pos += 64)
        {
            float* ch[] = { l.data() + pos, rr.data() + pos };
            core.process (ch, 2, (int) std::min<size_t> (64, n - pos));
        }
        const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        if (r > 0)
            loads.push_back (elapsed / seconds);
    }
    std::sort (loads.begin(), loads.end());
    const auto load = loads[loads.size() / 2];
    ctx.note (str ("eight active dynamic bands, 96 kHz stereo, block 64: ", load * 100.0, " % of one core"));
    LQ_CHECK (load < 0.05, str (load * 100.0, " % of one core"));
#endif
}
