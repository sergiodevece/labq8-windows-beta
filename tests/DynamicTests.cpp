// E2a dynamic Bell prototype tests (numbers refer to the E2a test list):
// detector accuracy and rejection (1, 2), transfer law (3, 5), ballistics
// (4), transients (6), low level and silence (7), extreme and non-finite
// input (8), fast automation (9), block-size independence (10), modulation
// clicks and time-varying stability (11), CPU (12), transparency when off
// (13), stereo link.

#pragma clang diagnostic ignored "-Wfloat-equal" // bit-exactness is a tested property

#include "AllocationGuard.h"
#include "FilterReference.h"
#include "TestHarness.h"
#include "TestSignals.h"

#include "dsp/DynamicBellBand.h"
#include "dsp/DynamicDetector.h"
#include "dsp/DynamicGainLaw.h"
#include "dsp/EqBand.h"

#include <chrono>
#include <limits>
#include <random>

using namespace labq8::dsp;
using namespace labq8::test::signals;
using labq8::test::ScopedAllocationCounter;
using labq8::test::str;
namespace dl = labq8::dsp::dynamics::limits;

namespace
{
constexpr int tick = DynamicBellBand::controlIntervalSamples;

DynamicBellSettings dyn (double f0, double q, double gain, bool enabled, double threshold = -30.0, double range = 12.0,
                         double attackMs = 10.0, double releaseMs = 100.0)
{
    return { f0, q, gain, enabled, threshold, range, attackMs, releaseMs };
}

DynamicBellBand makeDyn (double fs, const DynamicBellSettings& s, int channels = 1)
{
    DynamicBellBand band;
    band.setTargets (s);
    band.prepare (fs, channels);
    return band;
}

template <typename Band>
void run (Band& band, Signal& x, int blockSize = 512)
{
    for (size_t pos = 0; pos < x.size(); pos += (size_t) blockSize)
    {
        float* ch[] = { x.data() + pos };
        band.process (ch, 1, (int) std::min<size_t> ((size_t) blockSize, x.size() - pos));
    }
}

// Processes tick by tick and records the reduction applied to each tick.
std::vector<double> runTrace (DynamicBellBand& band, Signal& x)
{
    std::vector<double> gr;
    for (size_t pos = 0; pos < x.size(); pos += tick)
    {
        float* ch[] = { x.data() + pos };
        band.process (ch, 1, (int) std::min<size_t> (tick, x.size() - pos));
        gr.push_back (band.getGainReductionDb()); // applied to the samples just processed
    }
    return gr;
}

double dbToAmp (double db) { return std::pow (10.0, db / 20.0); }

// Settling time of the detector for a sine at f0: band-pass decay (two
// sections, pole time constant Q / (pi f0)) and the level window.
double detectorSettle (double f0, double q)
{
    return 12.0 * q / (std::numbers::pi * f0) + 20.0 * DynamicDetector::windowSeconds (f0) + 0.01;
}

// Largest 4th-order difference: rejects a low tone, keeps discontinuities.
double maxFourthDifference (const Signal& y, size_t begin, size_t end)
{
    double m = 0.0;
    for (auto i = std::max<size_t> (begin, 4); i < std::min (end, y.size()); ++i)
        m = std::max (m, std::abs ((double) y[i] - 4.0 * y[i - 1] + 6.0 * y[i - 2] - 4.0 * y[i - 3] + y[i - 4]));
    return m;
}

struct Event
{
    size_t position;
    DynamicBellSettings settings;
};

// Renders with settings changes at fixed sample positions, whatever the block sizes.
template <typename NextBlock>
std::vector<Signal> renderScheduled (double fs, const DynamicBellSettings& initial, std::vector<Signal> input,
                                     const std::vector<Event>& events, NextBlock nextBlock)
{
    const auto channels = (int) input.size();
    auto band = makeDyn (fs, initial, channels);
    size_t pos = 0, next = 0;
    const auto n = input[0].size();
    while (pos < n)
    {
        while (next < events.size() && events[next].position <= pos)
            band.setTargets (events[next++].settings);
        auto len = std::min ((size_t) nextBlock(), n - pos);
        if (next < events.size())
            len = std::min (len, events[next].position - pos);
        float* ch[2] = { input[0].data() + pos, channels > 1 ? input[1].data() + pos : nullptr };
        band.process (ch, channels, (int) len);
        pos += len;
    }
    return input;
}

std::vector<Event> randomEvents (double fs, size_t n, unsigned seed, bool enabledAlways)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    std::vector<Event> events;
    for (size_t pos = 0; pos < n; pos += 1 + (size_t) (u (rng) * 0.01 * fs))
        events.push_back ({ pos, dyn (40.0 * std::pow (400.0, u (rng)), 0.2 * std::pow (60.0, u (rng)), -12.0 + 24.0 * u (rng),
                                      enabledAlways || u (rng) < 0.7, -60.0 + 50.0 * u (rng), 24.0 * u (rng),
                                      0.5 + 50.0 * u (rng), 5.0 + 300.0 * u (rng)) });
    return events;
}
} // namespace

//==============================================================================
LQ_TEST ("dyn detector: a sine at f0 reads its sine-peak dBFS level for any f0, Q and rate (1)")
{
    // Tolerance 0.15 dB: the level window leaves a +/-0.107 dB ripple at 2 f0
    // (two one-poles at 0.5/f0: |H(2 f0)|^2 = 1 / (1 + (2 pi)^2) per pole).
    double worst = 0.0;
    for (const auto fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const auto f0 : { 50.0, 1000.0, 10000.0 })
            for (const auto q : { 0.5, 1.0, 4.0, 18.0 })
                for (const auto ampDb : { -6.0206, -40.0 })
                {
                    DynamicDetector det;
                    det.prepare (fs, 1);
                    det.setRegion (f0, q);
                    const auto settle = samples (detectorSettle (f0, q), fs);
                    auto x = makeSine (f0, fs, settle + samples (0.05, fs) + 16, dbToAmp (ampDb));
                    double lo = 1e9, hi = -1e9;
                    for (size_t pos = 0; pos + 16 <= x.size(); pos += 16)
                    {
                        const float* ch[] = { x.data() };
                        det.process (ch, 1, (int) pos, 16);
                        if (pos >= settle)
                        {
                            lo = std::min (lo, det.getLevelDb());
                            hi = std::max (hi, det.getLevelDb());
                        }
                    }
                    const auto err = std::max (std::abs (hi - ampDb), std::abs (lo - ampDb));
                    worst = std::max (worst, err);
                    LQ_CHECK (err < 0.15, str ("level ", lo, " .. ", hi, " dB for a ", ampDb, " dB sine, f0=", f0, " Q=", q, " fs=", fs));
                }
    ctx.note (str ("worst deviation of the reading of a sine at f0: ", worst, " dB (ripple included)"));
}

LQ_TEST ("dyn detector: signals outside the region are rejected as the 4th-order band-pass predicts (2)")
{
    constexpr double fs = 48000.0, f0 = 1000.0;
    for (const auto q : { 0.7, 1.0, 2.0, 8.0 })
        for (const auto ratio : { 0.25, 0.5, 2.0, 4.0 })
        {
            const auto f = f0 * ratio;
            DynamicDetector det;
            det.prepare (fs, 1);
            det.setRegion (f0, q);
            // Averaged over the steady part: the level window is tuned to f0,
            // so a component below f0 keeps a larger 2f ripple (documented).
            const auto settle = samples (detectorSettle (std::min (f, f0), q), fs);
            auto x = makeSine (f, fs, settle + samples (0.2, fs), 0.5);
            const float* ch[] = { x.data() };
            double sum = 0.0;
            int count = 0;
            for (size_t pos = 0; pos + 16 <= x.size(); pos += 16)
            {
                det.process (ch, 1, (int) pos, 16);
                if (pos >= settle)
                {
                    sum += det.getMeanSquare();
                    ++count;
                }
            }
            const auto reading = 10.0 * std::log10 (2.0 * sum / count) - toDb (0.5);
            const auto bp = designBandPass (fs, f0, q);
            const auto expected = 2.0 * toDb (magnitudeAt (bp, f, fs)); // two sections
            LQ_CHECK (std::abs (reading - expected) < 0.2, str ("Q=", q, " f=", f, ": reading ", reading, " dB, expected ", expected));
            if (q >= 1.0 && (ratio == 0.25 || ratio == 4.0))
                LQ_CHECK (reading < -20.0, str ("two octaves away must read below -20 dB, got ", reading));
        }

    // Broadband noise reads in proportion to the band's noise bandwidth: the
    // threshold refers to tonal components at f0 (documented, informative).
    for (const auto q : { 0.5, 2.0, 8.0 })
    {
        DynamicDetector det;
        det.prepare (fs, 1);
        det.setRegion (f0, q);
        auto x = makeNoise (samples (2.0, fs), 99, 0.5f);
        const float* ch[] = { x.data() };
        double sum = 0.0;
        int count = 0;
        for (size_t pos = 0; pos + 16 <= x.size(); pos += 16)
        {
            det.process (ch, 1, (int) pos, 16);
            if (pos > x.size() / 4)
            {
                sum += det.getMeanSquare();
                ++count;
            }
        }
        ctx.note (str ("white noise (RMS ", toDb (0.5 / std::sqrt (3.0)), " dBFS) at Q ", q, " reads ", 10.0 * std::log10 (2.0 * sum / count), " dB"));
    }
}

LQ_TEST ("dyn law: knee, slope and range of the transfer law; steady state below and above the threshold (3, 5)")
{
    using dynamics::gainReductionDb;
    using dynamics::kneeWidthDb;
    const auto half = 0.5 * kneeWidthDb;

    // Shape of the law.
    LQ_CHECK (gainReductionDb (-40.0, -30.0, 12.0) == 0.0 && gainReductionDb (-30.0 - half, -30.0, 12.0) == 0.0, "zero below the knee");
    LQ_CHECK (std::abs (gainReductionDb (-30.0, -30.0, 12.0) - kneeWidthDb / 8.0) < 1e-12, "K/8 at the threshold");
    LQ_CHECK (std::abs (gainReductionDb (-30.0 + half, -30.0, 12.0) - half) < 1e-12, "continuous at the top of the knee");
    LQ_CHECK (gainReductionDb (-20.0, -30.0, 12.0) == 10.0 && gainReductionDb (0.0, -30.0, 12.0) == 12.0, "slope 1, capped at the range");
    LQ_CHECK (gainReductionDb (0.0, -30.0, 0.0) == 0.0 && gainReductionDb (0.0, -30.0, -5.0) == 0.0, "range 0 (or negative) means no reduction");
    double last = 0.0;
    bool monotonic = true, continuous = true;
    for (int i = 0; i <= 4000; ++i)
    {
        const auto r = gainReductionDb (-50.0 + i * 0.01, -30.0, 24.0);
        monotonic = monotonic && r >= last;
        continuous = continuous && r - last <= 0.01 + 1e-12;
        last = r;
    }
    LQ_CHECK (monotonic && continuous, "monotonic, slope <= 1");

    // Steady state on a band: measured reduction and output gain at f0.
    constexpr double fs = 48000.0, f0 = 1000.0, threshold = -30.0, range = 12.0;
    double worstGr = 0.0, worstOut = 0.0;
    for (const auto staticGain : { 0.0, 6.0, -6.0 })
        for (const auto level : { -60.0, -36.0, -33.0, -31.0, -30.0, -28.0, -25.0, -20.0, -10.0, 0.0 })
        {
            auto band = makeDyn (fs, dyn (f0, 1.0, staticGain, true, threshold, range, 5.0, 50.0));
            const auto amp = dbToAmp (level);
            auto x = makeSine (f0, fs, samples (0.6, fs), amp);
            auto y = x;
            const auto gr = runTrace (band, y);
            const auto expected = gainReductionDb (level, threshold, range);
            double lo = 1e9, hi = -1e9;
            for (size_t t = gr.size() / 2; t < gr.size(); ++t)
            {
                lo = std::min (lo, gr[t]);
                hi = std::max (hi, gr[t]);
            }
            // 0.1 dB: detector ripple (0.107 dB) times the law slope (<= 1), smoothed by the ballistics.
            const auto errGr = std::max (std::abs (lo - expected), std::abs (hi - expected));
            const auto begin = y.size() / 2;
            const auto outDb = toDb (fitAmplitude (y, begin, y.size(), f0, fs) / amp);
            const auto expectedOut = std::max (staticGain - expected, labq8::dsp::limits::minGainDb);
            const auto errOut = std::abs (outDb - expectedOut);
            worstGr = std::max (worstGr, errGr);
            worstOut = std::max (worstOut, errOut);
            LQ_CHECK (errGr < 0.1, str ("G=", staticGain, " L=", level, ": reduction ", lo, " .. ", hi, " expected ", expected));
            LQ_CHECK (errOut < 0.12, str ("G=", staticGain, " L=", level, ": gain at f0 ", outDb, " expected ", expectedOut));
            LQ_CHECK (hi <= range, "never more than the range");
        }

    // Far above the threshold the reduction stops exactly at the range (5).
    {
        auto band = makeDyn (fs, dyn (f0, 1.0, 0.0, true, -40.0, 6.0, 1.0, 50.0));
        auto x = makeSine (f0, fs, samples (0.5, fs), 0.5);
        auto y = x;
        const auto gr = runTrace (band, y);
        LQ_CHECK (*std::max_element (gr.begin(), gr.end()) <= 6.0 && std::abs (gr.back() - 6.0) < 1e-3,
                  str ("range 6: max ", *std::max_element (gr.begin(), gr.end()), ", final ", gr.back()));
        const auto outDb = toDb (fitAmplitude (y, y.size() / 2, y.size(), f0, fs) / 0.5);
        LQ_CHECK (std::abs (outDb + 6.0) < 0.02, str ("output at f0 with range 6: ", outDb, " dB"));
    }

    ctx.note (str ("steady state: reduction within ", worstGr, " dB of the law, gain at f0 within ", worstOut, " dB"));
}

LQ_TEST ("dyn ballistics: attack and release time constants (4)")
{
    // Level step from 30 dB below to 20 dB above the threshold: target 20 dB.
    // The 63.2 % point must fall within [tau - 1 tick, tau + detector delay],
    // detector delay about 1 ms at 2 kHz (band-pass and window), bound 2 ms
    // for attack and 4 ms for release (the level must fall through the knee).
    constexpr double fs = 48000.0, f0 = 2000.0;
    const auto tickSeconds = tick / fs;
    for (const auto attack : { 2.0, 5.0, 20.0, 50.0 })
        for (const auto release : { 20.0, 100.0, 400.0 })
        {
            auto band = makeDyn (fs, dyn (f0, 1.0, 0.0, true, -40.0, 24.0, attack, release));
            const auto stepUp = samples (0.1, fs), stepDown = samples (0.1 + 0.3 + 10.0 * attack * 1e-3, fs);
            const auto n = stepDown + samples (20.0 * release * 1e-3 + 0.1, fs);
            Signal x (n);
            for (size_t i = 0; i < n; ++i)
                x[i] = (float) ((i >= stepUp && i < stepDown ? dbToAmp (-20.0) : dbToAmp (-70.0)) * std::sin (2.0 * std::numbers::pi * f0 * (double) i / fs));
            const auto gr = runTrace (band, x);

            const auto peak = gr[stepDown / tick - 1];
            double tAttack = -1, tRelease = -1;
            for (size_t t = stepUp / tick; t < gr.size() && tAttack < 0; ++t)
                if (gr[t] >= 0.632 * peak)
                    tAttack = (double) ((t + 1) * tick - stepUp) / fs;
            for (size_t t = stepDown / tick; t < gr.size() && tRelease < 0; ++t)
                if (gr[t] <= 0.368 * peak)
                    tRelease = (double) ((t + 1) * tick - stepDown) / fs;

            LQ_CHECK (std::abs (peak - 20.0) < 0.12, str ("reduction before release ", peak)); // detector ripple 0.107 dB
            LQ_CHECK (tAttack >= attack * 1e-3 - tickSeconds && tAttack <= attack * 1e-3 + 0.002,
                      str ("attack ", attack, " ms measured ", tAttack * 1e3, " ms"));
            LQ_CHECK (tRelease >= release * 1e-3 - tickSeconds && tRelease <= release * 1e-3 + 0.004,
                      str ("release ", release, " ms measured ", tRelease * 1e3, " ms"));
            LQ_CHECK (gr.back() == 0.0, "back to exactly 0 dB reduction after the release");
            if (attack == 5.0 && release == 100.0)
                ctx.note (str ("attack 5 ms -> ", tAttack * 1e3, " ms, release 100 ms -> ", tRelease * 1e3, " ms (63.2 % points)"));
        }
}

LQ_TEST ("dyn transients: bursts and impulses stay bounded, no lookahead, full recovery (6)")
{
    constexpr double fs = 48000.0, f0 = 1000.0;
    auto band = makeDyn (fs, dyn (f0, 2.0, 0.0, true, -30.0, 12.0, 10.0, 100.0));
    const auto n = samples (3.5, fs);
    Signal x (n, 0.0f);
    // 5 ms bursts 20 dB over the threshold every 200 ms, then an impulse, then silence.
    for (size_t start = samples (0.1, fs); start < samples (1.5, fs); start += samples (0.2, fs))
        for (size_t i = 0; i < samples (0.005, fs); ++i)
            x[start + i] = (float) (dbToAmp (-10.0) * std::sin (2.0 * std::numbers::pi * f0 * (double) i / fs));
    x[samples (1.6, fs)] = 1.0f;
    auto y = x;
    const auto gr = runTrace (band, y);

    LQ_CHECK (allFinite (y), "finite output");
    LQ_CHECK (*std::max_element (gr.begin(), gr.end()) <= 12.0, "reduction never exceeds the range");
    // No lookahead: the first millisecond of the first burst passes at the static gain.
    const auto first = samples (0.1, fs);
    const auto firstMs = samples (0.001, fs);
    LQ_CHECK (gr[first / tick] < 0.5, str ("reduction at the burst onset ", gr[first / tick], " dB (no lookahead)"));
    // A 5 ms burst with a 10 ms attack only gets part of the 12 dB.
    double burstMax = 0.0;
    for (size_t t = first / tick; t < (first + samples (0.006, fs)) / tick; ++t)
        burstMax = std::max (burstMax, gr[t]);
    LQ_CHECK (burstMax > 1.0 && burstMax < 12.0, str ("reduction reached during a 5 ms burst: ", burstMax, " dB"));
    LQ_CHECK (gr.back() == 0.0 && band.getBand().isIdle(), "silence after the release: reduction exactly 0 and band idle");
    ctx.note (str ("5 ms burst at +20 dB over threshold, attack 10 ms: up to ", burstMax, " dB of reduction; first ms passes ",
                   toDb (peak (y, first, first + firstMs) / peak (x, first, first + firstMs)), " dB"));
}

LQ_TEST ("dyn at rest: low level, silence and dynamics off are bit-identical to the static band (7, 13)")
{
    for (const auto fs : { 44100.0, 96000.0 })
        for (const auto gain : { 0.0, 6.0, -9.0 })
        {
            // (7) A -120 dBFS sine far below a -60 dBFS threshold: never any reduction.
            const auto x = makeSine (1000.0, fs, samples (0.5, fs), dbToAmp (-120.0));
            auto yDyn = x, yStatic = x;
            auto band = makeDyn (fs, dyn (1000.0, 1.0, gain, true, -60.0, 12.0));
            EqBand reference;
            reference.setTargets (BandSettings { 1000.0, gain, 1.0, false, FilterType::bell });
            reference.prepare (fs, 1);
            const auto gr = runTrace (band, yDyn);
            run (reference, yStatic, tick);
            LQ_CHECK (yDyn == yStatic && *std::max_element (gr.begin(), gr.end()) == 0.0,
                      str ("low-level signal must be the static band exactly, G=", gain, " fs=", fs));

            // Silence into a fresh band: exact zeros out, detector at its floor.
            auto fresh = makeDyn (fs, dyn (1000.0, 1.0, gain, true, -60.0, 12.0));
            Signal silence (samples (0.2, fs), 0.0f);
            run (fresh, silence, 100);
            LQ_CHECK (std::all_of (silence.begin(), silence.end(), [] (float v) { return v == 0.0f; }), "silence stays silence");
            LQ_CHECK (fresh.getDetectorLevelDb() == DynamicDetector::levelFloorDb,
                      str ("detector level on silence ", fresh.getDetectorLevelDb()));
        }

    // (13) Dynamics off with automation of f, Q and static gain: bit-identical
    // to EqBand, mono and stereo, any block size.
    for (const auto fs : { 44100.0, 48000.0, 96000.0 })
        for (const int channels : { 1, 2 })
        {
            const auto n = samples (1.0, fs);
            std::vector<Signal> input { makeNoise (n, 31), makeNoise (n, 32) };
            input.resize ((size_t) channels);
            auto events = randomEvents (fs, n, 33, false);
            for (auto& e : events)
                e.settings.dynamicEnabled = false;
            const auto blockSizes = { 1, 64, 512 };
            for (const auto block : blockSizes)
            {
                const auto out = renderScheduled (fs, dyn (500.0, 1.0, 3.0, false), input, events, [block] { return block; });

                EqBand reference;
                reference.setTargets (BandSettings { 500.0, 3.0, 1.0, false, FilterType::bell });
                reference.prepare (fs, channels);
                auto expected = input;
                size_t pos = 0, next = 0;
                while (pos < n)
                {
                    while (next < events.size() && events[next].position <= pos)
                    {
                        const auto& s = events[next++].settings;
                        reference.setTargets (BandSettings { s.frequencyHz, s.staticGainDb, s.q, false, FilterType::bell });
                    }
                    auto len = std::min ((size_t) block, n - pos);
                    if (next < events.size())
                        len = std::min (len, events[next].position - pos);
                    float* ch[2] = { expected[0].data() + pos, channels > 1 ? expected[1].data() + pos : nullptr };
                    reference.process (ch, channels, (int) len);
                    pos += len;
                }
                LQ_CHECK (out == expected, str ("dynamics off must equal EqBand Bell exactly, fs=", fs, " ch=", channels, " block=", block));
            }
        }

    // At 0 dB static gain and dynamics off the buffer is untouched.
    auto band = makeDyn (48000.0, dyn (1000.0, 1.0, 0.0, false));
    auto x = makeNoise (48000, 41);
    auto y = x;
    run (band, y, 333);
    LQ_CHECK (y == x && band.getBand().isIdle(), "0 dB, dynamics off: bit-exact pass-through");

    // EqBand hook used directly: a modulation set once must survive silence
    // (a 0 dB band must not go idle while it is modulated).
    EqBand e;
    e.setTargets (BandSettings { 1000.0, 0.0, 1.0, false, FilterType::bell });
    e.prepare (48000.0, 1);
    e.setGainModulationDb (-12.0);
    Signal quiet (4800, 0.0f);
    run (e, quiet, 64);
    auto tone = makeSine (1000.0, 48000.0, 24000, 0.5);
    run (e, tone, 64);
    const auto toneDb = toDb (fitAmplitude (tone, 12000, 24000, 1000.0, 48000.0) / 0.5);
    LQ_CHECK (! e.isIdle() && std::abs (toneDb + 12.0) < 0.01, str ("modulated 0 dB band after silence: ", toneDb, " dB at f0"));
}

LQ_TEST ("dyn extremes: signals above 0 dBFS are not clipped or limited; NaN/Inf are contained (8)")
{
    constexpr double fs = 48000.0, f0 = 1000.0;

    // +24 dBFS sine, threshold 0 dBFS, range 12: 12 dB of reduction, output
    // stays far above full scale (no clipping or limiting anywhere).
    {
        auto band = makeDyn (fs, dyn (f0, 1.0, 0.0, true, 0.0, 12.0, 5.0, 50.0));
        auto x = makeSine (f0, fs, samples (0.5, fs), dbToAmp (24.0));
        auto y = x;
        const auto gr = runTrace (band, y);
        const auto outDb = toDb (fitAmplitude (y, y.size() / 2, y.size(), f0, fs));
        LQ_CHECK (allFinite (y) && std::abs (gr.back() - 12.0) < 1e-3, str ("reduction at +24 dBFS ", gr.back()));
        LQ_CHECK (std::abs (outDb - 12.0) < 0.05 && peak (y, y.size() / 2) > 3.9f, str ("output level ", outDb, " dBFS, peak ", peak (y, y.size() / 2)));
        ctx.note (str ("+24 dBFS sine, threshold 0 dBFS, range 12 dB: output ", outDb, " dBFS (unclipped)"));
    }

    // Dynamics off: values above full scale go through exactly like the static band.
    {
        auto band = makeDyn (fs, dyn (f0, 1.0, 6.0, false));
        EqBand reference;
        reference.setTargets (BandSettings { f0, 6.0, 1.0, false, FilterType::bell });
        reference.prepare (fs, 1);
        auto x = makeNoise (samples (0.3, fs), 51, 8.0f);
        auto y1 = x, y2 = x;
        run (band, y1);
        run (reference, y2);
        LQ_CHECK (y1 == y2 && peak (y1) > 8.0f, "loud input with dynamics off: static band, no limiting");
    }

    // Non-finite and huge input.
    {
        auto clean = makeDyn (fs, dyn (f0, 2.0, 3.0, true, -30.0, 12.0, 5.0, 80.0));
        auto dirty = makeDyn (fs, dyn (f0, 2.0, 3.0, true, -30.0, 12.0, 5.0, 80.0));
        auto x = makeSine (f0, fs, samples (2.0, fs), dbToAmp (-20.0));
        auto xDirty = x;
        const auto nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        for (const auto [pos, v] : { std::pair { 0.2, nan }, std::pair { 0.3, inf }, std::pair { 0.35, -inf },
                                     std::pair { 0.4, 1.0e30f }, std::pair { 0.41, -1.0e30f } })
            for (size_t i = 0; i < 40; ++i)
                xDirty[samples (pos, fs) + i] = v;
        auto y1 = x, y2 = xDirty;
        const auto gr1 = runTrace (clean, y1);
        const auto gr2 = runTrace (dirty, y2);
        bool finite = allFinite (y2), bounded = true;
        for (const auto g : gr2)
            bounded = bounded && std::isfinite (g) && g >= 0.0 && g <= 12.0;
        LQ_CHECK (finite && bounded, "finite output and reduction within [0, range] with NaN, Inf and 1e30 input");
        LQ_CHECK (std::abs (gr1.back() - gr2.back()) < 0.01, str ("recovered: reduction ", gr2.back(), " vs clean ", gr1.back()));
    }
}

LQ_TEST ("dyn automation: fast random changes of every setting stay finite, bounded, allocation-free and converge (9)")
{
    for (const auto fs : { 44100.0, 96000.0 })
    {
        const auto n = samples (2.0, fs);
        auto events = randomEvents (fs, n, 61, true);
        // Fast changes: every 1..100 samples during the first second.
        std::mt19937 rng (62);
        std::uniform_int_distribution<int> step (1, 100);
        std::vector<Event> fast;
        for (size_t pos = 0; pos < n / 2; pos += (size_t) step (rng))
            fast.push_back ({ pos, events[(pos * 7) % events.size()].settings });
        const auto finalSettings = dyn (800.0, 2.0, 4.0, true, -30.0, 9.0, 5.0, 50.0);
        fast.push_back ({ n / 2, finalSettings });

        auto band = makeDyn (fs, events[0].settings);
        auto x = makeSine (800.0, fs, n, dbToAmp (-12.0));
        for (size_t i = 0; i < n; ++i)
            x[i] += 0.1f * (float) std::sin (2.0 * std::numbers::pi * 97.0 * (double) i / fs);
        auto y = x;
        long allocations = 0;
        size_t next = 0;
        {
            ScopedAllocationCounter counter;
            for (size_t pos = 0; pos < n; pos += tick)
            {
                while (next < fast.size() && fast[next].position <= pos)
                    band.setTargets (fast[next++].settings);
                float* ch[] = { y.data() + pos };
                band.process (ch, 1, (int) std::min<size_t> (tick, n - pos));
            }
            allocations = counter.count();
        }
        LQ_CHECK (allocations == 0, str (allocations, " allocations while processing"));
        LQ_CHECK (allFinite (y), "finite output");

        // Converged: the end state equals a fresh band with the final settings.
        auto fresh = makeDyn (fs, finalSettings);
        auto z = x;
        run (fresh, z, tick);
        LQ_CHECK (std::abs (band.getGainReductionDb() - fresh.getGainReductionDb()) < 0.02,
                  str ("reduction after automation ", band.getGainReductionDb(), " vs fresh band ", fresh.getGainReductionDb()));
        const auto tail = n - samples (0.2, fs);
        double maxDiff = 0.0;
        for (size_t i = tail; i < n; ++i)
            maxDiff = std::max (maxDiff, (double) std::abs (y[i] - z[i]));
        LQ_CHECK (maxDiff < 1e-4, str ("output after automation differs from a fresh band by ", maxDiff));
    }
}

LQ_TEST ("dyn blocks: output bit-identical for any block size with dynamics active and automation (10)")
{
    for (const auto fs : { 44100.0, 96000.0 })
        for (const int channels : { 1, 2 })
        {
            const auto n = samples (1.0, fs);
            std::vector<Signal> input { makeSine (700.0, fs, n, 0.3), makeNoise (n, 72, 0.2f) };
            for (size_t i = 0; i < n; ++i)
                input[0][i] += 0.2f * input[1][i];
            input.resize ((size_t) channels);
            const auto events = randomEvents (fs, n, 73, true);
            const auto reference = renderScheduled (fs, dyn (700.0, 1.0, 0.0, true), input, events, [] { return 512; });
            std::mt19937 rng (74);
            std::uniform_int_distribution<int> random (1, 700);
            for (const int block : { 1, 7, 16, 64, 480, 4096, -1 })
            {
                const auto out = renderScheduled (fs, dyn (700.0, 1.0, 0.0, true), input, events,
                                                  [&] { return block > 0 ? block : random (rng); });
                LQ_CHECK (out == reference, str ("block ", block, " differs, fs=", fs, " channels=", channels));
            }
        }
}

LQ_TEST ("dyn modulation: gain changes are interpolated per sample; no significant clicks; time-varying filter stays stable (11)")
{
    // A sine at f0 rises (2 ms raised-cosine onset, so the input itself has no
    // discontinuity) from far below to far above the threshold. References:
    //  - ideal: the same per-tick reduction values applied per sample, with
    //    the gain (not the coefficients) interpolated linearly over each tick
    //    and the Bell redesigned every sample (what a per-sample engine does);
    //  - staircase: the same values held for whole ticks (E2a before the
    //    coefficient interpolation), to quantify the zipper it avoids;
    constexpr double fs = 48000.0;
    double worstResidual = -1e9, worstStaircase = -1e9, worstRatio = -1e9;
    // Largest 4th difference of the deviation from the ideal: the high-frequency
    // part of the error, i.e. clicks and zipper, without the signal itself.
    auto clickLevel = [] (const Signal& a, const Signal& ideal, size_t from, size_t to)
    {
        Signal d (to - from);
        for (auto i = from; i < to; ++i)
            d[i - from] = a[i] - ideal[i];
        return maxFourthDifference (d, 0, d.size());
    };
    for (const auto f0 : { 100.0, 1000.0, 5000.0 })
        for (const auto attack : { 0.5, 2.0, 10.0 })
        {
            const auto settings = dyn (f0, 2.0, 0.0, true, -30.0, 12.0, attack, 100.0);
            const auto n = samples (0.5, fs), jump = samples (0.2, fs), onset = samples (0.002, fs);
            Signal x (n);
            for (size_t i = 0; i < n; ++i)
            {
                const auto w = i < jump ? 0.0 : (i >= jump + onset ? 1.0 : 0.5 - 0.5 * std::cos (std::numbers::pi * (double) (i - jump) / (double) onset));
                x[i] = (float) ((dbToAmp (-50.0) + w * (dbToAmp (-6.0) - dbToAmp (-50.0))) * std::sin (2.0 * std::numbers::pi * f0 * (double) i / fs));
            }
            auto band = makeDyn (fs, settings);
            auto y = x;
            const auto gr = runTrace (band, y);

            auto perSample = [&] (bool interpolate)
            {
                Signal out (n);
                double s1 = 0, s2 = 0;
                for (size_t i = 0; i < n; ++i)
                {
                    const auto t = i / tick;
                    const auto w = (double) (i % tick + 1) / tick;
                    const auto prev = t == 0 ? 0.0 : gr[t - 1];
                    const auto g = interpolate ? -(prev + w * (gr[t] - prev)) : -gr[t];
                    const auto c = designBell (fs, f0, g, 2.0);
                    const double in = x[i];
                    const auto o = c.b0 * in + s1;
                    s1 = c.b1 * in - c.a1 * o + s2;
                    s2 = c.b2 * in - c.a2 * o;
                    out[i] = (float) o;
                }
                return out;
            };
            const auto ideal = perSample (true);
            const auto staircase = perSample (false);

            const auto from = jump, to = jump + samples (5.0 * attack * 1e-3 + 0.01, fs);
            auto residualDb = [&] (const Signal& a)
            {
                double diff = 0.0, sig = 0.0;
                for (auto i = from; i < to; ++i)
                {
                    diff += ((double) a[i] - ideal[i]) * ((double) a[i] - ideal[i]);
                    sig += (double) ideal[i] * ideal[i];
                }
                return 10.0 * std::log10 (diff / sig + 1e-30);
            };
            const auto residual = residualDb (y);
            const auto stair = residualDb (staircase);
            const auto ratioDb = toDb (std::max (clickLevel (y, ideal, from, to), 1e-12) / clickLevel (staircase, ideal, from, to));
            worstResidual = std::max (worstResidual, residual);
            worstStaircase = std::max (worstStaircase, stair);
            worstRatio = std::max (worstRatio, ratioDb);

            // Criteria: residual against the per-sample engine below -55 dB of
            // the signal (coefficients are not linear in gain, so it is not 0;
            // the staircase reaches about -22 dB), and the high-frequency part
            // of the deviation at least 20 dB below that of the staircase.
            LQ_CHECK (residual < -55.0, str ("f0=", f0, " attack=", attack, ": residual vs per-sample modulation ", residual, " dB"));
            LQ_CHECK (ratioDb < -20.0, str ("f0=", f0, " attack=", attack, ": clicks vs 16-sample staircase ", ratioDb, " dB"));
        }
    ctx.note (str ("residual vs per-sample modulation: interpolated ", worstResidual, " dB, 16-sample staircase ", worstStaircase,
                   " dB (worst cases); clicks (4th difference of the deviation) vs staircase: ", worstRatio, " dB at worst"));

    // Worst-case time variation: the modulation alternates between 0 and -24 dB
    // every tick (far faster than any ballistics). The time-varying filter must
    // stay bounded and decay to silence.
    for (const auto fs2 : { 48000.0, 192000.0 })
        for (const auto f0 : { 20.0, 1000.0, 20000.0 })
            for (const auto q : { 0.1, 18.0 })
                for (const auto gain : { 0.0, 24.0 })
                {
                    EqBand e;
                    e.setTargets (BandSettings { f0, gain, q, false, FilterType::bell });
                    e.prepare (fs2, 1);
                    auto x = makeNoise (samples (2.0, fs2), 81, 0.5f);
                    x.resize (x.size() + samples (3.0, fs2), 0.0f);
                    float maxOut = 0.0f;
                    for (size_t pos = 0, t = 0; pos < x.size(); pos += tick, ++t)
                    {
                        e.setGainModulationDb ((t % 2) ? -24.0 : 0.0);
                        float* ch[] = { x.data() + pos };
                        e.process (ch, 1, (int) std::min<size_t> (tick, x.size() - pos));
                    }
                    maxOut = peak (x);
                    const auto end = x.size();
                    const auto earlier = peak (x, end - samples (1.0, fs2), end - samples (0.9, fs2));
                    const auto tail = peak (x, end - samples (0.1, fs2));
                    // Bound: the static worst-case gain at +24 dB Q 18 is +26 dB (labq8_sweep, L1);
                    // allow 6 dB on top for the time variation (0.5 peak input -> 20 linear).
                    // After the input stops the output must decay (the slowest pole, +24 dB
                    // Q 18 at 20 Hz, has a 1.1 s time constant, so "decaying" is checked,
                    // not "silent").
                    LQ_CHECK (allFinite (x) && maxOut < 20.0f && tail < 1e-3f * maxOut && (tail < earlier || tail < 1e-9f),
                              str ("alternating modulation fs=", fs2, " f0=", f0, " Q=", q, " G=", gain, ": peak ", maxOut, ", tail ", tail, ", earlier ", earlier));
                }
}

LQ_TEST ("dyn stereo: linked detection applies one reduction to both channels; link modes")
{
    constexpr double fs = 48000.0, f0 = 1000.0;
    const auto n = samples (0.5, fs);
    const auto sine = makeSine (f0, fs, n, 0.5);
    const Signal silence (n, 0.0f);

    auto levelFor = [&] (DetectorLink link, const Signal& l, const Signal& r)
    {
        DynamicDetector det;
        det.prepare (fs, 2);
        det.setLink (link);
        det.setRegion (f0, 1.0);
        const float* ch[] = { l.data(), r.data() };
        det.process (ch, 2, 0, (int) n);
        return det.getLevelDb();
    };

    const auto a = toDb (0.5);
    LQ_CHECK (std::abs (levelFor (DetectorLink::maxLinked, sine, sine) - a) < 0.15 && std::abs (levelFor (DetectorLink::maxLinked, sine, silence) - a) < 0.15,
              "max link: the same reading for a centred and a hard-panned source");
    LQ_CHECK (std::abs (levelFor (DetectorLink::energyLinked, sine, silence) - (a - 3.0103)) < 0.15, "energy link: -3 dB for a hard-panned source");
    LQ_CHECK (std::abs (levelFor (DetectorLink::mono, sine, silence) - (a - 6.0206)) < 0.15, "mono (L+R)/2: -6 dB for a hard-panned source");

    // One reduction for both channels: with the same input in L and R the two
    // outputs are identical, and a source only in L reduces R by the same gain.
    auto band = makeDyn (fs, dyn (f0, 1.0, 0.0, true, -20.0, 12.0, 5.0, 50.0), 2);
    auto l = sine, r = makeSine (f0, fs, n, 0.05);
    for (size_t pos = 0; pos < n; pos += 256)
    {
        float* ch[] = { l.data() + pos, r.data() + pos };
        band.process (ch, 2, (int) std::min<size_t> (256, n - pos));
    }
    const auto gl = toDb (fitAmplitude (l, n / 2, n, f0, fs) / 0.5), gr = toDb (fitAmplitude (r, n / 2, n, f0, fs) / 0.05);
    LQ_CHECK (std::abs (gl - gr) < 0.01 && gl < -10.0, str ("L ", gl, " dB, R ", gr, " dB: the stereo image must not move"));
}

LQ_TEST ("dyn perf: CPU of one dynamic band against the static band (stereo, Release) (12)")
{
#ifndef NDEBUG
    ctx.skip ("Debug build: timings are not meaningful; run the Release test binary");
    return;
#else
    constexpr double fs = 96000.0;
    constexpr double seconds = 5.0;
    const auto n = samples (seconds, fs);
    // Level modulated at 3 Hz across the threshold: the reduction never settles.
    Signal left (n), right (n);
    const auto noise = makeNoise (n, 91, 0.05f);
    for (size_t i = 0; i < n; ++i)
    {
        const auto env = 0.05 + 0.45 * (0.5 + 0.5 * std::sin (2.0 * std::numbers::pi * 3.0 * (double) i / fs));
        left[i] = (float) (env * std::sin (2.0 * std::numbers::pi * 1000.0 * (double) i / fs)) + noise[i];
        right[i] = left[i] * 0.8f;
    }

    auto measure = [&] (auto makeBand)
    {
        std::vector<double> loads;
        for (int r = 0; r < 6; ++r)
        {
            auto band = makeBand();
            auto l = left, rr = right;
            const auto start = std::chrono::steady_clock::now();
            for (size_t pos = 0; pos < n; pos += 64)
            {
                float* ch[] = { l.data() + pos, rr.data() + pos };
                band.process (ch, 2, (int) std::min<size_t> (64, n - pos));
            }
            const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            if (r > 0)
                loads.push_back (elapsed / seconds);
        }
        std::sort (loads.begin(), loads.end());
        return loads[loads.size() / 2];
    };

    const auto staticLoad = measure ([&] { EqBand b; b.setTargets (BandSettings { 1000.0, 6.0, 2.0, false, FilterType::bell }); b.prepare (fs, 2); return b; });
    const auto offLoad = measure ([&] { return makeDyn (fs, dyn (1000.0, 2.0, 6.0, false), 2); });
    const auto restLoad = measure ([&] { return makeDyn (fs, dyn (1000.0, 2.0, 6.0, true, 0.0, 12.0), 2); });
    const auto activeLoad = measure ([&] { return makeDyn (fs, dyn (1000.0, 2.0, 6.0, true, -20.0, 12.0, 2.0, 30.0), 2); });

    ctx.note (str ("96 kHz stereo, block 64, % of one core: static Bell ", staticLoad * 100.0, ", dynamic off ", offLoad * 100.0,
                   ", dynamic at rest ", restLoad * 100.0, ", dynamic active ", activeLoad * 100.0));
    // Budget: 0.5 % of one core for one active dynamic band (eight would be
    // 4 %, inside the 5 % budget of the E1a/E1b perf tests).
    LQ_CHECK (activeLoad < 0.005, str ("active dynamic band uses ", activeLoad * 100.0, " % of one core"));
#endif
}
