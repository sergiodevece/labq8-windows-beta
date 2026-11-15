// labq8_ms_bench (E4b): STEREO <-> MID/SIDE mode changes and CPU of the
// three-bank engine. JUCE-free; build in Release.
//
//   --transitions  Study of the mode-change policy (docs/MIDSIDE_E4B.md).
//                  Every scenario switches STEREO -> M/S (and back) on a
//                  fixed signal. The reference is the IDEAL change: both
//                  paths running in their steady state from t = 0 and the
//                  output crossfaded from one to the other. Policies:
//                    cold        the incoming path starts at the switch
//                                (zero filter state, zero reduction) and
//                                the fade starts at once;
//                    prewarm P   the incoming path starts at the switch,
//                                the fade starts P ms later;
//                    engine      dsp::StereoModeEngine (standby: the path
//                                not heard always runs on a copy).
//                  Reported against the ideal: peak error (dBFS and dB
//                  relative to the signal peak), worst 5 ms RMS deviation,
//                  and how long after the end of the fade the error stays
//                  above -80 dBFS. Then, for the ideal change itself, the
//                  fade length: a 4th-difference click measure against the
//                  steady signal and the worst 5 ms RMS deviation from the
//                  interpolated RMS. Then fast toggling on the engine.
//   --crossfade-analysis
//                  The level variation of the ideal 20 ms change itself
//                  (E4b review): signed deviation of the RMS of the
//                  crossfaded output from the RMS interpolated between both
//                  paths, with 5 / 20 / 50 ms windows; the correlation rho
//                  of both paths inside the fade and the dip a linear
//                  crossfade of signals with that correlation must have at
//                  w = 1/2; and the measure's own noise (the same windows on
//                  one path against itself shifted by one window step).
//   --cpu          CPU load of the engine against the pre-E4b STEREO core
//                  (one EqCore, stereo) at 44.1 / 48 / 96 kHz, 1 and 8
//                  instances, interleaved runs, median of 7.

#include "dsp/EqCore.h"
#include "dsp/MidSide.h"
#include "dsp/StereoModeEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace labq8::dsp;

namespace
{
constexpr double pi = 3.14159265358979323846;
using Signal = std::array<std::vector<float>, 2>;

struct Bank
{
    std::array<BandSettings, 8> bands {};
    std::array<DynamicSettings, 8> dynamics {};
};

struct Banks
{
    Bank stereo, mid, side;
};

Bank flatBank()
{
    Bank b;
    const double f[] = { 1000, 40, 100, 250, 2500, 5000, 10000, 16000 };
    for (int i = 0; i < 8; ++i)
        b.bands[(size_t) i] = { f[i], 0.0, 1.0, false, FilterType::bell };
    return b;
}

void apply (EqCore& core, const Bank& b)
{
    for (int i = 0; i < 8; ++i)
    {
        core.setBandTargets (i, b.bands[(size_t) i]);
        core.setBandDynamics (i, b.dynamics[(size_t) i]);
    }
}

void apply (StereoModeEngine& e, const Banks& b)
{
    for (int i = 0; i < 8; ++i)
    {
        e.setBandTargets (StereoBank::stereo, i, b.stereo.bands[(size_t) i]);
        e.setBandDynamics (StereoBank::stereo, i, b.stereo.dynamics[(size_t) i]);
        e.setBandTargets (StereoBank::mid, i, b.mid.bands[(size_t) i]);
        e.setBandDynamics (StereoBank::mid, i, b.mid.dynamics[(size_t) i]);
        e.setBandTargets (StereoBank::side, i, b.side.bands[(size_t) i]);
        e.setBandDynamics (StereoBank::side, i, b.side.dynamics[(size_t) i]);
    }
}

// One path, from sample `start` with fresh cores (zero state), in 256 blocks.
struct PathRunner
{
    EqCore stereo, mid, side;
    bool midSide;

    PathRunner (const Banks& b, bool ms, double fs) : midSide (ms)
    {
        apply (stereo, b.stereo); apply (mid, b.mid); apply (side, b.side);
        stereo.prepare (fs, 2); mid.prepare (fs, 1); side.prepare (fs, 1);
    }

    void process (float* l, float* r, int n)
    {
        if (! midSide)
        {
            float* ch[2] = { l, r };
            stereo.process (ch, 2, n);
            return;
        }
        std::vector<float> m ((size_t) n), s ((size_t) n);
        midside::encode (l, r, m.data(), s.data(), n);
        float* mp[1] = { m.data() };
        float* sp[1] = { s.data() };
        mid.process (mp, 1, n);
        side.process (sp, 1, n);
        midside::decode (m.data(), s.data(), l, r, n);
    }
};

Signal runPath (const Banks& b, bool ms, const Signal& in, double fs, int start = 0)
{
    Signal out = in;
    PathRunner p (b, ms, fs);
    const int total = (int) in[0].size();
    for (int pos = start; pos < total; pos += 256)
    {
        const int n = std::min (256, total - pos);
        p.process (out[0].data() + pos, out[1].data() + pos, n);
    }
    for (int ch = 0; ch < 2; ++ch)
        std::fill (out[(size_t) ch].begin(), out[(size_t) ch].begin() + start, 0.0f);
    return out;
}

Signal crossfade (const Signal& a, const Signal& b, int fadeStart, int fadeLength)
{
    Signal out = a;
    const int total = (int) a[0].size();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = fadeStart; i < total; ++i)
        {
            const double w = std::min (1.0, (double) (i - fadeStart + 1) / fadeLength);
            const double x = a[(size_t) ch][(size_t) i];
            out[(size_t) ch][(size_t) i] = (float) (x + w * ((double) b[(size_t) ch][(size_t) i] - x));
        }
    return out;
}

Signal runEngine (const Banks& b, const Signal& in, double fs, double fadeSeconds,
                  const std::vector<std::pair<int, StereoMode>>& switches, int block = 256)
{
    StereoModeEngine e;
    apply (e, b);
    e.setMode (StereoMode::stereo);
    e.prepare (fs, 2, fadeSeconds);
    Signal out = in;
    const int total = (int) in[0].size();
    size_t next = 0;
    for (int pos = 0; pos < total;)
    {
        int n = std::min (block, total - pos);
        if (next < switches.size() && switches[next].first == pos)
            e.setMode (switches[next++].second);
        if (next < switches.size())
            n = std::min (n, switches[next].first - pos);
        float* ch[2] = { out[0].data() + pos, out[1].data() + pos };
        e.process (ch, 2, n);
        pos += n;
    }
    return out;
}

double peakAbs (const Signal& s, int from = 0)
{
    double p = 0.0;
    for (const auto& c : s)
        for (size_t i = (size_t) from; i < c.size(); ++i)
            p = std::max (p, (double) std::abs (c[i]));
    return p;
}

double db (double x) { return x > 0.0 ? 20.0 * std::log10 (x) : -999.0; }

struct Error
{
    double peak = 0.0, worstRmsDevDb = 0.0, settleMs = 0.0;
};

Error compare (const Signal& got, const Signal& ideal, int from, int fadeEnd, double fs)
{
    Error e;
    const int total = (int) got[0].size();
    int lastAbove = -1;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = from; i < total; ++i)
        {
            const double d = std::abs ((double) got[(size_t) ch][(size_t) i] - ideal[(size_t) ch][(size_t) i]);
            e.peak = std::max (e.peak, d);
            if (d > 1.0e-4)
                lastAbove = std::max (lastAbove, i);
        }
    e.settleMs = lastAbove < fadeEnd ? 0.0 : 1000.0 * (lastAbove - fadeEnd + 1) / fs;
    const int w = (int) std::lround (0.005 * fs);
    for (int start = from; start + w <= total; start += w)
    {
        double eg = 0.0, ei = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = start; i < start + w; ++i)
            {
                eg += (double) got[(size_t) ch][(size_t) i] * got[(size_t) ch][(size_t) i];
                ei += (double) ideal[(size_t) ch][(size_t) i] * ideal[(size_t) ch][(size_t) i];
            }
        if (ei > 1.0e-12)
            e.worstRmsDevDb = std::max (e.worstRmsDevDb, std::abs (10.0 * std::log10 (std::max (eg, 1.0e-30) / ei)));
    }
    return e;
}

double fourthDiffMax (const Signal& s, int from, int to)
{
    double m = 0.0;
    for (const auto& c : s)
        for (int i = std::max (from, 4); i < to; ++i)
            m = std::max (m, std::abs ((double) c[(size_t) i] - 4.0 * c[(size_t) i - 1] + 6.0 * c[(size_t) i - 2] - 4.0 * c[(size_t) i - 3] + c[(size_t) i - 4]));
    return m;
}

//==============================================================================
struct Scenario
{
    std::string name;
    Banks banks;
    Signal input;
};

Signal makeSignal (double fs, double seconds, const std::function<std::pair<double, double> (double, std::mt19937&)>& f)
{
    const int n = (int) std::lround (fs * seconds);
    Signal s { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
    std::mt19937 rng (7);
    for (int i = 0; i < n; ++i)
    {
        const auto [l, r] = f ((double) i / fs, rng);
        s[0][(size_t) i] = (float) l;
        s[1][(size_t) i] = (float) r;
    }
    return s;
}

std::vector<Scenario> scenarios (double fs)
{
    std::vector<Scenario> v;
    const double seconds = 4.0;

    // 1. Sustained low tone, steep low bell, banks copied (STEREO = MID = SIDE).
    {
        Banks b { flatBank(), flatBank(), flatBank() };
        b.stereo.bands[1] = { 40.0, 18.0, 10.0, false, FilterType::bell };
        b.mid = b.side = b.stereo;
        v.push_back ({ "low tone 40 Hz, bell +18 dB Q 10, copied banks", b,
                       makeSignal (fs, seconds, [] (double t, std::mt19937&) { const double x = 0.2 * std::sin (2 * pi * 40 * t); return std::pair { x, 0.7 * x + 0.05 * std::sin (2 * pi * 41 * t) }; }) });
    }
    // 2. Extreme EQ on noise + 25 Hz: 20 Hz +24 Q 18, low cut 30 Hz Q 2, high shelf +24, notch Q 18; MID / SIDE different.
    {
        Banks b { flatBank(), flatBank(), flatBank() };
        b.stereo.bands[1] = { 20.0, 24.0, 18.0, false, FilterType::bell };
        b.stereo.bands[2] = { 30.0, 0.0, 2.0, false, FilterType::lowCut };
        b.stereo.bands[6] = { 12000.0, 24.0, 1.0, false, FilterType::highShelf };
        b.stereo.bands[0] = { 1000.0, 0.0, 18.0, false, FilterType::notch };
        b.mid = b.stereo;
        b.side = flatBank();
        b.side.bands[3] = { 250.0, -24.0, 18.0, false, FilterType::bell };
        b.side.bands[7] = { 16000.0, 24.0, 18.0, false, FilterType::bell };
        v.push_back ({ "extreme EQ (20 Hz +24 Q18, cuts, shelf +24, notch), MID != SIDE", b,
                       makeSignal (fs, seconds, [] (double t, std::mt19937& rng) {
                           std::uniform_real_distribution<double> u (-0.05, 0.05);
                           const double low = 0.1 * std::sin (2 * pi * 25 * t);
                           return std::pair { low + u (rng), 0.8 * low + u (rng) }; }) });
    }
    // 3. Transients: kick-like bursts every 250 ms + clicks; different banks.
    {
        Banks b { flatBank(), flatBank(), flatBank() };
        b.stereo.bands[1] = { 80.0, 6.0, 1.0, false, FilterType::bell };
        b.mid.bands[1] = { 60.0, 12.0, 4.0, false, FilterType::bell };
        b.side.bands[3] = { 200.0, -12.0, 2.0, false, FilterType::bell };
        b.side.bands[6] = { 8000.0, 9.0, 0.7, false, FilterType::highShelf };
        v.push_back ({ "transients (kicks + clicks), STEREO / MID / SIDE all different", b,
                       makeSignal (fs, seconds, [fs] (double t, std::mt19937&) {
                           const double tk = std::fmod (t, 0.25);
                           const double kick = 0.6 * std::exp (-tk / 0.06) * std::sin (2 * pi * (50 + 80 * std::exp (-tk / 0.02)) * tk);
                           const double click = (std::fmod (t + 0.125, 0.25) < 1.0 / fs) ? 0.5 : 0.0;
                           return std::pair { kick + click, kick - 0.5 * click }; }) });
    }
    // 4. Dynamics: bell 100 Hz, threshold -30, range 12, attack 100 ms, release 1000 ms, copied banks; tremolo tone.
    {
        Banks b { flatBank(), flatBank(), flatBank() };
        b.stereo.bands[2] = { 100.0, 0.0, 2.0, false, FilterType::bell };
        b.stereo.dynamics[2] = { true, -30.0, 12.0, 100.0, 1000.0 };
        b.mid = b.side = b.stereo;
        v.push_back ({ "dynamics 100 Hz (attack 100 ms, release 1 s), copied banks", b,
                       makeSignal (fs, seconds, [] (double t, std::mt19937&) {
                           const double x = 0.3 * (0.6 + 0.4 * std::sin (2 * pi * 0.7 * t)) * std::sin (2 * pi * 100 * t);
                           return std::pair { x, 0.9 * x }; }) });
    }
    return v;
}

void transitionsReport()
{
    const double fs = 48000.0;
    const int sw = (int) (1.5 * fs);
    std::printf ("STEREO -> M/S at 1.5 s, 48 kHz, stereo. Error against the ideal change (both paths in steady state, crossfaded)\n");
    std::printf ("fade 20 ms in all policies; 'settle' = time after the end of the fade with |error| > -80 dBFS\n\n");

    for (auto& sc : scenarios (fs))
    {
        std::printf ("== %s\n", sc.name.c_str());
        const auto a = runPath (sc.banks, false, sc.input, fs);
        const auto b = runPath (sc.banks, true, sc.input, fs);
        const auto peak = std::max (peakAbs (a), peakAbs (b));
        const int fade = (int) std::lround (0.02 * fs);
        std::printf ("   %-16s %12s %12s %14s %12s\n", "policy", "peak dBFS", "peak rel dB", "5ms RMS dev dB", "settle ms");

        struct P { const char* name; double prewarm; };
        for (const auto& p : { P { "cold", 0.0 }, P { "prewarm 20 ms", 0.02 }, P { "prewarm 80 ms", 0.08 }, P { "prewarm 320 ms", 0.32 } })
        {
            const int fadeStart = sw + (int) std::lround (p.prewarm * fs);
            const auto incoming = runPath (sc.banks, true, sc.input, fs, sw);
            const auto got = crossfade (a, incoming, fadeStart, fade);
            const auto ideal = crossfade (a, b, fadeStart, fade);
            const auto e = compare (got, ideal, sw, fadeStart + fade, fs);
            std::printf ("   %-16s %12.1f %12.1f %14.2f %12.1f\n", p.name, db (e.peak), db (e.peak / peak), e.worstRmsDevDb, e.settleMs);
        }
        {
            const auto got = runEngine (sc.banks, sc.input, fs, 0.02, { { sw, StereoMode::midSide } });
            const auto ideal = crossfade (a, b, sw, fade);
            const auto e = compare (got, ideal, 0, sw + fade, fs);
            std::printf ("   %-16s %12.1f %12.1f %14.2f %12.1f\n", "engine (standby)", db (e.peak), db (e.peak / peak), e.worstRmsDevDb, e.settleMs);
        }

        // The ideal change itself, by fade length (and a hard switch).
        std::printf ("   ideal change by fade length: 4th-diff peak during the fade vs steady (dB), worst 5 ms RMS deviation from interpolated RMS (dB)\n");
        const double steady = std::max (fourthDiffMax (a, sw - (int) fs / 2, sw), fourthDiffMax (b, sw + fade * 4, sw + fade * 4 + (int) fs / 2));
        for (const double f : { 1.0 / fs, 0.005, 0.01, 0.02, 0.04 })
        {
            const int len = std::max (1, (int) std::lround (f * fs));
            const auto ideal = crossfade (a, b, sw, len);
            const double click = fourthDiffMax (ideal, sw - 4, sw + len + 4);
            // RMS deviation against the RMS interpolated between both paths.
            const int w = (int) std::lround (0.005 * fs);
            double worst = 0.0;
            for (int start = sw - w; start < sw + len + w; start += std::max (1, w / 4))
            {
                double ei = 0.0, ea = 0.0, eb = 0.0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = start; i < start + w; ++i)
                    {
                        ei += (double) ideal[(size_t) ch][(size_t) i] * ideal[(size_t) ch][(size_t) i];
                        ea += (double) a[(size_t) ch][(size_t) i] * a[(size_t) ch][(size_t) i];
                        eb += (double) b[(size_t) ch][(size_t) i] * b[(size_t) ch][(size_t) i];
                    }
                const double mid = (double) (start + w / 2 - sw) / len;
                const double wm = std::clamp (mid, 0.0, 1.0);
                const double expected = std::sqrt (ea) + wm * (std::sqrt (eb) - std::sqrt (ea));
                if (expected > 1.0e-9)
                    worst = std::max (worst, std::abs (20.0 * std::log10 (std::sqrt (ei) / expected)));
            }
            std::printf ("     fade %6.1f ms: click %+7.1f dB   RMS dev %6.2f dB\n", 1000.0 * len / fs, db (click / steady), worst);
        }
        std::printf ("\n");
    }

    // Fast toggling on the engine: every 30 ms and every 5 ms (faster than
    // the fade) for 1 s, then M/S. Every output sample must lie between
    // the two steady paths (convex combination), and after the last fade
    // the output must be the steady M/S path exactly (no permanent gain
    // change, no latency).
    std::printf ("== fast toggling (engine), extreme-EQ scenario\n");
    auto sc = scenarios (fs)[1];
    const auto a = runPath (sc.banks, false, sc.input, fs);
    const auto b = runPath (sc.banks, true, sc.input, fs);
    for (const double period : { 0.030, 0.005 })
        for (const int block : { 1, 64, 256, 1000 })
        {
            std::vector<std::pair<int, StereoMode>> sws;
            bool ms = true;
            const int step = (int) std::lround (period * fs);
            for (int t = sw; t < sw + (int) fs; t += step, ms = ! ms)
                sws.push_back ({ t, ms ? StereoMode::midSide : StereoMode::stereo });
            if (sws.back().second != StereoMode::midSide)
                sws.push_back ({ sws.back().first + step, StereoMode::midSide });
            const auto got = runEngine (sc.banks, sc.input, fs, 0.02, sws, block);
            double outside = 0.0, after = 0.0;
            bool finite = true;
            const int end = sws.back().first + (int) std::lround (0.02 * fs);
            for (int ch = 0; ch < 2; ++ch)
                for (size_t i = 0; i < got[0].size(); ++i)
                {
                    const double y = got[(size_t) ch][i], lo = std::min (a[(size_t) ch][i], b[(size_t) ch][i]), hi = std::max (a[(size_t) ch][i], b[(size_t) ch][i]);
                    finite = finite && std::isfinite (y);
                    outside = std::max (outside, std::max (lo - y, y - hi));
                    if ((int) i >= end)
                        after = std::max (after, std::abs (y - (double) b[(size_t) ch][i]));
                }
            std::printf ("   toggle every %4.0f ms, host block %4d: finite %s, max outside [A,B] %.3g, after the last fade |out - M/S| max %.3g\n",
                         1000 * period, block, finite ? "yes" : "NO", std::max (0.0, outside), after);
        }
}

//==============================================================================
void crossfadeAnalysis()
{
    const double fs = 48000.0;
    const int sw = (int) (1.5 * fs), fade = (int) std::lround (0.02 * fs);
    std::printf ("Ideal STEREO -> M/S change at 1.5 s, linear 20 ms fade, 48 kHz.\n");
    std::printf ("dev = 20 log10 (RMS(out) / RMS interpolated between both paths), windows centred inside the fade, step = window / 8\n\n");
    for (auto& sc : scenarios (fs))
    {
        const auto a = runPath (sc.banks, false, sc.input, fs);
        const auto b = runPath (sc.banks, true, sc.input, fs);
        const auto out = crossfade (a, b, sw, fade);
        std::printf ("== %s\n", sc.name.c_str());

        // Correlation of both paths over the fade (both channels).
        double pa = 0, pb = 0, cab = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = sw; i < sw + fade; ++i)
            {
                const double x = a[(size_t) ch][(size_t) i], y = b[(size_t) ch][(size_t) i];
                pa += x * x; pb += y * y; cab += x * y;
            }
        const double rho = cab / std::sqrt (pa * pb);
        // Linear crossfade at w = 1/2: P = (Pa + Pb + 2 rho sqrt(Pa Pb)) / 4,
        // against the interpolated RMS ((sqrt Pa + sqrt Pb) / 2)^2.
        const double predicted = 10 * std::log10 ((pa + pb + 2 * rho * std::sqrt (pa * pb)) / std::pow (std::sqrt (pa) + std::sqrt (pb), 2));
        std::printf ("   paths over the fade: RMS A %.1f dBFS, RMS B %.1f dBFS, correlation rho = %.3f -> predicted level at mid-fade %+.2f dB\n",
                     10 * std::log10 (pa / (2 * fade)), 10 * std::log10 (pb / (2 * fade)), rho, predicted);

        for (const double wsec : { 0.005, 0.020, 0.050 })
        {
            const int w = (int) std::lround (wsec * fs);
            double worst = 0, worstT = 0, noise = 0;
            for (int centre = sw; centre <= sw + fade; centre += std::max (1, w / 8))
            {
                const int start = centre - w / 2;
                double ei = 0, ea = 0, eb = 0, ea2 = 0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = start; i < start + w; ++i)
                    {
                        ei += (double) out[(size_t) ch][(size_t) i] * out[(size_t) ch][(size_t) i];
                        ea += (double) a[(size_t) ch][(size_t) i] * a[(size_t) ch][(size_t) i];
                        eb += (double) b[(size_t) ch][(size_t) i] * b[(size_t) ch][(size_t) i];
                        const double s2 = a[(size_t) ch][(size_t) (i + std::max (1, w / 8))];
                        ea2 += s2 * s2;
                    }
                const double wc = std::clamp ((double) (centre - sw) / fade, 0.0, 1.0);
                const double expected = std::sqrt (ea) + wc * (std::sqrt (eb) - std::sqrt (ea));
                const double dev = 20 * std::log10 (std::sqrt (ei) / expected);
                if (std::abs (dev) > std::abs (worst)) { worst = dev; worstT = 1000.0 * (centre - sw) / fs; }
                noise = std::max (noise, std::abs (10 * std::log10 (ea2 / ea)));
            }
            std::printf ("   window %4.0f ms: worst deviation %+.2f dB at %4.1f ms into the fade; the measure's own noise on path A: %.2f dB\n",
                         1000 * wsec, worst, worstT, noise);
        }
        std::printf ("\n");
    }
}

//==============================================================================
struct Load
{
    double load;
};

Banks dynamicBanks (bool stereoActive, bool msActive)
{
    Banks b { flatBank(), flatBank(), flatBank() };
    auto fill = [] (Bank& bank, double shift)
    {
        const double f[] = { 1000, 60, 150, 400, 2500, 5000, 9000, 14000 };
        for (int i = 0; i < 8; ++i)
        {
            bank.bands[(size_t) i] = { f[i] * shift, (i % 2 ? 4.0 : -4.0), 2.0, false, FilterType::bell };
            bank.dynamics[(size_t) i] = { true, -40.0, 8.0, 5.0, 80.0 };
        }
    };
    if (stereoActive) fill (b.stereo, 1.0);
    if (msActive) { fill (b.mid, 1.1); fill (b.side, 0.9); }
    return b;
}

double measure (int instances, double fs, const std::function<void (int, float**, int)>& process, int blocks, int block)
{
    std::vector<float> l ((size_t) block), r ((size_t) block);
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> u (-0.5f, 0.5f);
    std::vector<std::array<std::vector<float>, 2>> inputs (16);
    for (auto& in : inputs)
        for (auto& c : in) { c.resize ((size_t) block); for (auto& x : c) x = u (rng); }
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < blocks; ++k)
        for (int inst = 0; inst < instances; ++inst)
        {
            const auto& in = inputs[(size_t) (k % 16)];
            std::copy (in[0].begin(), in[0].end(), l.begin());
            std::copy (in[1].begin(), in[1].end(), r.begin());
            float* ch[2] = { l.data(), r.data() };
            process (inst, ch, block);
        }
    const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    return elapsed / ((double) blocks * block / fs);
}

void cpuReport()
{
#ifndef NDEBUG
    std::printf ("WARNING: Debug build, numbers are not meaningful\n");
#endif
    const int block = 256;
    std::printf ("Load = CPU seconds / audio seconds, one core; per instance; median of 7 interleaved runs of 2 s\n");
    std::printf ("Signal: stereo noise -6 dBFS peak, 8 dynamic Bells per active bank (threshold -40, range 8, gains moving)\n\n");

    struct Case { const char* name; bool stereoActive, msActive; StereoMode mode; bool toggling; };
    const Case cases[] = {
        { "pre-E4b: STEREO core alone (reference)", true, false, StereoMode::stereo, false },
        { "engine Stereo, MID/SIDE flat",           true, false, StereoMode::stereo, false },
        { "engine Stereo, MID/SIDE active (standby)", true, true, StereoMode::stereo, false },
        { "engine M/S, STEREO flat",                false, true, StereoMode::midSide, false },
        { "engine M/S, STEREO active (standby)",    true, true, StereoMode::midSide, false },
        { "engine all flat (Stereo)",               false, false, StereoMode::stereo, false },
        { "engine toggling every 100 ms, all active", true, true, StereoMode::stereo, true },
    };

    for (const double fs : { 44100.0, 48000.0, 96000.0 })
        for (const int instances : { 1, 8 })
        {
            std::printf ("== %.1f kHz, %d instance%s\n", fs / 1000.0, instances, instances > 1 ? "s" : "");
            const int blocks = (int) (2.0 * fs / block);
            std::vector<std::vector<double>> runs (std::size (cases));
            for (int run = 0; run < 8; ++run)
                for (size_t c = 0; c < std::size (cases); ++c)
                {
                    const auto& k = cases[c];
                    const auto banks = dynamicBanks (k.stereoActive, k.msActive);
                    double load = 0.0;
                    if (c == 0)
                    {
                        std::vector<std::unique_ptr<EqCore>> cores;
                        for (int i = 0; i < instances; ++i) { cores.push_back (std::make_unique<EqCore>()); apply (*cores.back(), banks.stereo); cores.back()->prepare (fs, 2); }
                        load = measure (instances, fs, [&] (int i, float** ch, int n) { cores[(size_t) i]->process (ch, 2, n); }, blocks, block);
                    }
                    else
                    {
                        std::vector<std::unique_ptr<StereoModeEngine>> engines;
                        for (int i = 0; i < instances; ++i) { engines.push_back (std::make_unique<StereoModeEngine>()); apply (*engines.back(), banks); engines.back()->setMode (k.mode); engines.back()->prepare (fs, 2); }
                        int counter = 0;
                        const int togglePeriod = (int) (0.1 * fs / block);
                        load = measure (instances, fs, [&] (int i, float** ch, int n)
                        {
                            if (k.toggling && i == 0 && ++counter % togglePeriod == 0)
                                for (auto& e : engines) e->setMode (e->getRequestedMode() == StereoMode::stereo ? StereoMode::midSide : StereoMode::stereo);
                            engines[(size_t) i]->process (ch, 2, n);
                        }, blocks, block);
                    }
                    if (run > 0) // first run: warm-up
                        runs[c].push_back (load / instances);
                }
            std::vector<double> med;
            for (auto& r : runs) { std::sort (r.begin(), r.end()); med.push_back (r[r.size() / 2]); }
            for (size_t c = 0; c < std::size (cases); ++c)
                std::printf ("   %-44s %6.3f %%   x%.2f\n", cases[c].name, 100.0 * med[c], med[c] / med[0]);
        }
}
} // namespace

int main (int argc, char** argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "--transitions") { transitionsReport(); return 0; }
    if (mode == "--cpu") { cpuReport(); return 0; }
    if (mode == "--crossfade-analysis") { crossfadeAnalysis(); return 0; }
    std::printf ("usage: labq8_ms_bench --transitions | --crossfade-analysis | --cpu\n");
    return 2;
}
