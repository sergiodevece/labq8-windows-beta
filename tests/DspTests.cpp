// EQ Core tests. Tolerances (see docs/ARCHITECTURE.md, "Test tolerances"):
//   kExact        bit-exact, for paths that must not touch the signal
//   kDesignTolDb  1e-4 dB: double-precision design vs analytic prototype;
//                 observed errors are ~1e-12 dB, so this only flags real bugs
//   kMeasureTolDb 0.01 dB: sine measured through float32 I/O; float rounding
//                 contributes < 1e-5 dB, and 0.01 dB is ~10x below the
//                 smallest audible EQ difference (~0.1 dB)

// Exact float comparison is intentional: bit-exactness is a tested property.
#pragma clang diagnostic ignored "-Wfloat-equal"

#include "TestHarness.h"

#include "dsp/BandLimits.h"
#include "dsp/BellBand.h"
#include "dsp/BiquadDesign.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>
#include <random>
#include <utility>
#include <vector>

using namespace labq8::dsp;
using labq8::test::str;

namespace
{
constexpr double kDesignTolDb  = 1.0e-4;
constexpr double kMeasureTolDb = 0.01;
constexpr std::array<double, 3> kSampleRates { 44100.0, 48000.0, 96000.0 };

using Signal = std::vector<float>;

Signal makeNoise (size_t n, std::uint32_t seed, float amplitude = 0.5f)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-amplitude, amplitude);
    Signal s (n);
    for (auto& v : s)
        v = dist (rng);
    return s;
}

Signal makeSine (double frequency, double sampleRate, size_t n, double amplitude = 0.25)
{
    Signal s (n);
    for (size_t i = 0; i < n; ++i)
        s[i] = static_cast<float> (amplitude * std::sin (2.0 * std::numbers::pi * frequency * static_cast<double> (i) / sampleRate));
    return s;
}

size_t samples (double seconds, double sampleRate)
{
    return static_cast<size_t> (std::llround (seconds * sampleRate));
}

void processMono (BellBand& band, Signal& x, size_t begin, size_t end, int blockSize)
{
    for (auto pos = begin; pos < end;)
    {
        const auto n = std::min (static_cast<size_t> (blockSize), end - pos);
        float* channels[] = { x.data() + pos };
        band.process (channels, 1, static_cast<int> (n));
        pos += n;
    }
}

void processMono (BellBand& band, Signal& x, int blockSize = 512)
{
    processMono (band, x, 0, x.size(), blockSize);
}

BellBand makeBand (double sampleRate, const BellSettings& settings, int numChannels = 1)
{
    BellBand band;
    band.setTargets (settings);
    band.prepare (sampleRate, numChannels);
    return band;
}

// Independent reference: analog prototype evaluated at the bilinear-warped
// frequency. Shares no code with designBell().
double analogBellDb (double f, double f0, double gainDb, double q, double fs)
{
    const auto A = std::pow (10.0, gainDb / 40.0);
    const auto omega = std::tan (std::numbers::pi * f / fs) / std::tan (std::numbers::pi * f0 / fs);
    const std::complex<double> s (0.0, omega);
    const auto h = (s * s + s * (A / q) + 1.0) / (s * s + s / (A * q) + 1.0);
    return 20.0 * std::log10 (std::abs (h));
}

double toDb (double linear) { return 20.0 * std::log10 (linear); }

// Least-squares fit of a*sin + b*cos at a known frequency: exact for a pure
// sinusoid regardless of window length or phase.
double fitAmplitude (const Signal& y, size_t begin, size_t end, double f, double fs)
{
    double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;

    for (auto i = begin; i < end; ++i)
    {
        const auto ph = 2.0 * std::numbers::pi * f * static_cast<double> (i) / fs;
        const auto s = std::sin (ph), c = std::cos (ph);
        ss += s * s; cc += c * c; sc += s * c;
        ys += y[i] * s; yc += y[i] * c;
    }

    const auto det = ss * cc - sc * sc;
    const auto a = (ys * cc - yc * sc) / det;
    const auto b = (yc * ss - ys * sc) / det;
    return std::hypot (a, b);
}

// Time for the slowest pole to decay by e^-12. Analog poles have Q = A*Q:
// underdamped (Qp >= 0.5) decay at w0/(2Qp); overdamped ones are real and
// the slow one decays at w0*(1/(2Qp) - sqrt(1/(4Qp^2) - 1)), roughly w0*Qp.
double settleSeconds (double f0, double gainDb, double q)
{
    const auto poleQ = std::pow (10.0, gainDb / 40.0) * q;
    const auto w0 = 2.0 * std::numbers::pi * f0;
    const auto k = 1.0 / (2.0 * poleQ);
    const auto slowestRate = poleQ >= 0.5 ? w0 * k : w0 * (k - std::sqrt (k * k - 1.0));
    return std::max (0.25, 12.0 / slowestRate);
}

// Measures the steady-state gain (dB) of a band at frequency f.
double measureGainDb (BellBand& band, double f, double settle)
{
    const auto fs = band.getSampleRate();
    const auto measure = std::max (0.2, 20.0 / f);
    auto x = makeSine (f, fs, samples (settle + measure, fs));
    auto y = x;
    processMono (band, y, 512);
    const auto begin = samples (settle, fs);
    return toDb (fitAmplitude (y, begin, y.size(), f, fs) / fitAmplitude (x, begin, x.size(), f, fs));
}

bool allFinite (const Signal& s)
{
    return std::all_of (s.begin(), s.end(), [] (float v) { return std::isfinite (v); });
}

float peak (const Signal& s, size_t begin = 0, size_t end = std::numeric_limits<size_t>::max())
{
    float p = 0.0f;
    for (auto i = begin; i < std::min (end, s.size()); ++i)
        p = std::max (p, std::abs (s[i]));
    return p;
}

// Processes x with an arbitrary block-size sequence and parameter changes at
// absolute sample positions.
using Event = std::pair<size_t, BellSettings>;

template <typename NextBlock>
Signal processScheduled (double fs, const BellSettings& initial, const Signal& input,
                         const std::vector<Event>& events, NextBlock nextBlock)
{
    auto band = makeBand (fs, initial);
    auto y = input;
    size_t pos = 0, nextEvent = 0;

    while (pos < y.size())
    {
        while (nextEvent < events.size() && events[nextEvent].first <= pos)
            band.setTargets (events[nextEvent++].second);

        auto n = static_cast<size_t> (nextBlock());
        n = std::min (n, y.size() - pos);
        if (nextEvent < events.size())
            n = std::min (n, events[nextEvent].first - pos);

        processMono (band, y, pos, pos + n, static_cast<int> (n));
        pos += n;
    }

    return y;
}

// Unsmoothed TDF-II biquad used as a "what a click looks like" control.
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

// Largest 4th-order difference in [begin, end): suppresses a low sine by
// (2 sin(w/2))^4 but keeps step discontinuities, so it isolates clicks.
double maxFourthDifference (const Signal& y, size_t begin, size_t end)
{
    double m = 0.0;
    for (auto i = std::max<size_t> (begin, 4); i < std::min (end, y.size()); ++i)
    {
        const double d = y[i] - 4.0 * y[i - 1] + 6.0 * y[i - 2] - 4.0 * y[i - 3] + y[i - 4];
        m = std::max (m, std::abs (d));
    }
    return m;
}
} // namespace

//==============================================================================
LQ_TEST ("dsp: 0 dB gain is a bit-exact identity")
{
    for (const auto fs : kSampleRates)
        for (const auto f0 : { 20.0, 1000.0, 19000.0 })
            for (const auto q : { 0.1, 1.0, 18.0 })
            {
                auto band = makeBand (fs, { f0, 0.0, q, false });
                const auto x = makeNoise (4096, 1);
                auto y = x;
                processMono (band, y, 256);

                LQ_CHECK (y == x, str ("output differs from input fs=", fs, " f0=", f0, " q=", q));
                LQ_CHECK (band.isIdle(), "band at 0 dB should be idle");
            }
}

LQ_TEST ("dsp: band bypass is bit-exact once settled, and un-bypass restores the curve")
{
    for (const auto fs : kSampleRates)
    {
        // Bypassed from the start: never touches the signal.
        {
            auto band = makeBand (fs, { 1000.0, 12.0, 2.0, true });
            const auto x = makeNoise (samples (0.5, fs), 2);
            auto y = x;
            processMono (band, y);
            LQ_CHECK (y == x, str ("bypassed band altered the signal fs=", fs));
        }

        // Toggled while running.
        auto band = makeBand (fs, { 1000.0, 12.0, 2.0, false });
        const auto x = makeNoise (samples (2.25, fs), 3);
        auto y = x;
        const auto toggle = samples (0.25, fs);
        processMono (band, y, 0, toggle, 512);
        band.setTargets ({ 1000.0, 12.0, 2.0, true });
        processMono (band, y, toggle, y.size(), 512);

        LQ_CHECK (band.isIdle(), str ("band not idle 2 s after bypass fs=", fs));

        const auto tail = y.size() - samples (0.5, fs);
        LQ_CHECK (std::equal (y.begin() + (long) tail, y.end(), x.begin() + (long) tail),
                  str ("bypassed tail not bit-exact fs=", fs));

        band.setTargets ({ 1000.0, 12.0, 2.0, false });
        LQ_CHECK (! band.isIdle(), "un-bypass must wake the band");
        const auto g = measureGainDb (band, 1000.0, settleSeconds (1000.0, 12.0, 2.0));
        LQ_CHECK (std::abs (g - 12.0) < kMeasureTolDb, str ("gain after un-bypass ", g, " dB fs=", fs));
    }
}

LQ_TEST ("dsp: Bell coefficients match the analog prototype and are stable")
{
    for (const auto fs : kSampleRates)
        for (const auto f0 : { 20.0, 100.0, 1000.0, 10000.0, 20000.0 })
            for (const auto q : { 0.1, 0.707, 1.0, 4.0, 18.0 })
                for (const auto g : { -24.0, -6.0, 6.0, 24.0 })
                {
                    const auto c = designBell (fs, f0, g, q);
                    LQ_CHECK (isStable (c), str ("unstable fs=", fs, " f0=", f0, " q=", q, " g=", g));

                    const auto centre = toDb (magnitudeAt (c, f0, fs));
                    LQ_CHECK (std::abs (centre - g) < kDesignTolDb,
                              str ("centre gain ", centre, " expected ", g, " fs=", fs, " f0=", f0, " q=", q));

                    for (int k = 0; k < 40; ++k)
                    {
                        const auto f = 10.0 * std::pow (0.49 * fs / 10.0, k / 39.0);
                        const auto got = toDb (magnitudeAt (c, f, fs));
                        const auto expected = analogBellDb (f, f0, g, q, fs);
                        LQ_CHECK (std::abs (got - expected) < kDesignTolDb,
                                  str ("|H(", f, ")| = ", got, " expected ", expected, " fs=", fs, " f0=", f0, " q=", q, " g=", g));
                    }
                }
}

LQ_TEST ("dsp: measured sine response matches the design (float I/O)")
{
    struct Case { double f0, g, q; };
    const Case cases[] = { { 1000.0, 12.0, 1.0 }, { 100.0, -9.0, 0.707 }, { 8000.0, 6.0, 4.0 },
                           { 20.0, 24.0, 18.0 }, { 20000.0, -24.0, 0.1 } };

    for (const auto fs : kSampleRates)
        for (const auto& c : cases)
            for (const auto f : { c.f0 * 0.5, c.f0, c.f0 * 2.0 })
            {
                if (f < 10.0 || f > 0.45 * fs)
                    continue;

                auto band = makeBand (fs, { c.f0, c.g, c.q, false });
                const auto got = measureGainDb (band, f, settleSeconds (c.f0, c.g, c.q));
                const auto expected = analogBellDb (f, c.f0, c.g, c.q, fs);
                LQ_CHECK (std::abs (got - expected) < kMeasureTolDb,
                          str ("measured ", got, " dB expected ", expected, " at ", f, " Hz, fs=", fs,
                               " f0=", c.f0, " g=", c.g, " q=", c.q));
            }
}

LQ_TEST ("dsp: frequency and Q changes ramp smoothly to the new response")
{
    for (const auto fs : kSampleRates)
    {
        auto band = makeBand (fs, { 1000.0, 12.0, 1.0, false });
        auto x = makeNoise (samples (0.1, fs), 4);
        processMono (band, x);

        band.setTargets ({ 4000.0, 12.0, 4.0, false });

        // Walk through the ramp one update interval at a time.
        const auto expectedTicks = std::lround (BellBand::rampTimeSeconds * fs / BellBand::updateIntervalSamples);
        double lastF = band.getCurrentFrequencyHz(), lastQ = band.getCurrentQ();
        bool monotonic = true;
        long ticksToArrive = -1;

        for (long t = 0; t < expectedTicks + 10; ++t)
        {
            auto block = makeNoise (BellBand::updateIntervalSamples, 5 + (std::uint32_t) t);
            processMono (band, block, BellBand::updateIntervalSamples);
            const auto f = band.getCurrentFrequencyHz(), q = band.getCurrentQ();
            monotonic = monotonic && f >= lastF && q >= lastQ;
            lastF = f; lastQ = q;
            if (ticksToArrive < 0 && std::abs (f - 4000.0) < 1e-9 * 4000.0 && std::abs (q - 4.0) < 1e-9 * 4.0)
                ticksToArrive = t + 1;
        }

        LQ_CHECK (monotonic, str ("ramp not monotonic fs=", fs));
        LQ_CHECK (ticksToArrive == expectedTicks,
                  str ("ramp took ", ticksToArrive, " updates, expected ", expectedTicks, " fs=", fs));

        const auto& c = band.getCoefficients();
        LQ_CHECK (std::abs (toDb (magnitudeAt (c, 4000.0, fs)) - 12.0) < kDesignTolDb, "new centre gain");
        LQ_CHECK (std::abs (toDb (magnitudeAt (c, 1000.0, fs)) - analogBellDb (1000.0, 4000.0, 12.0, 4.0, fs)) < kDesignTolDb,
                  "old centre now follows the new curve");

        const auto g = measureGainDb (band, 4000.0, settleSeconds (4000.0, 12.0, 4.0));
        LQ_CHECK (std::abs (g - 12.0) < kMeasureTolDb, str ("measured ", g, " dB at new centre fs=", fs));
    }
}

LQ_TEST ("dsp: stereo channels are independent and match mono processing")
{
    for (const auto fs : kSampleRates)
    {
        const BellSettings s { 2500.0, -6.0, 3.0, false };
        const auto left = makeNoise (samples (0.5, fs), 10);
        const auto right = makeNoise (samples (0.5, fs), 11);

        auto stereo = makeBand (fs, s, 2);
        auto l = left, r = right;
        for (size_t pos = 0; pos < l.size(); pos += 300)
        {
            const auto n = static_cast<int> (std::min<size_t> (300, l.size() - pos));
            float* channels[] = { l.data() + pos, r.data() + pos };
            stereo.process (channels, 2, n);
        }

        auto monoL = makeBand (fs, s), monoR = makeBand (fs, s);
        auto ml = left, mr = right;
        processMono (monoL, ml, 300);
        processMono (monoR, mr, 300);

        LQ_CHECK (l == ml, str ("left differs from mono fs=", fs));
        LQ_CHECK (r == mr, str ("right differs from mono fs=", fs));

        // Silence on one side stays exact silence.
        auto loud = left;
        Signal silent (left.size(), 0.0f);
        auto band = makeBand (fs, s, 2);
        float* channels[] = { loud.data(), silent.data() };
        band.process (channels, 2, static_cast<int> (loud.size()));
        LQ_CHECK (peak (silent) == 0.0f, "crosstalk into a silent channel");
    }
}

LQ_TEST ("dsp: limits and invalid values keep the band stable and finite")
{
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();

    for (const auto fs : kSampleRates)
    {
        // Extremes of the valid range: impulse response must decay.
        for (const auto f0 : { limits::minFrequencyHz, limits::maxFrequencyHz })
            for (const auto q : { limits::minQ, limits::maxQ })
                for (const auto g : { limits::minGainDb, limits::maxGainDb })
                {
                    auto band = makeBand (fs, { f0, g, q, false });
                    LQ_CHECK (isStable (band.getCoefficients()), "edge setting unstable");

                    Signal h (samples (2.0 * settleSeconds (f0, g, q), fs), 0.0f);
                    h[0] = 1.0f;
                    processMono (band, h);
                    const auto p = peak (h);
                    const auto tail = peak (h, h.size() - h.size() / 10);
                    LQ_CHECK (allFinite (h), "non-finite impulse response");
                    LQ_CHECK (tail < 1.0e-5f * p, str ("impulse response not decaying: tail ", tail, " peak ", p,
                                                       " fs=", fs, " f0=", f0, " q=", q, " g=", g));
                }

        // Garbage settings are clamped, never propagated.
        const BellSettings garbage[] = { { nan, nan, nan, false }, { inf, inf, inf, false },
                                         { -inf, -inf, -inf, false }, { -100.0, 1.0e9, -5.0, false },
                                         { 1.0e12, -1.0e9, 1.0e9, false }, { 0.0, 0.0, 0.0, false } };

        for (const auto& s : garbage)
        {
            auto band = makeBand (fs, s);
            const auto f = band.getCurrentFrequencyHz();
            LQ_CHECK (f >= limits::minFrequencyHz * 0.999999 && f <= limits::maxFrequencyHz * 1.000001, str ("freq ", f));
            LQ_CHECK (band.getCurrentQ() >= limits::minQ * 0.999999 && band.getCurrentQ() <= limits::maxQ * 1.000001, "q");
            LQ_CHECK (band.getCurrentGainDb() >= limits::minGainDb && band.getCurrentGainDb() <= limits::maxGainDb, "gain");
            LQ_CHECK (isStable (band.getCoefficients()), "garbage setting unstable");

            auto y = makeNoise (samples (0.5, fs), 12);
            processMono (band, y);
            LQ_CHECK (allFinite (y), "garbage setting produced non-finite output");
        }
    }

    // Nyquist guard: below 40.8 kHz the centre is limited to 0.49 * fs.
    {
        auto band = makeBand (32000.0, { 20000.0, 6.0, 1.0, false });
        LQ_CHECK (std::abs (band.getCurrentFrequencyHz() - 0.49 * 32000.0) < 1e-6, "Nyquist clamp at 32 kHz");
    }

    // Invalid sample rates fall back to a safe default.
    for (const auto fs : { 0.0, -48000.0, nan, inf, 1.0e9 })
    {
        auto band = makeBand (fs, { 1000.0, 6.0, 1.0, false });
        LQ_CHECK (band.getSampleRate() == limits::fallbackSampleRate, str ("invalid fs ", fs, " not rejected"));
        auto y = makeNoise (4096, 13);
        processMono (band, y);
        LQ_CHECK (allFinite (y), "invalid fs produced non-finite output");
    }
}

LQ_TEST ("dsp: output is bit-identical for any host block size")
{
    for (const auto fs : kSampleRates)
    {
        const BellSettings a { 700.0, 9.0, 2.0, false };
        const BellSettings b { 5000.0, -12.0, 0.5, false };
        auto c = b;
        c.bypassed = true;
        const std::vector<Event> events { { samples (0.37, fs), b }, { samples (0.6, fs), c } };
        const auto x = makeNoise (samples (1.0, fs), 20);

        const auto reference = processScheduled (fs, a, x, events, [] { return 512; });

        for (const int size : { 1, 7, 16, 64, 333, 4096 })
        {
            const auto y = processScheduled (fs, a, x, events, [size] { return size; });
            LQ_CHECK (y == reference, str ("block size ", size, " differs fs=", fs));
        }

        std::mt19937 rng (21);
        std::uniform_int_distribution<int> dist (1, 2048);
        const auto y = processScheduled (fs, a, x, events, [&] { return dist (rng); });
        LQ_CHECK (y == reference, str ("random block sizes differ fs=", fs));
    }
}

LQ_TEST ("dsp: non-finite input samples are contained")
{
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto inf = std::numeric_limits<float>::infinity();

    for (const auto fs : kSampleRates)
    {
        auto x = makeNoise (samples (0.5, fs), 30);
        auto clean = x;
        const std::pair<size_t, float> bad[] = { { 100, nan }, { 2000, inf }, { 2001, -inf }, { x.size() - 1, nan } };

        for (const auto& [i, v] : bad)
        {
            x[i] = v;
            clean[i] = 0.0f;
        }

        auto bandA = makeBand (fs, { 1000.0, 12.0, 1.0, false });
        auto bandB = makeBand (fs, { 1000.0, 12.0, 1.0, false });
        processMono (bandA, x);
        processMono (bandB, clean);

        LQ_CHECK (allFinite (x), str ("non-finite output fs=", fs));
        LQ_CHECK (x == clean, "non-finite samples must behave exactly like zeros");
    }
}

LQ_TEST ("dsp: fast random automation stays finite, bounded and recovers")
{
    for (const auto fs : kSampleRates)
    {
        auto band = makeBand (fs, {});
        std::mt19937 rng (40);
        std::uniform_real_distribution<double> unit (0.0, 1.0);
        auto y = makeNoise (samples (3.0, fs), 41);

        for (size_t pos = 0; pos < y.size(); pos += 32)
        {
            BellSettings s;
            s.frequencyHz = limits::minFrequencyHz * std::pow (limits::maxFrequencyHz / limits::minFrequencyHz, unit (rng));
            s.q           = limits::minQ * std::pow (limits::maxQ / limits::minQ, unit (rng));
            s.gainDb      = limits::minGainDb + (limits::maxGainDb - limits::minGainDb) * unit (rng);
            s.bypassed    = unit (rng) < 0.1;
            band.setTargets (s);
            processMono (band, y, pos, std::min (pos + 32, y.size()), 32);
        }

        LQ_CHECK (allFinite (y), str ("non-finite output under automation fs=", fs));
        // 0.5 peak input, max +24 dB (x15.85): anything near 100 means instability.
        LQ_CHECK (peak (y) < 100.0f, str ("output peak ", peak (y), " under automation fs=", fs));

        band.setTargets ({ 1000.0, 0.0, 1.0, true });
        const auto x = makeNoise (samples (10.0, fs), 42);
        auto z = x;
        processMono (band, z);
        LQ_CHECK (band.isIdle(), str ("band did not return to idle fs=", fs));
        const auto tail = z.size() - samples (1.0, fs);
        LQ_CHECK (std::equal (z.begin() + (long) tail, z.end(), x.begin() + (long) tail), "recovered output not bit-exact");
    }
}

LQ_TEST ("dsp: gain steps and bypass toggles are click-free")
{
    for (const auto fs : kSampleRates)
    {
        const double tone = 200.0;
        const auto x = makeSine (tone, fs, samples (1.0, fs), 0.25);

        // Changes land on sine peaks, where an unsmoothed switch is worst.
        auto peakAt = [&] (int period) { return samples ((period + 0.25) / tone, fs); };
        const BellSettings start { tone, 0.0, 1.0, false };
        const std::vector<Event> events { { peakAt (50), { tone, 24.0, 1.0, false } },
                                          { peakAt (100), { tone, -24.0, 1.0, false } },
                                          { peakAt (150), { tone, -24.0, 1.0, true } } };

        const auto smoothed = processScheduled (fs, start, x, events, [] { return 512; });

        // Control: same changes applied instantly.
        RawBiquad raw { designBell (fs, tone, 0.0, 1.0) };
        Signal control (x.size());
        size_t e = 0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            if (e < events.size() && events[e].first == i)
            {
                const auto& s = events[e++].second;
                raw.c = designBell (fs, s.frequencyHz, s.bypassed ? 0.0 : s.gainDb, s.q);
            }
            control[i] = raw.process (x[i]);
        }

        const auto window = samples (0.1, fs);
        const auto steady = maxFourthDifference (smoothed, samples (0.05, fs), events[0].first - 1);

        for (const auto& [at, s] : events)
        {
            const auto clickSmoothed = maxFourthDifference (smoothed, at, at + window);
            const auto clickControl = maxFourthDifference (control, at, at + window);

            LQ_CHECK (clickControl > 10.0 * clickSmoothed,
                      str ("smoothing gives < 20 dB click reduction: smoothed ", clickSmoothed,
                           " control ", clickControl, " at ", at, " fs=", fs));
            LQ_CHECK (clickControl > 10.0 * steady, "control should click (test sensitivity)");
        }
    }
}
