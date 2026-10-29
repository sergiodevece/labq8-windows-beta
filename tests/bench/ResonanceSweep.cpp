// labq8_sweep: mathematical sweep of the shelf and cut responses (E1b.1).
//
// Every RBJ biquad here is the bilinear transform of its analog prototype,
// prewarped at f0, so its response depends on f and fs only through
// w0 = 2 pi f0 / fs. The admitted (f0, fs) pairs (20 Hz .. 20 kHz, f0 <=
// 0.49 fs, fs 8 .. 768 kHz) cover f0 / fs in [20 / 768000, 0.49]; the sweep
// walks that whole interval, so it covers every frequency and sample rate.
//
// Each f0 / fs is designed at a real admitted pair (pointFor): the designers
// replace an invalid fs or f0, so normalised units cannot be used directly.
//
// Sections:
//   1. Shelves: largest excursion outside the plateau range [min(0,G), max(0,G)]
//      (overshoot) for each Q and gain, digital (over w0) and analog.
//   2. Cuts: resonance peak for each Q, digital and analog.
//   3. Stability: pole radius and coefficient sanity for all six types over
//      the full Q range, at extreme gains and w0.
//   4. Headroom: L1 norm of the impulse response (largest possible |y| for
//      |x| <= 1) and step-response peak at the proposed limits.
//
// Uses the production designers (src/dsp/BiquadDesign.cpp) and the
// independent analog references of the tests (tests/FilterReference.h).

#include "FilterReference.h"
#include "dsp/BiquadDesign.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

using labq8::dsp::BiquadCoefficients;
using labq8::dsp::FilterType;
namespace ref = labq8::test::reference;

namespace
{
constexpr double minRatio = 20.0 / 768000.0; // lowest f0 / fs admitted
constexpr double maxRatio = 0.49;            // highest f0 / fs admitted
constexpr double qGrid[] = { 0.1, 0.25, 0.5, 0.7071, 0.85, 1.0, 1.25, 1.5, 2.0, 3.0, 5.0, 10.0, 18.0 };

std::vector<double> logGrid (double lo, double hi, int n)
{
    std::vector<double> v ((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = lo * std::pow (hi / lo, (double) i / (n - 1));
    return v;
}

double db (double x) { return 20.0 * std::log10 (x); }

// An admitted (f0, fs) pair for a given f0 / fs: fs in [8, 768] kHz and f0 in
// [20 Hz, 20 kHz] (the designers would silently replace anything else).
struct Point
{
    double fs, f0;
};

Point pointFor (double ratio)
{
    const auto fs = std::clamp (1000.0 / ratio, 8000.0, 768000.0);
    return { fs, ratio * fs };
}

BiquadCoefficients design (FilterType type, double ratio, double gainDb, double q)
{
    const auto p = pointFor (ratio);
    return labq8::dsp::designFilter (type, p.fs, p.f0, gainDb, q);
}

// Extremes of |H| in dB over (0, fs/2) for the design at pointFor (ratio).
struct Extremes
{
    double maxDb = -1.0e9, minDb = 1.0e9;
};

Extremes digitalExtremes (const BiquadCoefficients& c, double ratio)
{
    Extremes e;
    auto visit = [&] (double f)
    {
        const auto m = db (std::abs (ref::digitalResponse (c, f, 1.0))); // f normalised to fs
        e.maxDb = std::max (e.maxDb, m);
        e.minDb = std::min (e.minDb, m);
    };

    // Dense log grid around f0 (where shelves and cuts have their extremes),
    // plus the band edges.
    for (const auto f : logGrid (std::max (1.0e-9, ratio * 1.0e-3), 0.4999999, 6000))
        visit (f);
    for (const auto f : logGrid (ratio * 0.2, std::min (0.4999999, ratio * 5.0), 6000))
        visit (f);
    visit (0.4999999);
    return e;
}

Extremes analogExtremes (FilterType type, double gainDb, double q)
{
    // Analog prototype at f0 = 1 over Omega in (0, inf); evaluated through the
    // reference with fs large enough that tan() warping is negligible.
    Extremes e;
    const double fs = 1.0e12, f0 = 1.0;
    for (const auto omega : logGrid (1.0e-4, 1.0e4, 400000))
    {
        const auto m = db (std::abs (ref::analogResponse (type, omega, f0, gainDb, q, fs)));
        e.maxDb = std::max (e.maxDb, m);
        e.minDb = std::min (e.minDb, m);
    }
    return e;
}

double shelfOvershoot (const Extremes& e, double gainDb)
{
    return std::max (e.maxDb - std::max (0.0, gainDb), std::min (0.0, gainDb) - e.minDb);
}

struct TimeDomain
{
    double l1 = 0.0, stepPeak = 0.0;
    long long samples = 0;
    bool truncated = false;
};

TimeDomain timeDomain (const BiquadCoefficients& c)
{
    // Impulse and step responses together (TDF-II, double, like the DSP).
    TimeDomain t;
    double i1 = 0, i2 = 0, s1 = 0, s2 = 0;
    constexpr long long cap = 200'000'000;

    for (long long n = 0; n < cap; ++n)
    {
        const double x = n == 0 ? 1.0 : 0.0;
        const auto y = c.b0 * x + i1;
        i1 = c.b1 * x - c.a1 * y + i2;
        i2 = c.b2 * x - c.a2 * y;
        t.l1 += std::abs (y);

        const auto ys = c.b0 + s1;
        s1 = c.b1 - c.a1 * ys + s2;
        s2 = c.b2 - c.a2 * ys;
        t.stepPeak = std::max (t.stepPeak, std::abs (ys));

        t.samples = n + 1;
        if (n > 64 && std::abs (i1) + std::abs (i2) < 1.0e-16 * t.l1 && std::abs (y) < 1.0e-16 * t.l1)
            return t;
    }
    t.truncated = true;
    return t;
}

void shelves()
{
    std::printf ("\n== 1. Shelves: overshoot outside [min(0,G), max(0,G)] in dB\n"
                 "   digital: max over f0/fs in [%.3g, %.2f] (80 points) and both shelf types;\n"
                 "   analog: prototype. Gains +/-1, 3, 6, 12, 18, 24 dB.\n\n", minRatio, maxRatio);

    const double gains[] = { 1.0, 3.0, 6.0, 12.0, 18.0, 24.0 };
    std::printf ("%7s", "Q");
    for (const auto g : gains)
        std::printf ("  %7s%-5.0f", "G=+/-", g);
    std::printf ("  %12s\n", "|dig-analog|");

    for (const auto q : qGrid)
    {
        std::printf ("%7.4g", q);
        double worstDelta = 0.0;

        for (const auto g : gains)
        {
            double worst = 0.0;
            for (const auto sign : { 1.0, -1.0 })
            {
                const auto gain = sign * g;
                const auto analog = shelfOvershoot (analogExtremes (FilterType::lowShelf, gain, q), gain);
                worst = std::max (worst, analog);

                for (const auto type : { FilterType::lowShelf, FilterType::highShelf })
                    for (const auto ratio : logGrid (minRatio, maxRatio, 80))
                    {
                        const auto c = design (type, ratio, gain, q);
                        const auto o = shelfOvershoot (digitalExtremes (c, ratio), gain);
                        worst = std::max (worst, o);
                        worstDelta = std::max (worstDelta, std::abs (o - analog));
                    }
            }
            std::printf ("  %12.3f", worst);
        }
        std::printf ("  %12.2g\n", worstDelta);
    }
}

void cuts()
{
    std::printf ("\n== 2. Cuts: resonance peak (max |H|, dB) over f0/fs in [%.3g, %.2f] (300 points)\n\n",
                 minRatio, maxRatio);
    std::printf ("%7s %10s %10s %10s %14s\n", "Q", "LowCut", "HighCut", "analog", "closed form");

    for (const auto q : qGrid)
    {
        double lo = -1.0e9, hi = -1.0e9;
        for (const auto ratio : logGrid (minRatio, maxRatio, 300))
        {
            lo = std::max (lo, digitalExtremes (design (FilterType::lowCut, ratio, 0.0, q), ratio).maxDb);
            hi = std::max (hi, digitalExtremes (design (FilterType::highCut, ratio, 0.0, q), ratio).maxDb);
        }
        const auto analog = analogExtremes (FilterType::lowCut, 0.0, q).maxDb;
        // |H| peak = Q^2 / sqrt(Q^2 - 1/4) for Q > 1/sqrt(2), else 0 dB (at the passband edge).
        const auto closed = q > std::numbers::sqrt2 / 2.0 ? db (q * q / std::sqrt (q * q - 0.25)) : 0.0;
        std::printf ("%7.4g %10.3f %10.3f %10.3f %14.3f\n", q, lo, hi, analog, closed);
    }
}

void stability()
{
    std::printf ("\n== 3. Stability: all types, Q in [0.1, 18], G in {-24, 0, +24}, f0/fs in [%.3g, %.2f] (400 points)\n\n",
                 minRatio, maxRatio);
    std::printf ("%-10s %8s %10s %14s %16s\n", "type", "designs", "unstable", "non-finite", "max pole radius");

    for (const auto type : ref::allTypes)
    {
        long long designs = 0, unstable = 0, nonFinite = 0;
        double maxRadius = 0.0;

        for (const auto q : logGrid (0.1, 18.0, 40))
            for (const auto g : { -24.0, 0.0, 24.0 })
                for (const auto ratio : logGrid (minRatio, maxRatio, 400))
                {
                    const auto c = design (type, ratio, g, q);
                    ++designs;
                    const bool finite = std::isfinite (c.b0) && std::isfinite (c.b1) && std::isfinite (c.b2)
                                     && std::isfinite (c.a1) && std::isfinite (c.a2);
                    nonFinite += finite ? 0 : 1;
                    const auto r = ref::poleRadius (c);
                    unstable += (labq8::dsp::isStable (c) && r < 1.0) ? 0 : 1;
                    maxRadius = std::max (maxRadius, r);
                }

        std::printf ("%-10s %8lld %10lld %14lld %16.12f\n", ref::name (type), designs, unstable, nonFinite, maxRadius);
    }
}

void headroom()
{
    std::printf ("\n== 4. Headroom: L1 norm of the impulse response = largest |y| for any |x| <= 1 (dB),\n"
                 "   and step-response peak (dB). Worst over f0/fs in [%.3g, %.2f] (60 points).\n\n", minRatio, maxRatio);

    struct Row
    {
        const char* label;
        FilterType type;
        double gain, q;
    };

    // Each shelf/cut at Q 0.7071 (no overshoot), at the proposed limit and at
    // the E1b maximum, so the share due to Q is visible.
    const Row rows[] = {
        { "LowShelf  +24 dB Q 0.7071", FilterType::lowShelf, 24.0, 0.7071 },
        { "LowShelf  +24 dB Q 1", FilterType::lowShelf, 24.0, 1.0 },
        { "LowShelf  +24 dB Q 18 (E1b)", FilterType::lowShelf, 24.0, 18.0 },
        { "LowShelf  -24 dB Q 0.7071", FilterType::lowShelf, -24.0, 0.7071 },
        { "LowShelf  -24 dB Q 1", FilterType::lowShelf, -24.0, 1.0 },
        { "LowShelf  -24 dB Q 18 (E1b)", FilterType::lowShelf, -24.0, 18.0 },
        { "HighShelf +24 dB Q 0.7071", FilterType::highShelf, 24.0, 0.7071 },
        { "HighShelf +24 dB Q 1", FilterType::highShelf, 24.0, 1.0 },
        { "HighShelf +24 dB Q 18 (E1b)", FilterType::highShelf, 24.0, 18.0 },
        { "HighShelf -24 dB Q 0.7071", FilterType::highShelf, -24.0, 0.7071 },
        { "HighShelf -24 dB Q 1", FilterType::highShelf, -24.0, 1.0 },
        { "HighShelf -24 dB Q 18 (E1b)", FilterType::highShelf, -24.0, 18.0 },
        { "LowCut    Q 0.7071", FilterType::lowCut, 0.0, 0.7071 },
        { "LowCut    Q 2", FilterType::lowCut, 0.0, 2.0 },
        { "LowCut    Q 18 (E1b)", FilterType::lowCut, 0.0, 18.0 },
        { "HighCut   Q 0.7071", FilterType::highCut, 0.0, 0.7071 },
        { "HighCut   Q 2", FilterType::highCut, 0.0, 2.0 },
        { "HighCut   Q 18 (E1b)", FilterType::highCut, 0.0, 18.0 },
        { "Bell      +24 dB Q 18", FilterType::bell, 24.0, 18.0 },
        { "Notch     Q 18", FilterType::notch, 0.0, 18.0 },
    };

    const auto ratios = logGrid (minRatio, maxRatio, 60);

    std::printf ("%-28s %10s %10s %12s %10s\n", "design", "L1 (dB)", "step (dB)", "at f0/fs", "samples");
    for (const auto& row : rows)
    {
        TimeDomain worst;
        double at = 0.0;
        bool truncated = false;
        for (const auto ratio : ratios)
        {
            const auto t = timeDomain (design (row.type, ratio, row.gain, row.q));
            truncated = truncated || t.truncated;
            if (t.l1 > worst.l1)
            {
                worst.l1 = t.l1;
                worst.samples = t.samples;
                at = ratio;
            }
            worst.stepPeak = std::max (worst.stepPeak, t.stepPeak);
        }
        std::printf ("%-28s %10.2f %10.2f %12.4g %10lld%s\n", row.label, db (worst.l1), db (worst.stepPeak), at,
                     worst.samples, truncated ? "  (truncated)" : "");
    }
}
} // namespace

int main()
{
    std::printf ("labq8_sweep: shelf and cut response sweep (E1b.1)\n");
    shelves();
    cuts();
    stability();
    headroom();
    return 0;
}
