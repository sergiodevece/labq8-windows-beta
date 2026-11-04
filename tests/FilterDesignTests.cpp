// E1b filter designs (A, B, C, D): every topology against its analog
// prototype, the meaning of frequency / gain / Q, stability at the limits and
// near Nyquist, at 44.1, 48 and 96 kHz.

#pragma clang diagnostic ignored "-Wfloat-equal" // bit-exactness is a tested property

#include "FilterReference.h"
#include "TestHarness.h"
#include "TestSignals.h"

#include "dsp/BandLimits.h"
#include "dsp/EqBand.h"

#include <limits>

using namespace labq8::dsp;
using namespace labq8::test::signals;
using namespace labq8::test::reference;
using labq8::test::str;

namespace
{
double db (double linear) { return 20.0 * std::log10 (linear); }

// Above -60 dB compare in dB; deeper (stop bands, notch centre) compare the
// linear magnitude absolutely, where dB would amplify harmless rounding.
bool responseMatches (double got, double expected)
{
    if (expected > 1.0e-3)
        return std::abs (db (got) - db (expected)) < designTolDb;
    return std::abs (got - expected) < 1.0e-8;
}

bool sameCoefficients (const BiquadCoefficients& a, const BiquadCoefficients& b)
{
    return a.b0 == b.b0 && a.b1 == b.b1 && a.b2 == b.b2 && a.a1 == b.a1 && a.a2 == b.a2;
}

// Steady-state gain of an EqBand at f, through float32 I/O.
double measureBandDb (double fs, const BandSettings& s, double f)
{
    EqBand band;
    band.setTargets (s);
    band.prepare (fs, 1);
    const auto settle = std::max (0.25, decaySamples (band.getCoefficients(), 1.0e-7) / fs);
    const auto measure = std::max (0.2, 20.0 / f);
    auto x = makeSine (f, fs, samples (settle + measure, fs));
    auto y = x;
    for (size_t pos = 0; pos < y.size(); pos += 512)
    {
        float* ch[] = { y.data() + pos };
        band.process (ch, 1, (int) std::min<size_t> (512, y.size() - pos));
    }
    const auto begin = samples (settle, fs);
    return db (fitAmplitude (y, begin, y.size(), f, fs) / fitAmplitude (x, begin, x.size(), f, fs));
}
} // namespace

LQ_TEST ("filter: every topology matches its analog prototype (A, D)")
{
    for (const auto fs : sampleRates)
        for (const auto type : allTypes)
            for (const auto f0 : { 20.0, 100.0, 1000.0, 10000.0, 20000.0 })
                for (const auto q : { 0.1, 0.7071, 1.0, 4.0, 18.0 })
                    for (const auto g : { -24.0, -6.0, 6.0, 24.0 })
                    {
                        const auto c = designFilter (type, fs, f0, g, q);
                        LQ_CHECK (isStable (c), str (name (type), " unstable fs=", fs, " f0=", f0, " q=", q, " g=", g));

                        bool ok = true;
                        double worst = 0.0, worstF = 0.0;
                        for (int k = 0; k < 48; ++k)
                        {
                            const auto f = 5.0 * std::pow (0.495 * fs / 5.0, k / 47.0);
                            const auto got = magnitudeAt (c, f, fs);
                            const auto expected = std::abs (analogResponse (type, f, f0, g, q, fs));
                            if (! responseMatches (got, expected))
                            {
                                ok = false;
                                worst = got;
                                worstF = f;
                            }
                        }
                        LQ_CHECK (ok, str (name (type), " response off at ", worstF, " Hz (", worst, ") fs=", fs, " f0=", f0, " q=", q, " g=", g));

                        if (! usesGain (type)) // gain must not change cuts or notch at all
                            LQ_CHECK (sameCoefficients (c, designFilter (type, fs, f0, 0.0, q)),
                                      str (name (type), " depends on gain, f0=", f0, " q=", q, " g=", g));
                    }
}

LQ_TEST ("filter: frequency, gain and Q mean what the schema says (B)")
{
    const auto nyq = [] (double fs) { return 0.5 * fs; };

    for (const auto fs : sampleRates)
        for (const auto f0 : { 50.0, 1000.0, 8000.0 })
            for (const auto q : { 0.5, 0.7071, 2.0, 8.0 })
            {
                for (const auto g : { -12.0, 6.0 })
                {
                    // Shelves: full gain on their side, exactly g/2 at f0 (any Q), 0 dB on the other side.
                    const auto ls = designLowShelf (fs, f0, g, q);
                    LQ_CHECK (std::abs (db (magnitudeAt (ls, 0.0, fs)) - g) < 1e-6, str ("low shelf DC fs=", fs, " f0=", f0));
                    LQ_CHECK (std::abs (db (magnitudeAt (ls, f0, fs)) - g / 2.0) < 1e-6, str ("low shelf f0 fs=", fs, " f0=", f0, " q=", q));
                    LQ_CHECK (std::abs (db (magnitudeAt (ls, nyq (fs), fs))) < 1e-6, "low shelf Nyquist");

                    const auto hs = designHighShelf (fs, f0, g, q);
                    LQ_CHECK (std::abs (db (magnitudeAt (hs, 0.0, fs))) < 1e-6, "high shelf DC");
                    LQ_CHECK (std::abs (db (magnitudeAt (hs, f0, fs)) - g / 2.0) < 1e-6, str ("high shelf f0 fs=", fs, " f0=", f0, " q=", q));
                    LQ_CHECK (std::abs (db (magnitudeAt (hs, nyq (fs), fs)) - g) < 1e-6, "high shelf Nyquist");

                    // Bell: exactly g at f0 (E0 property, repeated for completeness).
                    LQ_CHECK (std::abs (db (magnitudeAt (designBell (fs, f0, g, q), f0, fs)) - g) < 1e-6, "bell f0");
                }

                // Cuts: |H(f0)| = Q (Q = 0.7071 -> -3.01 dB, Butterworth); unity pass band; zero in the stop band edge.
                const auto lc = designLowCut (fs, f0, q);
                LQ_CHECK (std::abs (magnitudeAt (lc, f0, fs) - q) < 1e-9 * std::max (1.0, q), str ("low cut |H(f0)| != Q, fs=", fs, " f0=", f0, " q=", q));
                LQ_CHECK (magnitudeAt (lc, 0.0, fs) < 1e-12, "low cut DC");
                LQ_CHECK (std::abs (magnitudeAt (lc, nyq (fs), fs) - 1.0) < 1e-9, "low cut Nyquist");

                const auto hc = designHighCut (fs, f0, q);
                LQ_CHECK (std::abs (magnitudeAt (hc, f0, fs) - q) < 1e-9 * std::max (1.0, q), str ("high cut |H(f0)| != Q, fs=", fs, " f0=", f0, " q=", q));
                LQ_CHECK (std::abs (magnitudeAt (hc, 0.0, fs) - 1.0) < 1e-9, "high cut DC");
                LQ_CHECK (magnitudeAt (hc, nyq (fs), fs) < 1e-12, "high cut Nyquist");

                // Notch: zero at f0, unity at DC and Nyquist, -3.01 dB bandwidth = f0 / Q
                // (analog edges Omega = sqrt(1 + 1/(4Q^2)) +- 1/(2Q), mapped back through the warp).
                const auto n = designNotch (fs, f0, q);
                LQ_CHECK (magnitudeAt (n, f0, fs) < 1e-9, str ("notch depth fs=", fs, " f0=", f0, " q=", q));
                LQ_CHECK (std::abs (magnitudeAt (n, 0.0, fs) - 1.0) < 1e-9 && std::abs (magnitudeAt (n, nyq (fs), fs) - 1.0) < 1e-9, "notch unity");
                const auto k = std::tan (std::numbers::pi * f0 / fs);
                for (const double sign : { -1.0, 1.0 })
                {
                    const auto omega = std::sqrt (1.0 + 1.0 / (4.0 * q * q)) + sign / (2.0 * q);
                    const auto edge = fs / std::numbers::pi * std::atan (omega * k);
                    LQ_CHECK (std::abs (db (magnitudeAt (n, edge, fs)) + 3.0103) < 1e-3, str ("notch -3 dB edge at ", edge, " fs=", fs, " f0=", f0, " q=", q));
                }
            }

    // 12 dB/oct asymptotes (second order), measured far from f0 where the warp is negligible.
    const auto fs = 96000.0;
    const auto lc = designLowCut (fs, 4000.0, 0.7071);
    const auto lcSlope = db (magnitudeAt (lc, 4000.0 / 32.0, fs)) - db (magnitudeAt (lc, 4000.0 / 64.0, fs));
    LQ_CHECK (std::abs (lcSlope - 12.04) < 0.01, str ("low cut slope ", lcSlope, " dB/oct"));
    const auto hc = designHighCut (fs, 100.0, 0.7071);
    const auto hcSlope = db (magnitudeAt (hc, 100.0 * 16.0, fs)) - db (magnitudeAt (hc, 100.0 * 32.0, fs));
    LQ_CHECK (std::abs (hcSlope - 12.04) < 0.05, str ("high cut slope ", hcSlope, " dB/oct"));
    ctx.note (str ("asymptotic slopes: low cut ", lcSlope, " dB/oct, high cut ", hcSlope, " dB/oct (ideal 12.04)"));
}

LQ_TEST ("filter: measured sine response through EqBand matches the design (float I/O)")
{
    for (const auto fs : sampleRates)
        for (const auto type : allTypes)
            for (const auto f0 : { 100.0, 1000.0, 10000.0 })
            {
                const BandSettings s { f0, usesGain (type) ? 9.0 : 0.0, 2.0, false, type };
                for (const auto f : { f0 * 0.5, f0, f0 * 2.0 })
                {
                    if (f > 0.45 * fs)
                        continue;
                    // E1b.1: the band designs with the effective Q of its type (shelves: 1).
                    const auto expected = std::abs (analogResponse (type, f, f0, s.gainDb, effectiveQ (type, s.q), fs));
                    const auto got = measureBandDb (fs, s, f);

                    if (type == FilterType::notch && f == f0)
                        LQ_CHECK (got < -80.0, str ("notch measured only ", got, " dB at f0, fs=", fs));
                    else
                        LQ_CHECK (std::abs (got - db (expected)) < measureTolDb,
                                  str (name (type), " measured ", got, " dB expected ", db (expected), " at ", f, " Hz, fs=", fs, " f0=", f0));
                }
            }
}

LQ_TEST ("filter: limits, Nyquist and invalid input keep every topology stable (C)")
{
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();

    for (const auto fs : { 32000.0, 44100.0, 48000.0, 96000.0 })
        for (const auto type : allTypes)
        {
            // Edges of the valid ranges, including the 0.49 fs clamp at 32 kHz.
            for (const auto f0 : { limits::minFrequencyHz, limits::maxFrequencyHz, 0.49 * fs })
                for (const auto q : { limits::minQ, limits::maxQ })
                    for (const auto g : { limits::minGainDb, limits::maxGainDb })
                    {
                        EqBand band;
                        band.setTargets ({ f0, g, q, false, type });
                        band.prepare (fs, 1);
                        const auto& c = band.getCoefficients();
                        LQ_CHECK (isStable (c), str (name (type), " unstable at the edge fs=", fs, " f0=", f0, " q=", q, " g=", g));

                        Signal h ((size_t) std::min (decaySamples (c), 30.0 * fs), 0.0f);
                        h[0] = 1.0f;
                        for (size_t pos = 0; pos < h.size(); pos += 4096)
                        {
                            float* ch[] = { h.data() + pos };
                            band.process (ch, 1, (int) std::min<size_t> (4096, h.size() - pos));
                        }
                        const auto p = peak (h);
                        const auto tail = peak (h, h.size() - h.size() / 20);
                        LQ_CHECK (allFinite (h), str (name (type), " non-finite impulse response"));
                        LQ_CHECK (tail <= 1.0e-5f * std::max (p, 1.0f),
                                  str (name (type), " impulse not decaying: tail ", tail, " peak ", p, " fs=", fs, " f0=", f0, " q=", q, " g=", g));
                    }

            // Garbage in: clamped, never propagated.
            for (const auto& bad : { BandSettings { nan, nan, nan, false, type }, BandSettings { inf, -inf, inf, false, type },
                                     BandSettings { -5.0, 1.0e9, 0.0, false, type }, BandSettings { 1.0e12, -1.0e9, -3.0, false, type } })
            {
                EqBand band;
                band.setTargets (bad);
                band.prepare (fs, 1);
                LQ_CHECK (isStable (band.getCoefficients()), str (name (type), " garbage setting unstable"));
                auto y = makeNoise (4096, 7);
                float* ch[] = { y.data() };
                band.process (ch, 1, (int) y.size());
                LQ_CHECK (allFinite (y), str (name (type), " garbage setting produced non-finite output"));
            }
        }

    // Invalid type indices fall back to Bell.
    LQ_CHECK (filterTypeFromIndex (-1) == FilterType::bell && filterTypeFromIndex (6) == FilterType::bell
                  && filterTypeFromIndex (3) == FilterType::lowCut, "type index fallback");
}

LQ_TEST ("filter: shelf overshoot and cut resonance across the Q range (measured, informative)")
{
    // Not a pass/fail limit: documents what Q in [0.1, 18] does to shelves and cuts.
    const auto fs = 48000.0;
    for (const auto q : { 0.1, 0.5, 0.7071, 1.0, 2.0, 4.0, 18.0 })
    {
        double shelfPeak = -1e9, shelfDip = 1e9;
        const auto ls = designLowShelf (fs, 1000.0, 12.0, q);
        for (int k = 0; k < 2000; ++k)
        {
            const auto f = 10.0 * std::pow (2000.0, k / 1999.0);
            const auto m = db (magnitudeAt (ls, f, fs));
            shelfPeak = std::max (shelfPeak, m);
            shelfDip = std::min (shelfDip, m);
        }
        const auto cutPeak = db (q > 0.7071 ? q / std::sqrt (1.0 - 1.0 / (4.0 * q * q)) : 1.0);
        ctx.note (str ("Q=", q, ": +12 dB low shelf spans ", shelfDip, " .. ", shelfPeak, " dB; cut resonance peak ", cutPeak, " dB, |H(f0)| ", db (q), " dB"));
        LQ_CHECK (std::isfinite (shelfPeak) && std::isfinite (shelfDip), "finite");
    }
}
