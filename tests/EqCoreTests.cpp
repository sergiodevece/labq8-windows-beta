// E1a: eight Bell bands in cascade (dsp::EqCore). Tolerances as in
// DspTests.cpp: bit-exact for structural properties, 1e-4 dB for designed
// responses, 0.01 dB for sines measured through float32 I/O.

// Exact float comparison is intentional: bit-exactness is a tested property.
#pragma clang diagnostic ignored "-Wfloat-equal"

#include "TestHarness.h"
#include "TestSignals.h"

#include "dsp/BandLimits.h"
#include "dsp/EqCore.h"

#include <array>
#include <limits>
#include <utility>

using namespace labq8::dsp;
using namespace labq8::test::signals;
using labq8::test::str;

namespace
{
constexpr int kBands = EqCore::numBands;

// Eight distinct, all-active settings (valid at 44.1 kHz and above).
const std::array<BellSettings, kBands> kBandSet { {
    { 40.0, 6.0, 0.7, false },    { 100.0, -4.0, 1.5, false }, { 250.0, 3.0, 2.0, false },
    { 1000.0, -8.0, 4.0, false }, { 2500.0, 5.0, 1.0, false }, { 5000.0, -3.0, 0.5, false },
    { 10000.0, 4.0, 2.5, false }, { 16000.0, -6.0, 1.2, false },
} };

EqCore makeCore (double fs, const std::array<BellSettings, kBands>& settings, int channels = 1)
{
    EqCore core;
    for (int b = 0; b < kBands; ++b)
        core.setBandTargets (b, settings[(size_t) b]);
    core.prepare (fs, channels);
    return core;
}

std::array<BellSettings, kBands> neutralSet()
{
    std::array<BellSettings, kBands> s {};
    return s; // BellSettings defaults: 1 kHz, 0 dB, Q 1
}

void processMono (EqCore& core, Signal& x, size_t begin, size_t end, int blockSize)
{
    for (auto pos = begin; pos < end;)
    {
        const auto n = std::min (static_cast<size_t> (blockSize), end - pos);
        float* channels[] = { x.data() + pos };
        core.process (channels, 1, static_cast<int> (n));
        pos += n;
    }
}

void processMono (EqCore& core, Signal& x, int blockSize = 512)
{
    processMono (core, x, 0, x.size(), blockSize);
}

void processStereo (EqCore& core, Signal& l, Signal& r, int blockSize)
{
    for (size_t pos = 0; pos < l.size(); pos += (size_t) blockSize)
    {
        const auto n = static_cast<int> (std::min (static_cast<size_t> (blockSize), l.size() - pos));
        float* channels[] = { l.data() + pos, r.data() + pos };
        core.process (channels, 2, n);
    }
}

double compositeAnalogDb (const std::array<BellSettings, kBands>& set, double f, double fs)
{
    double db = 0.0;
    for (const auto& s : set)
        if (! s.bypassed && s.gainDb != 0.0)
            db += analogBellDb (f, s.frequencyHz, s.gainDb, s.q, fs);
    return db;
}

double compositeSettle (const std::array<BellSettings, kBands>& set)
{
    double t = 0.25;
    for (const auto& s : set)
        if (! s.bypassed && s.gainDb != 0.0)
            t = std::max (t, settleSeconds (s.frequencyHz, s.gainDb, s.q));
    return t;
}

double measureGainDb (EqCore& core, double fs, double f, double settle)
{
    const auto measure = std::max (0.2, 20.0 / f);
    auto x = makeSine (f, fs, samples (settle + measure, fs));
    auto y = x;
    processMono (core, y);
    const auto begin = samples (settle, fs);
    return toDb (fitAmplitude (y, begin, y.size(), f, fs) / fitAmplitude (x, begin, x.size(), f, fs));
}

struct CoreEvent
{
    size_t position;
    int band;
    BellSettings settings;
};

template <typename NextBlock>
Signal processScheduled (double fs, const std::array<BellSettings, kBands>& initial, const Signal& input,
                         const std::vector<CoreEvent>& events, NextBlock nextBlock)
{
    auto core = makeCore (fs, initial);
    auto y = input;
    size_t pos = 0, next = 0;

    while (pos < y.size())
    {
        while (next < events.size() && events[next].position <= pos)
        {
            core.setBandTargets (events[next].band, events[next].settings);
            ++next;
        }

        auto n = std::min (static_cast<size_t> (nextBlock()), y.size() - pos);
        if (next < events.size())
            n = std::min (n, events[next].position - pos);

        processMono (core, y, pos, pos + n, static_cast<int> (n));
        pos += n;
    }

    return y;
}
} // namespace

//==============================================================================
LQ_TEST ("eq: each of the 8 bands shapes the response on its own")
{
    for (const auto fs : sampleRates)
        for (int k = 0; k < kBands; ++k)
        {
            auto set = neutralSet();
            auto active = kBandSet[(size_t) k];
            active.gainDb = (k % 2 == 0) ? 12.0 : -12.0;
            set[(size_t) k] = active;

            auto core = makeCore (fs, set);

            for (int j = 0; j < kBands; ++j)
                LQ_CHECK (core.getBand (j).isIdle() == (j != k), str ("band ", j + 1, " idle state wrong with only band ", k + 1, " active"));

            bool shapeOk = true;
            for (int i = 0; i < 40; ++i)
            {
                const auto f = 10.0 * std::pow (0.49 * fs / 10.0, i / 39.0);
                const auto expected = analogBellDb (f, active.frequencyHz, active.gainDb, active.q, fs);
                shapeOk = shapeOk && std::abs (toDb (core.magnitudeAt (f)) - expected) < designTolDb;
            }
            LQ_CHECK (shapeOk, str ("composite curve != band ", k + 1, " curve, fs=", fs));

            const auto g = measureGainDb (core, fs, active.frequencyHz, compositeSettle (set));
            LQ_CHECK (std::abs (g - active.gainDb) < measureTolDb,
                      str ("band ", k + 1, " measured ", g, " dB at ", active.frequencyHz, " Hz, fs=", fs));
        }
}

LQ_TEST ("eq: cascade response equals the sum of the 8 band responses (dB)")
{
    for (const auto fs : sampleRates)
    {
        auto core = makeCore (fs, kBandSet);
        LQ_CHECK (! core.isIdle(), "all bands should be active");

        for (int i = 0; i < 60; ++i)
        {
            const auto f = 10.0 * std::pow (0.49 * fs / 10.0, i / 59.0);
            const auto got = toDb (core.magnitudeAt (f));
            const auto expected = compositeAnalogDb (kBandSet, f, fs);
            LQ_CHECK (std::abs (got - expected) < designTolDb, str ("composite |H(", f, ")| ", got, " expected ", expected, " fs=", fs));
        }

        for (const auto f : { 30.0, 40.0, 100.0, 250.0, 700.0, 1000.0, 2500.0, 5000.0, 10000.0, 16000.0 })
        {
            auto measured = makeCore (fs, kBandSet);
            const auto got = measureGainDb (measured, fs, f, compositeSettle (kBandSet));
            const auto expected = compositeAnalogDb (kBandSet, f, fs);
            LQ_CHECK (std::abs (got - expected) < measureTolDb, str ("measured ", got, " dB expected ", expected, " at ", f, " Hz, fs=", fs));
        }
    }
}

LQ_TEST ("eq: neutral settings are bit-exact transparent (mono and stereo)")
{
    for (const auto fs : sampleRates)
    {
        // All bands at 0 dB with assorted frequencies and Q.
        auto zero = kBandSet;
        for (auto& s : zero)
            s.gainDb = 0.0;

        // All bands boosted/cut but bypassed.
        auto bypassed = kBandSet;
        for (auto& s : bypassed)
            s.bypassed = true;

        for (const auto* set : { &zero, &bypassed })
        {
            auto mono = makeCore (fs, *set, 1);
            const auto x = makeNoise (samples (0.25, fs), 50);
            auto y = x;
            processMono (mono, y, 333);
            LQ_CHECK (y == x && mono.isIdle(), str ("mono not transparent fs=", fs));

            auto stereo = makeCore (fs, *set, 2);
            const auto r = makeNoise (x.size(), 51);
            auto l2 = x, r2 = r;
            processStereo (stereo, l2, r2, 333);
            LQ_CHECK (l2 == x && r2 == r && stereo.isIdle(), str ("stereo not transparent fs=", fs));
        }

        // Back to neutral at runtime: idle and bit-exact once settled.
        auto core = makeCore (fs, kBandSet);
        auto x = makeNoise (samples (3.0, fs), 52);
        auto y = x;
        processMono (core, y, 0, samples (0.25, fs), 512);
        for (int b = 0; b < kBands; ++b)
            core.setBandTargets (b, zero[(size_t) b]);
        processMono (core, y, samples (0.25, fs), y.size(), 512);

        const auto tail = y.size() - samples (0.5, fs);
        LQ_CHECK (core.isIdle(), str ("not idle after returning to neutral fs=", fs));
        LQ_CHECK (std::equal (y.begin() + (long) tail, y.end(), x.begin() + (long) tail), "neutral tail not bit-exact");
    }
}

LQ_TEST ("eq: individual band bypass removes exactly that band")
{
    for (const auto fs : sampleRates)
    {
        // Bypassed from the start == that band at 0 dB, bit for bit.
        for (int k = 0; k < kBands; ++k)
        {
            auto withBypass = kBandSet;
            withBypass[(size_t) k].bypassed = true;
            auto withZero = kBandSet;
            withZero[(size_t) k].gainDb = 0.0;

            const auto x = makeNoise (samples (0.25, fs), 60 + (std::uint32_t) k);
            auto a = x, b = x;
            auto coreA = makeCore (fs, withBypass);
            auto coreB = makeCore (fs, withZero);
            processMono (coreA, a);
            processMono (coreB, b);

            LQ_CHECK (a == b, str ("bypass of band ", k + 1, " differs from 0 dB, fs=", fs));
            LQ_CHECK (coreA.getBand (k).isIdle(), str ("bypassed band ", k + 1, " not idle"));
        }

        // Toggled at runtime: only that band leaves, and comes back.
        constexpr int k = 3; // 1 kHz, -8 dB, Q 4
        auto core = makeCore (fs, kBandSet);
        auto x = makeNoise (samples (3.0, fs), 70);
        processMono (core, x, 0, samples (0.25, fs), 512);
        auto off = kBandSet[k];
        off.bypassed = true;
        core.setBandTargets (k, off);
        processMono (core, x, samples (0.25, fs), x.size(), 512);

        LQ_CHECK (core.getBand (k).isIdle(), str ("band 4 not idle after bypass fs=", fs));
        for (int j = 0; j < kBands; ++j)
            if (j != k)
                LQ_CHECK (! core.getBand (j).isIdle(), str ("band ", j + 1, " went idle with band 4"));

        auto without = kBandSet;
        without[k].bypassed = true;
        for (const auto f : { 1000.0, 2500.0 })
        {
            const auto got = measureGainDb (core, fs, f, compositeSettle (kBandSet));
            const auto expected = compositeAnalogDb (without, f, fs);
            LQ_CHECK (std::abs (got - expected) < measureTolDb, str ("bypassed response ", got, " expected ", expected, " at ", f));
        }

        core.setBandTargets (k, kBandSet[k]);
        const auto got = measureGainDb (core, fs, 1000.0, compositeSettle (kBandSet));
        const auto expected = compositeAnalogDb (kBandSet, 1000.0, fs);
        LQ_CHECK (std::abs (got - expected) < measureTolDb, str ("un-bypassed response ", got, " expected ", expected));
    }
}

LQ_TEST ("eq: simultaneous automation of all bands ramps in lockstep")
{
    for (const auto fs : sampleRates)
    {
        auto core = makeCore (fs, kBandSet);
        auto warm = makeNoise (samples (0.1, fs), 80);
        processMono (core, warm);

        // Every band moves at once: up two octaves, Q doubled, gain inverted.
        auto target = kBandSet;
        for (auto& s : target)
        {
            s.frequencyHz = std::min (s.frequencyHz * 4.0, 18000.0);
            s.q *= 2.0;
            s.gainDb = -s.gainDb;
        }
        for (int b = 0; b < kBands; ++b)
            core.setBandTargets (b, target[(size_t) b]);

        const auto expectedTicks = std::lround (BellBand::rampTimeSeconds * fs / BellBand::updateIntervalSamples);
        std::array<long, kBands> arrived {};
        arrived.fill (-1);

        for (long t = 0; t < expectedTicks + 10; ++t)
        {
            auto block = makeNoise (BellBand::updateIntervalSamples, 81 + (std::uint32_t) t);
            processMono (core, block, BellBand::updateIntervalSamples);

            for (int b = 0; b < kBands; ++b)
            {
                const auto& band = core.getBand (b);
                const auto& s = target[(size_t) b];
                const bool there = std::abs (band.getCurrentFrequencyHz() - s.frequencyHz) < 1e-9 * s.frequencyHz
                                && std::abs (band.getCurrentQ() - s.q) < 1e-9 * s.q
                                && band.getCurrentGainDb() == s.gainDb;
                if (arrived[(size_t) b] < 0 && there)
                    arrived[(size_t) b] = t + 1;
            }
        }

        for (int b = 0; b < kBands; ++b)
            LQ_CHECK (arrived[(size_t) b] == expectedTicks,
                      str ("band ", b + 1, " arrived after ", arrived[(size_t) b], " updates, expected ", expectedTicks, " fs=", fs));

        for (int i = 0; i < 30; ++i)
        {
            const auto f = 20.0 * std::pow (0.45 * fs / 20.0, i / 29.0);
            LQ_CHECK (std::abs (toDb (core.magnitudeAt (f)) - compositeAnalogDb (target, f, fs)) < designTolDb,
                      str ("post-automation curve wrong at ", f, " fs=", fs));
        }
    }
}

LQ_TEST ("eq: random automation of all bands stays finite, stable and recovers")
{
    for (const auto fs : sampleRates)
    {
        auto core = makeCore (fs, neutralSet());
        std::mt19937 rng (90);
        std::uniform_real_distribution<double> unit (0.0, 1.0);
        auto y = makeNoise (samples (3.0, fs), 91);
        bool stable = true;

        for (size_t pos = 0; pos < y.size(); pos += 32)
        {
            for (int b = 0; b < kBands; ++b)
            {
                BellSettings s;
                s.frequencyHz = limits::minFrequencyHz * std::pow (limits::maxFrequencyHz / limits::minFrequencyHz, unit (rng));
                s.q           = limits::minQ * std::pow (limits::maxQ / limits::minQ, unit (rng));
                s.gainDb      = limits::minGainDb + (limits::maxGainDb - limits::minGainDb) * unit (rng);
                s.bypassed    = unit (rng) < 0.1;
                core.setBandTargets (b, s);
            }

            processMono (core, y, pos, std::min (pos + 32, y.size()), 32);

            for (int b = 0; b < kBands; ++b)
                stable = stable && isStable (core.getBand (b).getCoefficients());
        }

        LQ_CHECK (stable, str ("a band left the stability triangle fs=", fs));
        LQ_CHECK (allFinite (y), str ("non-finite output under 8-band automation fs=", fs));
        ctx.note (str ("fs=", fs, ": output peak ", peak (y), " for 0.5 peak input"));

        auto off = neutralSet();
        for (int b = 0; b < kBands; ++b)
        {
            off[(size_t) b].bypassed = true;
            core.setBandTargets (b, off[(size_t) b]);
        }

        const auto x = makeNoise (samples (10.0, fs), 92);
        auto z = x;
        processMono (core, z);
        LQ_CHECK (core.isIdle(), str ("not idle 10 s after bypassing all bands fs=", fs));
        const auto tail = z.size() - samples (1.0, fs);
        LQ_CHECK (std::equal (z.begin() + (long) tail, z.end(), x.begin() + (long) tail), "recovered output not bit-exact");
    }
}

LQ_TEST ("eq: channels and bands keep independent state")
{
    for (const auto fs : sampleRates)
    {
        const auto left = makeNoise (samples (0.5, fs), 100);
        const auto right = makeNoise (samples (0.5, fs), 101);

        auto stereo = makeCore (fs, kBandSet, 2);
        auto l = left, r = right;
        processStereo (stereo, l, r, 300);

        auto monoL = makeCore (fs, kBandSet), monoR = makeCore (fs, kBandSet);
        auto ml = left, mr = right;
        processMono (monoL, ml, 300);
        processMono (monoR, mr, 300);

        LQ_CHECK (l == ml, str ("left differs from mono cascade fs=", fs));
        LQ_CHECK (r == mr, str ("right differs from mono cascade fs=", fs));

        auto loud = left;
        Signal silent (left.size(), 0.0f);
        auto core = makeCore (fs, kBandSet, 2);
        processStereo (core, loud, silent, 512);
        LQ_CHECK (peak (silent) == 0.0f, "crosstalk into a silent channel through 8 bands");

        // Band state independence: the cascade equals the 8 bands run one after
        // another as separate single-band filters.
        auto chained = left;
        for (int b = 0; b < kBands; ++b)
        {
            BellBand single;
            single.setTargets (kBandSet[(size_t) b]);
            single.prepare (fs, 1);
            for (size_t pos = 0; pos < chained.size(); pos += 512)
            {
                float* ch[] = { chained.data() + pos };
                single.process (ch, 1, static_cast<int> (std::min<size_t> (512, chained.size() - pos)));
            }
        }
        LQ_CHECK (chained == ml, str ("cascade differs from 8 chained single bands fs=", fs));
    }
}

LQ_TEST ("eq: cascade output is bit-identical for any host block size")
{
    for (const auto fs : sampleRates)
    {
        auto moved = kBandSet;
        moved[1].frequencyHz = 180.0;
        moved[4].gainDb = -9.0;
        moved[6].q = 0.3;
        auto off = kBandSet[7];
        off.bypassed = true;

        const std::vector<CoreEvent> events {
            { samples (0.21, fs), 1, moved[1] }, { samples (0.21, fs), 4, moved[4] }, { samples (0.21, fs), 6, moved[6] },
            { samples (0.43, fs), 7, off },      { samples (0.66, fs), 0, { 3000.0, 10.0, 6.0, false } },
            { samples (0.80, fs), 7, kBandSet[7] },
        };
        const auto x = makeNoise (samples (1.0, fs), 110);
        const auto reference = processScheduled (fs, kBandSet, x, events, [] { return 512; });

        for (const int size : { 1, 7, 16, 64, 333, 4096 })
            LQ_CHECK (processScheduled (fs, kBandSet, x, events, [size] { return size; }) == reference,
                      str ("block size ", size, " differs fs=", fs));

        std::mt19937 rng (111);
        std::uniform_int_distribution<int> dist (1, 2048);
        LQ_CHECK (processScheduled (fs, kBandSet, x, events, [&] { return dist (rng); }) == reference,
                  str ("random block sizes differ fs=", fs));
    }
}

LQ_TEST ("eq: non-finite input is contained through the cascade")
{
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto inf = std::numeric_limits<float>::infinity();

    for (const auto fs : sampleRates)
    {
        auto x = makeNoise (samples (0.5, fs), 120);
        auto clean = x;
        for (const auto i : { size_t { 10 }, size_t { 5000 }, size_t { 5001 }, x.size() - 1 })
        {
            x[i] = (i % 2 == 0) ? nan : (i == 5001 ? -inf : inf);
            clean[i] = 0.0f;
        }

        auto a = makeCore (fs, kBandSet), b = makeCore (fs, kBandSet);
        processMono (a, x);
        processMono (b, clean);
        LQ_CHECK (allFinite (x), str ("non-finite output fs=", fs));
        LQ_CHECK (x == clean, "non-finite samples must behave exactly like zeros");
    }
}

LQ_TEST ("eq: extreme settings stacked in all 8 bands stay stable and decay")
{
    for (const auto fs : sampleRates)
        for (const auto f0 : { limits::minFrequencyHz, limits::maxFrequencyHz })
            for (const auto q : { limits::minQ, limits::maxQ })
                for (const bool alternate : { false, true })
                {
                    std::array<BellSettings, kBands> set {};
                    for (int b = 0; b < kBands; ++b)
                        set[(size_t) b] = { f0, (alternate && b % 2 == 1) ? limits::minGainDb : limits::maxGainDb, q, false };

                    auto core = makeCore (fs, set);
                    bool stable = true;
                    for (int b = 0; b < kBands; ++b)
                        stable = stable && isStable (core.getBand (b).getCoefficients());
                    LQ_CHECK (stable, "stacked extreme setting unstable");

                    // Eight identical poles decay like t^7 e^(-t/tau): allow 4x the single-band settle time.
                    Signal h (samples (4.0 * settleSeconds (f0, limits::maxGainDb, q), fs), 0.0f);
                    h[0] = 1.0f;
                    processMono (core, h);
                    const auto p = peak (h);
                    const auto tail = peak (h, h.size() - h.size() / 10);
                    LQ_CHECK (allFinite (h), "non-finite stacked impulse response");
                    LQ_CHECK (tail < 1.0e-5f * p, str ("stacked response not decaying: tail ", tail, " peak ", p,
                                                       " fs=", fs, " f0=", f0, " q=", q, " alternate=", alternate));
                }
}
