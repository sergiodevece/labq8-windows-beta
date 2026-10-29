// E1b EqBand / multi-filter tests (E, F, G, H, I, J, K, L, M).

#pragma clang diagnostic ignored "-Wfloat-equal" // bit-exactness is a tested property

#include "FilterReference.h"
#include "TestHarness.h"
#include "TestSignals.h"

#include "dsp/BandLimits.h"
#include "dsp/BellBand.h"
#include "dsp/EqBand.h"
#include "dsp/EqCore.h"

#include <array>
#include <limits>
#include <optional>
#include <utility>

using namespace labq8::dsp;
using namespace labq8::test::signals;
using namespace labq8::test::reference;
using labq8::test::str;

namespace
{
template <typename Band>
void run (Band& band, Signal& x, size_t begin, size_t end, int blockSize)
{
    for (auto pos = begin; pos < end;)
    {
        const auto n = std::min (static_cast<size_t> (blockSize), end - pos);
        float* ch[] = { x.data() + pos };
        band.process (ch, 1, static_cast<int> (n));
        pos += n;
    }
}

template <typename Band>
void run (Band& band, Signal& x, int blockSize = 512) { run (band, x, 0, x.size(), blockSize); }

void runStereo (EqBand& band, Signal& l, Signal& r, int blockSize)
{
    for (size_t pos = 0; pos < l.size(); pos += (size_t) blockSize)
    {
        const auto n = static_cast<int> (std::min<size_t> ((size_t) blockSize, l.size() - pos));
        float* ch[] = { l.data() + pos, r.data() + pos };
        band.process (ch, 2, n);
    }
}

EqBand makeBand (double fs, const BandSettings& s, int channels = 1)
{
    EqBand band;
    band.setTargets (s);
    band.prepare (fs, channels);
    return band;
}

BandSettings typed (FilterType type, double f0 = 1000.0, double gain = 6.0, double q = 1.0, bool bypass = false)
{
    return { f0, gain, q, bypass, type };
}

double db (double linear) { return 20.0 * std::log10 (linear); }

double measureGainDb (EqBand& band, double f, double settle)
{
    const auto fs = band.getSampleRate();
    const auto measure = std::max (0.2, 20.0 / f);
    auto x = makeSine (f, fs, samples (settle + measure, fs));
    auto y = x;
    run (band, y);
    const auto begin = samples (settle, fs);
    return db (fitAmplitude (y, begin, y.size(), f, fs) / fitAmplitude (x, begin, x.size(), f, fs));
}

// Largest 4th-order difference in [begin, end): rejects a low tone, keeps clicks.
double maxFourthDifference (const Signal& y, size_t begin, size_t end)
{
    double m = 0.0;
    for (auto i = std::max<size_t> (begin, 4); i < std::min (end, y.size()); ++i)
        m = std::max (m, std::abs ((double) y[i] - 4.0 * y[i - 1] + 6.0 * y[i - 2] - 4.0 * y[i - 3] + y[i - 4]));
    return m;
}

struct RawBiquad
{
    BiquadCoefficients c;
    double s1 = 0, s2 = 0;

    float process (float in)
    {
        const double x = in;
        const auto y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return static_cast<float> (y);
    }
};

struct BandEvent
{
    size_t position;
    BandSettings settings;
};

template <typename NextBlock>
Signal processScheduled (double fs, const BandSettings& initial, const Signal& input,
                         const std::vector<BandEvent>& events, NextBlock nextBlock)
{
    auto band = makeBand (fs, initial);
    auto y = input;
    size_t pos = 0, next = 0;
    while (pos < y.size())
    {
        while (next < events.size() && events[next].position <= pos)
            band.setTargets (events[next++].settings);
        auto n = std::min (static_cast<size_t> (nextBlock()), y.size() - pos);
        if (next < events.size())
            n = std::min (n, events[next].position - pos);
        run (band, y, pos, pos + n, static_cast<int> (n));
        pos += n;
    }
    return y;
}

float ulpScale (float a, float b) { return std::max ({ std::abs (a), std::abs (b), 1.0f / 1024.0f }) * 1.2e-7f; }
} // namespace

//==============================================================================
LQ_TEST ("band: Bell type is bit-identical to the frozen E0 BellBand (automation and bypass)")
{
    for (const auto fs : sampleRates)
        for (const int channels : { 1, 2 })
        {
            EqBand band;
            BellBand reference;
            band.prepare (fs, channels);
            reference.prepare (fs, channels);

            std::mt19937 rng (500);
            std::uniform_real_distribution<double> u (0.0, 1.0);
            auto l = makeNoise (samples (2.0, fs), 501), r = makeNoise (l.size(), 502);
            auto l2 = l, r2 = r;

            for (size_t pos = 0; pos < l.size(); pos += 37)
            {
                BellSettings s { 20.0 * std::pow (1000.0, u (rng)), -24.0 + 48.0 * u (rng), 0.1 * std::pow (180.0, u (rng)), u (rng) < 0.2 };
                if (u (rng) < 0.3)
                    s.gainDb = 0.0; // exercise the idle path
                band.setTargets (s);
                reference.setTargets (s);
                const auto n = (int) std::min<size_t> (37, l.size() - pos);
                float* a[] = { l.data() + pos, r.data() + pos };
                float* b[] = { l2.data() + pos, r2.data() + pos };
                band.process (a, channels, n);
                reference.process (b, channels, n);
            }

            LQ_CHECK (l == l2 && r == r2, str ("EqBand(Bell) != BellBand, fs=", fs, " channels=", channels));
        }
}

LQ_TEST ("band: Bell and shelves at 0 dB are bit-exact transparent and idle (G)")
{
    for (const auto fs : sampleRates)
        for (const auto type : { FilterType::bell, FilterType::lowShelf, FilterType::highShelf })
        {
            for (const auto f0 : { 20.0, 1000.0, 18000.0 })
                for (const auto q : { 0.1, 1.0, 18.0 })
                {
                    auto band = makeBand (fs, typed (type, f0, 0.0, q), 2);
                    const auto l = makeNoise (4096, 510), r = makeNoise (4096, 511);
                    auto l2 = l, r2 = r;
                    runStereo (band, l2, r2, 333);
                    LQ_CHECK (l2 == l && r2 == r && band.isIdle(), str (name (type), " at 0 dB not transparent, fs=", fs, " f0=", f0, " q=", q));
                }

            // Back to 0 dB at runtime: idle and bit-exact once settled.
            auto band = makeBand (fs, typed (type, 400.0, 9.0, 2.0));
            const auto x = makeNoise (samples (2.5, fs), 512);
            auto y = x;
            run (band, y, 0, samples (0.25, fs), 512);
            band.setTargets (typed (type, 400.0, 0.0, 2.0));
            run (band, y, samples (0.25, fs), y.size(), 512);
            const auto tail = y.size() - samples (0.5, fs);
            LQ_CHECK (band.isIdle() && std::equal (y.begin() + (long) tail, y.end(), x.begin() + (long) tail),
                      str (name (type), " not transparent after returning to 0 dB, fs=", fs));
        }
}

LQ_TEST ("band: Cut and Notch stay active at 0 dB and ignore gain entirely (H)")
{
    for (const auto fs : sampleRates)
        for (const auto type : { FilterType::lowCut, FilterType::highCut, FilterType::notch })
        {
            const auto x = makeNoise (samples (0.3, fs), 520);
            Signal outputs[3];
            int i = 0;
            for (const auto g : { 0.0, -24.0, 24.0 })
            {
                auto band = makeBand (fs, typed (type, 1000.0, g, 0.7071));
                LQ_CHECK (! band.isIdle(), str (name (type), " idle at gain ", g));
                outputs[i] = x;
                run (band, outputs[i++]);
            }
            LQ_CHECK (outputs[0] != x, str (name (type), " at 0 dB does nothing"));
            LQ_CHECK (outputs[0] == outputs[1] && outputs[0] == outputs[2], str (name (type), " output depends on gain, fs=", fs));

            // And it really filters: response at f0 (cuts: Q = 0.7071 -> -3.01 dB; notch: deep).
            auto band = makeBand (fs, typed (type, 1000.0, 0.0, 0.7071));
            const auto g = measureGainDb (band, 1000.0, 0.25);
            if (type == FilterType::notch)
                LQ_CHECK (g < -80.0, str ("notch at 0 dB only ", g, " dB at f0"));
            else
                LQ_CHECK (std::abs (g - db (0.7071)) < measureTolDb, str (name (type), " at 0 dB: ", g, " dB at f0"));
        }
}

LQ_TEST ("band: individual bypass fades every topology to the dry signal and back (G)")
{
    for (const auto fs : sampleRates)
        for (const auto type : allTypes)
        {
            const auto on = typed (type, 1000.0, 9.0, 2.0);
            auto off = on;
            off.bypassed = true;

            auto band = makeBand (fs, on);
            const auto x = makeNoise (samples (2.5, fs), 530);
            auto y = x;
            const auto t0 = samples (0.25, fs) + 3; // off the 16-sample grid on purpose
            run (band, y, 0, t0, 512);
            band.setTargets (off);

            if (usesGain (type))
            {
                // E0 semantics: gain ramps to 0 dB (exact identity), then idle.
                LQ_CHECK (! band.isCrossfading(), str (name (type), " bypass should ramp gain, not crossfade"));
                run (band, y, t0, y.size(), 512);
            }
            else
            {
                // Cut / notch: linear crossfade to the dry path, exactly N samples, then idle.
                LQ_CHECK (band.isCrossfading() && band.isFadingPathDry() == false && ! band.getActiveFilter().has_value(),
                          str (name (type), " bypass did not start a fade to dry"));
                const auto n = (size_t) band.getCrossfadeLengthSamples();
                LQ_CHECK ((long) n == std::lround (0.02 * fs), "fade length");

                // Reference: the same filter running alone.
                auto ref = makeBand (fs, on);
                auto yr = x;
                run (ref, yr, 0, t0 + n, 512);

                run (band, y, t0, t0 + n - 1, 1);
                LQ_CHECK (! band.isIdle(), "idle one sample early");
                run (band, y, t0 + n - 1, t0 + n, 1);
                LQ_CHECK (band.isIdle(), str (name (type), " not idle right after the fade"));

                bool mixOk = true;
                for (size_t k = 0; k < n; ++k)
                {
                    const auto w = (double) (k + 1) / (double) n;
                    const auto expected = (float) ((double) yr[t0 + k] + w * ((double) x[t0 + k] - (double) yr[t0 + k]));
                    mixOk = mixOk && std::abs (y[t0 + k] - expected) <= 2.0f * ulpScale (yr[t0 + k], x[t0 + k]);
                }
                LQ_CHECK (mixOk, str (name (type), " bypass fade is not filtered->dry linear mix, fs=", fs));
                run (band, y, t0 + n, y.size(), 512);
            }

            const auto tail = y.size() - samples (0.5, fs);
            LQ_CHECK (band.isIdle() && std::equal (y.begin() + (long) tail, y.end(), x.begin() + (long) tail),
                      str (name (type), " bypassed tail not bit-exact dry, fs=", fs));

            // Un-bypass restores the curve.
            band.setTargets (on);
            const auto f = type == FilterType::notch ? 700.0 : 1000.0;
            const auto got = measureGainDb (band, f, 0.3);
            const auto expected = db (std::abs (analogResponse (type, f, 1000.0, 9.0, 2.0, fs)));
            LQ_CHECK (std::abs (got - expected) < measureTolDb, str (name (type), " after un-bypass ", got, " dB expected ", expected));
        }
}

LQ_TEST ("crossfade: output is the linear mix of two independent paths, convex, exact at the end (K)")
{
    for (const auto fs : { 44100.0, 96000.0 })
        for (const auto a : allTypes)
            for (const auto b : allTypes)
            {
                if (a == b)
                    continue;

                const auto x = makeNoise (samples (0.5, fs), 540);
                const size_t t0 = 12345; // off the 16-sample grid
                auto band = makeBand (fs, typed (a));
                auto y = x;
                run (band, y, 0, t0, 512);
                band.setTargets (typed (b));
                LQ_CHECK (band.isCrossfading() && band.getActiveFilter() == b && band.getFadingFilter() == a, "fade started");
                run (band, y, t0, y.size(), 512);
                const auto n = (size_t) band.getCrossfadeLengthSamples();

                auto refA = makeBand (fs, typed (a));
                auto ya = x;
                run (refA, ya);

                Signal yb (x.begin() + (long) t0, x.end()); // new path: fresh state from t0
                auto refB = makeBand (fs, typed (b));
                run (refB, yb);

                bool mixOk = true, convex = true;
                for (size_t k = 0; k < n; ++k)
                {
                    const auto w = (double) (k + 1) / (double) n;
                    const auto oldY = ya[t0 + k], newY = yb[k];
                    const auto expected = (float) ((double) oldY + w * ((double) newY - (double) oldY));
                    mixOk = mixOk && std::abs (y[t0 + k] - expected) <= 4.0f * ulpScale (oldY, newY);
                    convex = convex && std::abs (y[t0 + k]) <= std::max (std::abs (oldY), std::abs (newY)) + 4.0f * ulpScale (oldY, newY);
                }
                LQ_CHECK (mixOk, str (name (a), "->", name (b), " is not old + w(new - old), fs=", fs));
                LQ_CHECK (convex, str (name (a), "->", name (b), " exceeds the larger path (law not convex), fs=", fs));

                // After the fade the band is exactly the new path: no step at the end.
                LQ_CHECK (std::equal (y.begin() + (long) (t0 + n), y.end(), yb.begin() + (long) n),
                          str (name (a), "->", name (b), " differs from the new path after the fade, fs=", fs));
            }
}

LQ_TEST ("crossfade: lasts round(20 ms * fs) samples at every rate and is block-size independent (K, L)")
{
    for (const auto fs : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        const auto expectedN = (size_t) std::lround (0.02 * fs);
        const auto x = makeNoise (samples (0.3, fs), 550);
        const size_t t0 = 1001;

        auto band = makeBand (fs, typed (FilterType::lowShelf));
        auto y = x;
        run (band, y, 0, t0, 64);
        band.setTargets (typed (FilterType::highCut));
        size_t counted = 0;
        for (size_t pos = t0; pos < y.size() && band.isCrossfading(); ++pos, ++counted)
            run (band, y, pos, pos + 1, 1);
        LQ_CHECK (counted == expectedN, str ("fade lasted ", counted, " samples, expected ", expectedN, " at fs=", fs));

        const std::vector<BandEvent> events { { t0, typed (FilterType::highCut) } };
        const auto reference = processScheduled (fs, typed (FilterType::lowShelf), x, events, [] { return 1; });
        for (const int size : { 7, 64, 512, 4096 })
            LQ_CHECK (processScheduled (fs, typed (FilterType::lowShelf), x, events, [size] { return size; }) == reference,
                      str ("block size ", size, " changes the fade, fs=", fs));
    }
}

LQ_TEST ("crossfade: policy for requests during a fade (J)")
{
    // Only the type differs: frequency, gain and Q are band parameters shared
    // by both paths (a simultaneous parameter change ramps both).
    const double fs = 48000.0;
    const auto A = typed (FilterType::bell, 1000.0, 9.0, 2.0);
    const auto B = typed (FilterType::lowCut, 1000.0, 9.0, 2.0);
    const auto C = typed (FilterType::notch, 1000.0, 9.0, 2.0);
    const auto D = typed (FilterType::highShelf, 1000.0, 9.0, 2.0);
    const auto x = makeNoise (samples (0.5, fs), 560);
    const size_t t0 = 4000;

    // (a) Back to the outgoing type mid-fade: reversed in place, never reset.
    {
        auto band = makeBand (fs, A);
        const auto n = (size_t) band.getCrossfadeLengthSamples();
        auto y = x;
        run (band, y, 0, t0, 512);
        band.setTargets (B);
        run (band, y, t0, t0 + 300, 1);
        band.setTargets (A);
        LQ_CHECK (band.isCrossfading() && band.getActiveFilter() == FilterType::bell && band.getCrossfadePosition() == (int) (n - 300),
                  str ("reverse: position ", band.getCrossfadePosition(), " expected ", n - 300));
        run (band, y, t0 + 300, t0 + 600, 1);
        LQ_CHECK (! band.isCrossfading() && band.getActiveFilter() == FilterType::bell, "reverse completes after the same 300 samples");
        run (band, y, t0 + 600, y.size(), 512);

        auto refA = makeBand (fs, A);
        auto ya = x;
        run (refA, ya);
        LQ_CHECK (std::equal (y.begin() + (long) (t0 + 600), y.end(), ya.begin() + (long) (t0 + 600)),
                  "after reversing, the original path continues untouched (bit-exact)");
        LQ_CHECK (maxFourthDifference (y, t0 + 280, t0 + 320) < 10.0 * maxFourthDifference (ya, t0 + 280, t0 + 320) + 1e-3,
                  "no discontinuity at the reversal");
    }

    // (b) Other types mid-fade: one pending slot, latest wins, reached within 2N.
    {
        auto band = makeBand (fs, A);
        const auto n = (size_t) band.getCrossfadeLengthSamples();
        auto y = x;
        run (band, y, 0, t0, 512);
        band.setTargets (B);
        bool sawC = false;
        size_t reachedD = 0;
        for (size_t pos = t0; pos < t0 + 3 * n; ++pos)
        {
            if (pos == t0 + 100) band.setTargets (C);
            if (pos == t0 + 200) band.setTargets (D);
            run (band, y, pos, pos + 1, 1);
            sawC = sawC || band.getActiveFilter() == FilterType::notch;
            if (reachedD == 0 && ! band.isCrossfading() && band.getActiveFilter() == FilterType::highShelf)
                reachedD = pos + 1 - t0;
        }
        LQ_CHECK (! sawC, "an overwritten pending type must never become active");
        LQ_CHECK (reachedD == 2 * n, str ("last request reached after ", reachedD, " samples, expected ", 2 * n));
        LQ_CHECK (allFinite (y), "finite");
    }

    // (c) Same type as the incoming one: no-op. (d) C then B again: pending cleared.
    {
        auto band = makeBand (fs, A);
        const auto n = (size_t) band.getCrossfadeLengthSamples();
        auto y = x;
        run (band, y, 0, t0, 512);
        band.setTargets (B);
        run (band, y, t0, t0 + 50, 1);
        band.setTargets (B);
        LQ_CHECK (! band.hasPendingChange(), "same type queued a change");
        band.setTargets (C);
        LQ_CHECK (band.hasPendingChange(), "C not queued");
        band.setTargets (B);
        LQ_CHECK (! band.hasPendingChange(), "returning to the incoming type must clear the pending one");
        run (band, y, t0 + 50, t0 + n, 1);
        LQ_CHECK (! band.isCrossfading() && band.getActiveFilter() == FilterType::lowCut, "single fade A->B");
    }

    // (e) Reversed before a single sample was faded: as if nothing happened.
    {
        auto band = makeBand (fs, A);
        auto y = x;
        run (band, y, 0, t0, 512);
        band.setTargets (B);
        band.setTargets (A);
        LQ_CHECK (! band.isCrossfading() && band.getActiveFilter() == FilterType::bell, "immediate reversal");
        run (band, y, t0, y.size(), 512);
        auto refA = makeBand (fs, A);
        auto ya = x;
        run (refA, ya);
        LQ_CHECK (y == ya, "immediate reversal altered the audio");
    }
}

LQ_TEST ("crossfade: smoothed type changes reduce discontinuities vs an abrupt topology switch (K)")
{
    // Control: a single biquad whose coefficients are swapped at t0 keeping its
    // state, i.e. a naive topology change. Metric M: largest 4th difference in
    // the 30 ms after t0 (a 200 Hz / 3 kHz tone is attenuated by its 4th
    // difference, steps and kinks are not); a step of height h gives M = 3h.
    // Changes land on a tone peak, the worst case.
    //
    // Criteria:
    //  1. Where the abrupt switch is a real click (M >= 0.03, a -40 dBFS step
    //     or worse) the crossfade must reduce it by >= 20 dB.
    //  2. Every smoothed change stays within the filters' own steady level plus
    //     0.003 (a -60 dBFS step equivalent). The residue is the new path
    //     starting from zero state, a short ring at its own resonance scaled by
    //     the small early w (documented in ARCHITECTURE.md).
    constexpr double loudClick = 0.03, residueBound = 3.0e-3;
    int clicking = 0, mild = 0;
    double worstRatio = 0.0, worstSmooth = 0.0;
    std::string worstPair;

    for (const auto fs : { 48000.0 })
        for (const auto tone : { 200.0, 3000.0 })
            for (const auto a : allTypes)
                for (const auto b : allTypes)
                {
                    if (a == b)
                        continue;

                    const auto sa = typed (a, 1000.0, 9.0, 2.0), sb = typed (b, 1000.0, 9.0, 2.0);
                    const auto x = makeSine (tone, fs, samples (0.4, fs), 0.25);
                    const auto t0 = samples ((std::floor (0.2 * tone) + 0.25) / tone, fs);
                    const auto window = samples (0.03, fs);

                    const auto smooth = processScheduled (fs, sa, x, { { t0, sb } }, [] { return 512; });

                    RawBiquad raw { designFilter (a, fs, 1000.0, 9.0, 2.0) };
                    Signal abrupt (x.size());
                    for (size_t i = 0; i < x.size(); ++i)
                    {
                        if (i == t0)
                            raw.c = designFilter (b, fs, 1000.0, 9.0, 2.0);
                        abrupt[i] = raw.process (x[i]);
                    }

                    // Steady-state reference: the larger of either filter's own 4th difference.
                    auto onlyA = processScheduled (fs, sa, x, {}, [] { return 512; });
                    auto onlyB = processScheduled (fs, sb, x, {}, [] { return 512; });
                    const auto steady = std::max (maxFourthDifference (onlyA, t0 - window, t0),
                                                  maxFourthDifference (onlyB, t0 + window, t0 + 2 * window));

                    const auto ms = maxFourthDifference (smooth, t0, t0 + window);
                    const auto ma = maxFourthDifference (abrupt, t0, t0 + window);

                    if (ma >= loudClick)
                    {
                        ++clicking;
                        LQ_CHECK (ms <= 0.1 * ma, str (name (a), "->", name (b), " tone ", tone, ": smoothed ", ms, " vs abrupt ", ma,
                                                      " (< 20 dB reduction), steady ", steady));
                        if (ms / ma > worstRatio)
                        {
                            worstRatio = ms / ma;
                            worstPair = str (name (a), "->", name (b), " @", tone, " Hz");
                        }
                    }
                    else
                    {
                        ++mild;
                    }

                    LQ_CHECK (ms <= steady + residueBound, str (name (a), "->", name (b), " tone ", tone, ": smoothed metric ", ms, " steady ", steady));
                    worstSmooth = std::max (worstSmooth, ms - steady);
                }

    LQ_CHECK (clicking >= 30, str ("only ", clicking, " of 60 pair/tone cases click loudly when switched abruptly: test not sensitive"));
    ctx.note (str (clicking, " of 60 abrupt switches are loud clicks (>= -40 dBFS step), ", mild, " mild; worst smoothed/abrupt ratio on loud ones ",
                   db (worstRatio), " dB (", worstPair, "); worst smoothed excess over steady ", worstSmooth, " (",
                   db (worstSmooth / 3.0), " dBFS step-equivalent)"));
}

LQ_TEST ("crossfade: phase cancellation during a fade matches theory (K, measured)")
{
    // During a fade the output at frequency f is ((1 - w) H_old(f) + w H_new(f)) * input.
    // When the two responses are out of phase this dips below both; the law
    // cannot prevent it. Measured with a tone at f0 (worst for cut <-> cut).
    const double fs = 48000.0, f0 = 1000.0, q = 0.7071;
    double worstDb = 0.0;
    std::string worstPair;
    int checked = 0;

    for (const auto a : allTypes)
        for (const auto b : allTypes)
        {
            if (a == b)
                continue;

            for (const auto tone : { f0, 300.0, 3000.0 })
            {
                const auto ca = designFilter (a, fs, f0, 6.0, q), cb = designFilter (b, fs, f0, 6.0, q);
                const auto ha = digitalResponse (ca, tone, fs), hb = digitalResponse (cb, tone, fs);

                const auto x = makeSine (tone, fs, samples (0.3, fs), 0.25);
                const size_t t0 = samples (0.1, fs);
                const auto y = processScheduled (fs, typed (a, f0, 6.0, q), x, { { t0, typed (b, f0, 6.0, q) } }, [] { return 256; });

                const auto n = (size_t) std::lround (0.02 * fs);
                const auto period = (size_t) std::lround (fs / tone * std::ceil (tone / 1000.0));
                bool agrees = true;
                double measuredMin = 1e9, theoryMin = 1e9;

                // Skip the first 2 ms (new path's start-up transient), then compare per window.
                for (size_t start = t0 + samples (0.002, fs); start + period <= t0 + n; start += period)
                {
                    const auto centreW = ((double) (start - t0) + 0.5 * (double) period) / (double) n;
                    const auto theory = std::abs ((1.0 - centreW) * ha + centreW * hb);
                    const auto measured = fitAmplitude (y, start, start + period, tone, fs) / 0.25;
                    // The mix changes inside a window; the error of a one-window
                    // fit scales with the larger path, not with the (possibly
                    // cancelled) result.
                    agrees = agrees && std::abs (measured - theory) < 0.02 * std::max ({ 1.0, std::abs (ha), std::abs (hb) });
                    measuredMin = std::min (measuredMin, measured);
                    theoryMin = std::min (theoryMin, theory);
                }

                ++checked;
                LQ_CHECK (agrees, str (name (a), "->", name (b), " at ", tone, " Hz: measured fade envelope departs from theory (min measured ",
                                       measuredMin, ", theory ", theoryMin, ")"));

                const auto dip = db (std::max (measuredMin, 1e-6)) - db (std::min (std::abs (ha), std::abs (hb)));
                if (dip < worstDb)
                {
                    worstDb = dip;
                    worstPair = str (name (a), "->", name (b), " @", tone, " Hz; theory min |H| ", theoryMin);
                }
            }
        }

    ctx.note (str (checked, " pair/tone cases; deepest measured dip below the quieter path: ", worstDb, " dB (", worstPair, ")"));
}

LQ_TEST ("type changes: fast random changes with automation stay finite, stable and converge (J)")
{
    for (const auto fs : sampleRates)
    {
        EqBand band;
        band.prepare (fs, 1);
        std::mt19937 rng (570);
        std::uniform_real_distribution<double> u (0.0, 1.0);
        std::uniform_int_distribution<int> typeDist (0, numFilterTypes - 1), gap (1, 200);
        auto y = makeNoise (samples (3.0, fs), 571);
        BandSettings last;
        bool stable = true;

        for (size_t pos = 0; pos < y.size();)
        {
            last = { 20.0 * std::pow (1000.0, u (rng)), -24.0 + 48.0 * u (rng), 0.1 * std::pow (180.0, u (rng)),
                     u (rng) < 0.1, filterTypeFromIndex (typeDist (rng)) };
            band.setTargets (last);
            const auto n = std::min ((size_t) gap (rng), y.size() - pos);
            run (band, y, pos, pos + n, (int) n);
            stable = stable && isStable (band.getCoefficients());
            pos += n;
        }

        LQ_CHECK (allFinite (y), str ("non-finite output, fs=", fs));
        LQ_CHECK (stable, str ("unstable coefficients, fs=", fs));
        LQ_CHECK (peak (y) < 100.0f, str ("peak ", peak (y), " for 0.5 input, fs=", fs));
        ctx.note (str ("fs=", fs, ": peak ", peak (y), " for 0.5 peak input"));

        // Converges to the last request within two fades.
        auto tail = makeNoise ((size_t) (2 * band.getCrossfadeLengthSamples() + 32), 572);
        run (band, tail, 1);
        LQ_CHECK (! band.isCrossfading(), "still fading 2N after the last request");
        const auto expectedKind = (last.bypassed && ! usesGain (last.type)) ? std::optional<FilterType> {} : std::optional<FilterType> { last.type };
        LQ_CHECK (band.getActiveFilter() == expectedKind, "did not land on the last requested type");
    }
}

LQ_TEST ("band: output bit-identical for any block size with type changes, mid-fade requests and bypass (L)")
{
    for (const auto fs : sampleRates)
    {
        const std::vector<BandEvent> events {
            { samples (0.10, fs), typed (FilterType::lowCut, 200.0, 3.0, 0.7071) },
            { samples (0.105, fs), typed (FilterType::notch, 900.0, 3.0, 6.0) },           // pending
            { samples (0.108, fs), typed (FilterType::highShelf, 4000.0, -5.0, 1.0) },     // overwrites pending
            { samples (0.20, fs), typed (FilterType::highShelf, 4000.0, -5.0, 1.0, true) },// gain bypass
            { samples (0.30, fs), typed (FilterType::highCut, 6000.0, -5.0, 2.0) },
            { samples (0.31, fs), typed (FilterType::highShelf, 4000.0, -5.0, 1.0, true) },// reverse
            { samples (0.40, fs), typed (FilterType::notch, 50.0, 0.0, 18.0, true) },      // to dry
            { samples (0.50, fs), typed (FilterType::notch, 50.0, 0.0, 18.0) },
        };
        const auto x = makeNoise (samples (0.8, fs), 580);
        const auto reference = processScheduled (fs, typed (FilterType::bell), x, events, [] { return 512; });

        for (const int size : { 1, 7, 16, 64, 333, 4096 })
            LQ_CHECK (processScheduled (fs, typed (FilterType::bell), x, events, [size] { return size; }) == reference,
                      str ("block size ", size, " differs, fs=", fs));

        std::mt19937 rng (581);
        std::uniform_int_distribution<int> dist (1, 2048);
        LQ_CHECK (processScheduled (fs, typed (FilterType::bell), x, events, [&] { return dist (rng); }) == reference,
                  str ("random block sizes differ, fs=", fs));
    }
}

LQ_TEST ("band: non-finite input is contained in single-path and crossfade modes (M)")
{
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto inf = std::numeric_limits<float>::infinity();

    for (const auto fs : sampleRates)
    {
        const size_t t0 = 3000;
        const std::vector<BandEvent> events { { t0, typed (FilterType::lowCut, 300.0) } };
        auto x = makeNoise (samples (0.3, fs), 590);
        auto clean = x;
        for (const auto i : { size_t { 100 }, t0 + 5, t0 + 6, t0 + 200, x.size() - 1 })
        {
            x[i] = (i % 2 == 0) ? nan : inf;
            clean[i] = 0.0f;
        }
        const auto y = processScheduled (fs, typed (FilterType::notch, 1000.0), x, events, [] { return 512; });
        const auto yc = processScheduled (fs, typed (FilterType::notch, 1000.0), clean, events, [] { return 512; });
        LQ_CHECK (allFinite (y), str ("non-finite output, fs=", fs));
        LQ_CHECK (y == yc, "non-finite samples must behave exactly like zeros");
    }
}

LQ_TEST ("band: stereo equals two monos through type changes; no crosstalk (E)")
{
    for (const auto fs : sampleRates)
    {
        const auto left = makeNoise (samples (0.4, fs), 600), right = makeNoise (left.size(), 601);
        const std::vector<BandEvent> events { { samples (0.1, fs), typed (FilterType::highCut, 2000.0) },
                                              { samples (0.2, fs), typed (FilterType::notch, 500.0, 0.0, 3.0, true) },
                                              { samples (0.3, fs), typed (FilterType::lowShelf, 150.0, 8.0) } };

        auto stereo = makeBand (fs, typed (FilterType::bell), 2);
        auto l = left, r = right;
        Signal silent (left.size(), 0.0f);
        auto l2 = left;
        auto silentBand = makeBand (fs, typed (FilterType::bell), 2);
        size_t pos = 0, next = 0;
        while (pos < l.size())
        {
            while (next < events.size() && events[next].position <= pos)
            {
                stereo.setTargets (events[next].settings);
                silentBand.setTargets (events[next].settings);
                ++next;
            }
            auto n = std::min<size_t> (300, l.size() - pos);
            if (next < events.size())
                n = std::min (n, events[next].position - pos);
            float* ch[] = { l.data() + pos, r.data() + pos };
            stereo.process (ch, 2, (int) n);
            float* ch2[] = { l2.data() + pos, silent.data() + pos };
            silentBand.process (ch2, 2, (int) n);
            pos += n;
        }

        const auto ml = processScheduled (fs, typed (FilterType::bell), left, events, [] { return 300; });
        const auto mr = processScheduled (fs, typed (FilterType::bell), right, events, [] { return 300; });
        LQ_CHECK (l == ml && r == mr, str ("stereo differs from two monos, fs=", fs));
        LQ_CHECK (peak (silent) == 0.0f, "crosstalk into a silent channel");
    }
}

LQ_TEST ("multi: eight bands of different types in cascade (F)")
{
    const std::array<BandSettings, 8> set { {
        { 30.0, 0.0, 0.7071, false, FilterType::lowCut },
        { 100.0, 3.0, 0.7071, false, FilterType::lowShelf },
        { 400.0, -4.0, 1.5, false, FilterType::bell },
        { 1000.0, 0.0, 8.0, false, FilterType::notch },
        { 2500.0, 5.0, 1.0, false, FilterType::bell },
        { 6000.0, -3.0, 0.7071, false, FilterType::highShelf },
        { 10000.0, 2.0, 2.0, false, FilterType::bell },
        { 16000.0, 0.0, 0.7071, false, FilterType::highCut },
    } };

    auto expectedDb = [&] (double f, double fs)
    {
        std::complex<double> h = 1.0;
        for (const auto& s : set)
            h *= analogResponse (s.type, f, s.frequencyHz, s.gainDb, s.q, fs);
        return db (std::abs (h));
    };

    for (const auto fs : sampleRates)
    {
        auto makeCore = [&]
        {
            EqCore core;
            for (int b = 0; b < 8; ++b)
                core.setBandTargets (b, set[(size_t) b]);
            core.prepare (fs, 1);
            return core;
        };

        auto core = makeCore();
        LQ_CHECK (! core.isIdle(), "mixed cascade should be active");
        for (int k = 0; k < 60; ++k)
        {
            const auto f = 20.0 * std::pow (0.45 * fs / 20.0, k / 59.0);
            if (std::abs (f - 1000.0) < 200.0)
                continue; // notch centre: compared in linear terms by the filter tests
            LQ_CHECK (std::abs (db (core.magnitudeAt (f)) - expectedDb (f, fs)) < designTolDb, str ("composite off at ", f, " Hz, fs=", fs));
        }

        for (const auto f : { 25.0, 60.0, 150.0, 400.0, 2500.0, 6000.0, 10000.0, 15000.0 })
        {
            auto measured = makeCore();
            const auto x = makeSine (f, fs, samples (1.5, fs));
            auto y = x;
            for (size_t pos = 0; pos < y.size(); pos += 512)
            {
                float* ch[] = { y.data() + pos };
                measured.process (ch, 1, (int) std::min<size_t> (512, y.size() - pos));
            }
            const auto begin = samples (1.0, fs);
            const auto got = db (fitAmplitude (y, begin, y.size(), f, fs) / fitAmplitude (x, begin, x.size(), f, fs));
            LQ_CHECK (std::abs (got - expectedDb (f, fs)) < measureTolDb, str ("measured ", got, " expected ", expectedDb (f, fs), " at ", f, " Hz, fs=", fs));
        }
    }
}

LQ_TEST ("band: frequency, Q and gain automation ramps every topology like Bell (I)")
{
    for (const auto fs : sampleRates)
        for (const auto type : allTypes)
        {
            const auto from = typed (type, 500.0, 6.0, 1.0);
            const auto to = typed (type, 2000.0, -9.0, 2.0);
            auto band = makeBand (fs, from);
            auto warm = makeNoise (samples (0.05, fs), 610);
            run (band, warm);
            band.setTargets (to);
            LQ_CHECK (! band.isCrossfading(), "parameter automation must not crossfade");

            const auto expectedTicks = std::lround (EqBand::rampTimeSeconds * fs / EqBand::updateIntervalSamples);
            long arrived = -1;
            bool monotonic = true;
            double lastF = band.getCurrentFrequencyHz(), lastQ = band.getCurrentQ();
            for (long t = 0; t < expectedTicks + 8; ++t)
            {
                auto block = makeNoise (EqBand::updateIntervalSamples, 611 + (std::uint32_t) t);
                run (band, block, EqBand::updateIntervalSamples);
                const auto f = band.getCurrentFrequencyHz(), q = band.getCurrentQ();
                monotonic = monotonic && f >= lastF && q >= lastQ;
                lastF = f;
                lastQ = q;
                if (arrived < 0 && std::abs (f - 2000.0) < 1e-9 * 2000.0 && std::abs (q - 2.0) < 1e-9 * 2.0 && band.getCurrentGainDb() == -9.0)
                    arrived = t + 1;
            }
            LQ_CHECK (monotonic && arrived == expectedTicks, str (name (type), " ramp arrived after ", arrived, " (expected ", expectedTicks, "), fs=", fs));

            bool ok = true;
            for (int k = 0; k < 30; ++k)
            {
                const auto f = 20.0 * std::pow (0.45 * fs / 20.0, k / 29.0);
                const auto expected = std::abs (analogResponse (type, f, 2000.0, -9.0, 2.0, fs));
                const auto got = magnitudeAt (band.getCoefficients(), f, fs);
                ok = ok && (expected > 1e-3 ? std::abs (db (got) - db (expected)) < designTolDb : std::abs (got - expected) < 1e-8);
            }
            LQ_CHECK (ok, str (name (type), " curve after automation, fs=", fs));
        }
}
