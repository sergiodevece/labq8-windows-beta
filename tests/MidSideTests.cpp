// E4b: the MID/SIDE engine (dsp::StereoModeEngine) and its use by the
// processor. Letters refer to the E4b test list (docs/MIDSIDE_E4B.md):
// A STEREO identity, B flat transparency, C independence, D mono, E
// antiphase, F gain / normalisation, G bypass and mode changes, H state,
// I stress / automation / concurrency. CPU (J) is labq8_ms_bench --cpu;
// compatibility (K) is tests/compat.

#pragma clang diagnostic ignored "-Wfloat-equal" // bit-exactness is a tested property

#include "AllocationGuard.h"
#include "TestHarness.h"
#include "TestSignals.h"

#include "dsp/EqCore.h"
#include "dsp/MidSide.h"
#include "dsp/StereoModeEngine.h"
#include "parameters/ParameterIDs.h"
#include "plugin/PluginProcessor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <random>
#include <thread>

using labq8::LabQ8AudioProcessor;
using labq8::test::str;
namespace params = labq8::params;
namespace dsp = labq8::dsp;
using dsp::StereoBank;
using dsp::StereoMode;

namespace
{
constexpr double pi = 3.14159265358979323846;

struct Bank
{
    std::array<dsp::BandSettings, 8> bands {};
    std::array<dsp::DynamicSettings, 8> dynamics {};
};

Bank flatBank()
{
    Bank b;
    for (int i = 0; i < 8; ++i)
        b.bands[(size_t) i] = { params::defaultFrequencyHz[(size_t) i], 0.0, 1.0, false, dsp::FilterType::bell };
    return b;
}

// Random but reproducible: all six types, bypass, dynamics on half the bands.
Bank randomBank (unsigned seed, bool withDynamics = true)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    Bank b;
    for (int i = 0; i < 8; ++i)
    {
        auto& s = b.bands[(size_t) i];
        s.type = dsp::filterTypeFromIndex ((int) (u (rng) * 5.999));
        s.frequencyHz = 30.0 * std::pow (500.0, u (rng));
        s.gainDb = -12.0 + 24.0 * u (rng);
        s.q = 0.3 * std::pow (25.0, u (rng));
        s.bypassed = u (rng) < 0.1;
        auto& d = b.dynamics[(size_t) i];
        d.enabled = withDynamics && u (rng) < 0.5;
        d.thresholdDb = -50.0 + 40.0 * u (rng);
        d.rangeDb = 3.0 + 12.0 * u (rng);
        d.attackMs = 1.0 + 49.0 * u (rng);
        d.releaseMs = 20.0 + 380.0 * u (rng);
    }
    return b;
}

void apply (dsp::EqCore& core, const Bank& b)
{
    for (int i = 0; i < 8; ++i)
    {
        core.setBandTargets (i, b.bands[(size_t) i]);
        core.setBandDynamics (i, b.dynamics[(size_t) i]);
    }
}

void apply (dsp::StereoModeEngine& e, StereoBank bank, const Bank& b)
{
    for (int i = 0; i < 8; ++i)
    {
        e.setBandTargets (bank, i, b.bands[(size_t) i]);
        e.setBandDynamics (bank, i, b.dynamics[(size_t) i]);
    }
}

using Stereo = std::array<std::vector<float>, 2>;

Stereo makeInput (int n, double fs, unsigned seed, double correlation = 0.6)
{
    Stereo s { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> u (-0.25, 0.25);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const double common = u (rng) + 0.3 * std::sin (2 * pi * 110.0 * t) * (0.6 + 0.4 * std::sin (2 * pi * 2.0 * t));
        const double l = u (rng), r = u (rng);
        s[0][(size_t) i] = (float) (correlation * common + (1 - correlation) * l + 0.2 * std::sin (2 * pi * 3100.0 * t));
        s[1][(size_t) i] = (float) (correlation * common + (1 - correlation) * r);
    }
    return s;
}

// Engine over a whole signal in blocks of `block` (in place).
Stereo runEngine (dsp::StereoModeEngine& e, Stereo x, int channels, int block)
{
    const int n = (int) x[0].size();
    for (int pos = 0; pos < n; pos += block)
    {
        float* ch[2] = { x[0].data() + pos, x[1].data() + pos };
        e.process (ch, channels, std::min (block, n - pos));
    }
    return x;
}

// The reference M/S implementation: separate, fresh EqCores, coded per call.
Stereo referenceMidSide (const Bank& mid, const Bank& side, Stereo x, double fs, int block)
{
    dsp::EqCore m, s;
    apply (m, mid);
    apply (s, side);
    m.prepare (fs, 1);
    s.prepare (fs, 1);
    const int n = (int) x[0].size();
    std::vector<float> mb ((size_t) block), sb ((size_t) block);
    for (int pos = 0; pos < n; pos += block)
    {
        const int k = std::min (block, n - pos);
        dsp::midside::encode (x[0].data() + pos, x[1].data() + pos, mb.data(), sb.data(), k);
        float* mp[1] = { mb.data() };
        float* sp[1] = { sb.data() };
        m.process (mp, 1, k);
        s.process (sp, 1, k);
        dsp::midside::decode (mb.data(), sb.data(), x[0].data() + pos, x[1].data() + pos, k);
    }
    return x;
}

Stereo referenceStereo (const Bank& bank, Stereo x, double fs, int channels, int block)
{
    dsp::EqCore c;
    apply (c, bank);
    c.prepare (fs, channels);
    const int n = (int) x[0].size();
    for (int pos = 0; pos < n; pos += block)
    {
        float* ch[2] = { x[0].data() + pos, x[1].data() + pos };
        c.process (ch, channels, std::min (block, n - pos));
    }
    return x;
}

bool same (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), sizeof (float) * a.size()) == 0;
}

bool same (const Stereo& a, const Stereo& b, int channels = 2)
{
    for (int ch = 0; ch < channels; ++ch)
        if (! same (a[(size_t) ch], b[(size_t) ch]))
            return false;
    return true;
}

double maxAbs (const std::vector<float>& v)
{
    double m = 0.0;
    for (const auto x : v)
        m = std::max (m, (double) std::abs (x));
    return m;
}

//==============================================================================
// Processor helpers.

void setParam (LabQ8AudioProcessor& p, const char* id, float value)
{
    auto* parameter = p.getValueTreeState().getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void setBank (LabQ8AudioProcessor& p, params::Bank bank, const Bank& b)
{
    const auto ids = params::bankOf (bank);
    for (int i = 0; i < 8; ++i)
    {
        const auto& s = b.bands[(size_t) i];
        const auto& d = b.dynamics[(size_t) i];
        const auto& bi = ids.bands[(size_t) i];
        const auto& di = ids.dynamics[(size_t) i];
        setParam (p, bi.frequency, (float) s.frequencyHz);
        setParam (p, bi.gain, (float) s.gainDb);
        setParam (p, bi.q, (float) s.q);
        setParam (p, bi.bypass, s.bypassed ? 1.0f : 0.0f);
        setParam (p, bi.type, (float) (int) s.type);
        setParam (p, di.enabled, d.enabled ? 1.0f : 0.0f);
        setParam (p, di.threshold, (float) d.thresholdDb);
        setParam (p, di.range, (float) d.rangeDb);
        setParam (p, di.attack, (float) d.attackMs);
        setParam (p, di.release, (float) d.releaseMs);
    }
}

void setMode (LabQ8AudioProcessor& p, StereoMode m)
{
    setParam (p, params::procModeID, m == StereoMode::midSide ? 1.0f : 0.0f);
}

bool setLayout (LabQ8AudioProcessor& p, int channels)
{
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    return p.setBusesLayout (layout);
}

// Renders x through p (prepared here), in host blocks; `before (pos)` runs
// before every block (automation).
Stereo render (LabQ8AudioProcessor& p, const Stereo& x, int channels, double fs, int block,
               const std::function<void (int)>& before = {})
{
    setLayout (p, channels);
    p.prepareToPlay (fs, block);
    Stereo out = x;
    const int n = (int) x[0].size();
    juce::MidiBuffer midi;
    for (int pos = 0; pos < n; pos += block)
    {
        if (before)
            before (pos);
        const int k = std::min (block, n - pos);
        juce::AudioBuffer<float> buffer (channels, k);
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom (ch, 0, x[(size_t) ch].data() + pos, k);
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < channels; ++ch)
            std::memcpy (out[(size_t) ch].data() + pos, buffer.getReadPointer (ch), sizeof (float) * (size_t) k);
    }
    return out;
}

const int kBlocks[] = { 1, 37, 256, 1000 };
} // namespace

//==============================================================================
// A. STEREO identity.

LQ_TEST ("e4b A: in Stereo the engine is bit-identical to the bare STEREO core, whatever MID / SIDE hold (standby running), mono and stereo, 4 block sizes")
{
    const double fs = 48000.0;
    const auto x = makeInput (24000, fs, 1);
    for (unsigned seed = 1; seed <= 4; ++seed)
        for (const int channels : { 1, 2 })
            for (const int block : kBlocks)
            {
                const auto stereo = randomBank (seed);
                dsp::StereoModeEngine e;
                apply (e, StereoBank::stereo, stereo);
                apply (e, StereoBank::mid, randomBank (seed + 100));
                apply (e, StereoBank::side, randomBank (seed + 200));
                e.setMode (StereoMode::stereo);
                e.prepare (fs, channels);
                const auto got = runEngine (e, x, channels, block);
                LQ_CHECK (same (got, referenceStereo (stereo, x, fs, channels, 256), channels),
                          str ("seed ", seed, ", ", channels, " ch, block ", block, ": STEREO path differs from the bare core"));
            }
}

//==============================================================================
// B. Flat transparency.

LQ_TEST ("e4b B: M/S with flat MID and SIDE banks is bit-exact transparent (stereo and mono, 3 rates, 4 block sizes, STEREO bank active or flat)")
{
    for (const auto fs : labq8::test::signals::sampleRates)
        for (const int channels : { 1, 2 })
            for (const int block : kBlocks)
                for (const bool stereoActive : { false, true })
                {
                    const auto x = makeInput (12000, fs, 2);
                    LabQ8AudioProcessor p;
                    if (stereoActive)
                        setBank (p, params::Bank::stereo, randomBank (9));
                    setMode (p, StereoMode::midSide);
                    const auto got = render (p, x, channels, fs, block);
                    LQ_CHECK (same (got, x, channels), str (fs, " Hz, ", channels, " ch, block ", block, ", STEREO ", stereoActive ? "active" : "flat", ": not bit-exact"));
                    LQ_CHECK (p.getEngineForTesting().isMidSideFlat(), "flat path in use");
                }

    // Flat also covers bypassed bands of every type (Bell / shelves at 0 dB,
    // cuts and notch on the dry path).
    LabQ8AudioProcessor p;
    Bank bypassed = randomBank (12, false);
    for (auto& s : bypassed.bands)
        s.bypassed = true;
    setBank (p, params::Bank::mid, bypassed);
    setBank (p, params::Bank::side, bypassed);
    setMode (p, StereoMode::midSide);
    const auto x = makeInput (144000, 48000.0, 3);
    const auto got = render (p, x, 2, 48000.0, 512);
    // After the 20 ms bypass ramps / crossfades and the decay of the bands'
    // residual state (at their pole rate: up to about 1.6 s for the lowest,
    // narrowest band here) the path is flat again and exactly transparent.
    std::vector<float> tailIn (x[0].begin() + 120000, x[0].end()), tailOut (got[0].begin() + 120000, got[0].end());
    LQ_CHECK (same (tailIn, tailOut), "all bands bypassed: transparent once the bypass ramps have finished");
}

LQ_TEST ("e4b B: entering the flat path when a band returns to 0 dB gives exactly the input, at the same sample for every host block size")
{
    const double fs = 48000.0;
    const auto x = makeInput (48000, fs, 4);
    Bank mid = flatBank();
    mid.bands[3] = { 250.0, 9.0, 2.0, false, dsp::FilterType::bell };
    Stereo reference;
    for (const int block : kBlocks)
    {
        dsp::StereoModeEngine e;
        apply (e, StereoBank::mid, mid);
        e.setMode (StereoMode::midSide);
        e.prepare (fs, 2);
        Stereo y = x;
        // 2/3 s active, then back to 0 dB at a block start: sample 32000
        // (a boundary for 1, 256 and 1000), 32005 for 37.
        const int change = block == 37 ? 32005 : 32000;
        for (int pos = 0; pos < 48000;)
        {
            if (pos == change)
            {
                mid.bands[3].gainDb = 0.0;
                apply (e, StereoBank::mid, mid);
                mid.bands[3].gainDb = 9.0;
            }
            int k = std::min (block, 48000 - pos);
            float* ch[2] = { y[0].data() + pos, y[1].data() + pos };
            e.process (ch, 2, k);
            pos += k;
        }
        // Flat again at the end, and exactly transparent there.
        std::vector<float> tailIn (x[0].begin() + 40000, x[0].end()), tailOut (y[0].begin() + 40000, y[0].end());
        LQ_CHECK (e.isMidSideFlat() && same (tailIn, tailOut), str ("block ", block, ": not transparent after returning to 0 dB"));
        if (block != 37)
        {
            if (reference[0].empty())
                reference = y;
            else
                LQ_CHECK (same (y, reference), str ("block ", block, ": output depends on the host block size"));
        }
    }
}

//==============================================================================
// C. Independence, against a reference implementation.

LQ_TEST ("e4b C: M/S output equals the reference (encode, two separate EqCores, decode) bit for bit: static and dynamic random banks, 3 rates, 4 block sizes")
{
    for (const auto fs : labq8::test::signals::sampleRates)
        for (unsigned seed = 1; seed <= 3; ++seed)
        {
            const auto x = makeInput ((int) (0.5 * fs), fs, seed + 10);
            const auto mid = randomBank (seed + 300), side = randomBank (seed + 400);
            const auto ref = referenceMidSide (mid, side, x, fs, 256);
            for (const int block : kBlocks)
            {
                dsp::StereoModeEngine e;
                apply (e, StereoBank::stereo, randomBank (seed + 500)); // standby, never heard
                apply (e, StereoBank::mid, mid);
                apply (e, StereoBank::side, side);
                e.setMode (StereoMode::midSide);
                e.prepare (fs, 2);
                LQ_CHECK (same (runEngine (e, x, 2, block), ref), str (fs, " Hz, seed ", seed, ", block ", block, ": differs from the reference"));
            }
        }
}

LQ_TEST ("e4b C: banks are independent: changing every MID setting leaves the SIDE core's trajectory bit-identical, and vice versa; detectors read their own signal")
{
    const double fs = 48000.0;
    const auto x = makeInput (24000, fs, 21);
    auto trajectory = [&] (const Bank& mid, const Bank& side, StereoBank watch)
    {
        dsp::StereoModeEngine e;
        apply (e, StereoBank::mid, mid);
        apply (e, StereoBank::side, side);
        e.setMode (StereoMode::midSide);
        e.prepare (fs, 2);
        std::vector<double> t;
        Stereo y = x;
        for (int pos = 0; pos < 24000; pos += 64)
        {
            float* ch[2] = { y[0].data() + pos, y[1].data() + pos };
            e.process (ch, 2, 64);
            const auto& c = e.getCore (watch);
            for (int b = 0; b < 8; ++b)
            {
                t.push_back (c.getBand (b).getCurrentGainDb());
                t.push_back (c.getDynamics (b).getGainReductionDb());
                t.push_back (c.getDynamics (b).getDetectorLevelDb());
                const auto k = c.getBand (b).getCoefficients();
                t.push_back (k.b0); t.push_back (k.a1);
            }
        }
        return t;
    };
    const auto midA = randomBank (31), midB = randomBank (32), sideA = randomBank (33), sideB = randomBank (34);
    LQ_CHECK (trajectory (midA, sideA, StereoBank::side) == trajectory (midB, sideA, StereoBank::side), "SIDE depends on MID settings");
    LQ_CHECK (trajectory (midA, sideA, StereoBank::mid) == trajectory (midA, sideB, StereoBank::mid), "MID depends on SIDE settings");

    // Detectors: a tone only in M reduces MID only; only in S, SIDE only.
    Bank dynamic = flatBank();
    dynamic.bands[2] = { 200.0, 0.0, 2.0, false, dsp::FilterType::bell };
    dynamic.dynamics[2] = { true, -30.0, 10.0, 2.0, 50.0 };
    for (const bool inMid : { true, false })
    {
        dsp::StereoModeEngine e;
        apply (e, StereoBank::mid, dynamic);
        apply (e, StereoBank::side, dynamic);
        e.setMode (StereoMode::midSide);
        e.prepare (fs, 2);
        Stereo y { std::vector<float> (24000), std::vector<float> (24000) };
        for (int i = 0; i < 24000; ++i)
        {
            const auto v = (float) (0.3 * std::sin (2 * pi * 200.0 * i / fs));
            y[0][(size_t) i] = v;
            y[1][(size_t) i] = inMid ? v : -v;
        }
        runEngine (e, y, 2, 256);
        const auto rm = e.getCore (StereoBank::mid).getDynamics (2).getGainReductionDb();
        const auto rs = e.getCore (StereoBank::side).getDynamics (2).getGainReductionDb();
        LQ_CHECK (inMid ? (rm > 5.0 && rs == 0.0) : (rs > 5.0 && rm == 0.0),
                  str ("tone in ", inMid ? "M" : "S", ": MID reduction ", rm, ", SIDE reduction ", rs));
    }
}

//==============================================================================
// D, E. Mono and antiphase.

LQ_TEST ("e4b D: a mono signal (L = R) never gets SIDE content: L' = R' bit for bit with any SIDE bank, and it equals the STEREO path with the same bank")
{
    const double fs = 48000.0;
    auto x = makeInput (24000, fs, 41);
    x[1] = x[0];
    for (unsigned seed = 1; seed <= 4; ++seed)
    {
        const auto mid = randomBank (seed + 600);
        Bank extremeSide = randomBank (seed + 700);
        extremeSide.bands[0] = { 100.0, 24.0, 18.0, false, dsp::FilterType::bell };
        extremeSide.bands[1] = { 8000.0, 24.0, 1.0, false, dsp::FilterType::highShelf };
        dsp::StereoModeEngine e;
        apply (e, StereoBank::mid, mid);
        apply (e, StereoBank::side, extremeSide);
        e.setMode (StereoMode::midSide);
        e.prepare (fs, 2);
        const auto y = runEngine (e, x, 2, 256);
        LQ_CHECK (same (y[0], y[1]), str ("seed ", seed, ": L' != R' for a mono signal"));
        LQ_CHECK (e.getCore (StereoBank::side).getDynamics (0).getDetectorLevelDb() <= dsp::DynamicDetector::levelFloorDb,
                  "SIDE detector must see silence");

        // Same bank in STEREO (L = R, linked detector) gives the same samples.
        const auto stereo = referenceStereo (mid, x, fs, 2, 256);
        LQ_CHECK (same (y, stereo), str ("seed ", seed, ": M/S MID differs from STEREO with the same bank on a mono signal"));
    }
}

LQ_TEST ("e4b D: on a mono bus (1 channel) MID processes the channel and SIDE has no effect")
{
    const double fs = 48000.0;
    const auto x = makeInput (24000, fs, 42);
    const auto mid = randomBank (51);
    Stereo first;
    for (unsigned sideSeed : { 52u, 53u })
    {
        dsp::StereoModeEngine e;
        apply (e, StereoBank::mid, mid);
        apply (e, StereoBank::side, randomBank (sideSeed));
        e.setMode (StereoMode::midSide);
        e.prepare (fs, 1);
        const auto y = runEngine (e, x, 1, 333);
        LQ_CHECK (same (y[0], referenceStereo (mid, x, fs, 1, 256)[0]), "mono bus: MID differs from the bank applied to the channel");
        if (first[0].empty())
            first = y;
        else
            LQ_CHECK (same (first[0], y[0]), "mono bus: SIDE settings changed the output");
    }
}

LQ_TEST ("e4b E: an antiphase signal (R = -L) never gets MID content: L' = -R' bit for bit, and MID settings change nothing")
{
    const double fs = 48000.0;
    auto x = makeInput (24000, fs, 43);
    for (size_t i = 0; i < x[0].size(); ++i)
        x[1][i] = -x[0][i];
    Stereo first;
    for (unsigned midSeed : { 61u, 62u, 63u })
    {
        dsp::StereoModeEngine e;
        apply (e, StereoBank::mid, randomBank (midSeed));
        apply (e, StereoBank::side, randomBank (64));
        e.setMode (StereoMode::midSide);
        e.prepare (fs, 2);
        const auto y = runEngine (e, x, 2, 256);
        bool antiphase = true;
        for (size_t i = 0; i < y[0].size(); ++i)
            antiphase = antiphase && y[0][i] == -y[1][i];
        LQ_CHECK (antiphase, str ("MID seed ", midSeed, ": L' != -R'"));
        LQ_CHECK (e.getCore (StereoBank::mid).getDynamics (0).getDetectorLevelDb() <= dsp::DynamicDetector::levelFloorDb,
                  "MID detector must see silence");
        if (first[0].empty())
            first = y;
        else
            LQ_CHECK (same (first, y), str ("MID seed ", midSeed, ": MID settings changed an antiphase signal"));
    }
}

//==============================================================================
// F. Gain and normalisation.

LQ_TEST ("e4b F: codec levels: L-only / R-only / mono / antiphase sines through MID +6 dB and SIDE -6 dB give the analytic amplitudes (0.01 dB)")
{
    const double fs = 48000.0, f = 1000.0, a = 0.25;
    Bank mid = flatBank(), side = flatBank();
    mid.bands[0] = { f, 6.0, 1.0, false, dsp::FilterType::bell };
    side.bands[0] = { f, -6.0, 1.0, false, dsp::FilterType::bell };
    dsp::EqCore probeM, probeS;
    apply (probeM, mid); apply (probeS, side);
    probeM.prepare (fs, 1); probeS.prepare (fs, 1);
    const double gm = probeM.magnitudeAt (f), gs = probeS.magnitudeAt (f);
    LQ_CHECK (std::abs (20 * std::log10 (gm) - 6.0) < 1e-6 && std::abs (20 * std::log10 (gs) + 6.0) < 1e-6, "probe magnitudes");

    struct Case { const char* name; double l, r; double expectL, expectR; };
    // M = (l + r) / 2, S = (l - r) / 2; L' = gm M + gs S, R' = gm M - gs S.
    const Case cases[] = {
        { "L only",    1, 0,  (gm + gs) / 2, (gm - gs) / 2 },
        { "R only",    0, 1,  (gm - gs) / 2, (gm + gs) / 2 },
        { "mono",      1, 1,  gm, gm },
        { "antiphase", 1, -1, gs, gs },
    };
    for (const auto& c : cases)
    {
        dsp::StereoModeEngine e;
        apply (e, StereoBank::mid, mid);
        apply (e, StereoBank::side, side);
        e.setMode (StereoMode::midSide);
        e.prepare (fs, 2);
        Stereo y { std::vector<float> (48000), std::vector<float> (48000) };
        for (int i = 0; i < 48000; ++i)
        {
            const double v = a * std::sin (2 * pi * f * i / fs);
            y[0][(size_t) i] = (float) (c.l * v);
            y[1][(size_t) i] = (float) (c.r * v);
        }
        y = runEngine (e, y, 2, 256);
        auto amplitude = [] (const std::vector<float>& s) { return maxAbs (std::vector<float> (s.begin() + 24000, s.end())); };
        const double dl = 20 * std::log10 (amplitude (y[0]) / (a * c.expectL));
        const double dr = 20 * std::log10 (amplitude (y[1]) / (a * c.expectR));
        LQ_CHECK (std::abs (dl) < 0.01 && std::abs (dr) < 0.01, str (c.name, ": L error ", dl, " dB, R error ", dr, " dB"));
    }

    // The coded round trip (no processing): within 1 ulp of max(|L|, |R|),
    // exact for mono, antiphase and one-sided pairs (E4a §8).
    std::mt19937 rng (5);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    double worstUlp = 0.0;
    bool exactSpecial = true;
    for (int i = 0; i < 200000; ++i)
    {
        const float l = u (rng), r = (i % 4 == 0) ? l : (i % 4 == 1) ? -l : (i % 4 == 2) ? 0.0f : u (rng);
        float m, s, l2, r2;
        dsp::midside::encode (&l, &r, &m, &s, 1);
        dsp::midside::decode (&m, &s, &l2, &r2, 1);
        const float big = std::max (std::abs (l), std::abs (r));
        if (i % 4 != 3)
            exactSpecial = exactSpecial && l2 == l && r2 == r;
        else if (big > 0.0f)
            worstUlp = std::max (worstUlp, std::max (std::abs ((double) l2 - l), std::abs ((double) r2 - r)) / (std::nextafter (big, 2.0f) - big));
    }
    LQ_CHECK (exactSpecial, "codec round trip must be exact for mono, antiphase and one-sided pairs");
    LQ_CHECK (worstUlp <= 1.0, str ("codec round trip error ", worstUlp, " ulp of the larger channel"));
    ctx.note (str ("codec round trip, random pairs: worst ", worstUlp, " ulp of max(|L|, |R|)"));
}

//==============================================================================
// G. Mode changes and bypass during playback (processor).

LQ_TEST ("e4b G: mode change during playback: identical to Stereo before, 20 ms convex crossfade, exactly M/S after; reversal returns exactly to Stereo; 4 block sizes")
{
    const double fs = 48000.0;
    const auto x = makeInput (48000, fs, 71);
    const auto st = randomBank (72), mid = randomBank (73), side = randomBank (74);
    auto configured = [&] (StereoMode m)
    {
        auto p = std::make_unique<LabQ8AudioProcessor>();
        setBank (*p, params::Bank::stereo, st);
        setBank (*p, params::Bank::mid, mid);
        setBank (*p, params::Bank::side, side);
        setMode (*p, m);
        return p;
    };
    for (const int block : { 64, 256, 333, 1000 })
    {
        const auto a = render (*configured (StereoMode::stereo), x, 2, fs, block);
        const auto b = render (*configured (StereoMode::midSide), x, 2, fs, block);
        const int sw = block * (12000 / block);
        const int fade = 960;

        auto p = configured (StereoMode::stereo);
        const auto c = render (*p, x, 2, fs, block, [&] (int pos) { if (pos == sw) setMode (*p, StereoMode::midSide); });
        bool before = true, after = true, convex = true, formula = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 48000; ++i)
            {
                const float y = c[(size_t) ch][(size_t) i], ya = a[(size_t) ch][(size_t) i], yb = b[(size_t) ch][(size_t) i];
                if (i < sw) before = before && y == ya;
                else if (i >= sw + fade) after = after && y == yb;
                else
                {
                    convex = convex && y >= std::min (ya, yb) && y <= std::max (ya, yb);
                    const double w = (double) (i - sw + 1) / fade;
                    formula = formula && y == (float) (ya + w * ((double) yb - ya));
                }
            }
        LQ_CHECK (before && after && convex && formula, str ("block ", block, ": before ", before, " after ", after, " convex ", convex, " formula ", formula));

        // Back again after 300 samples (inside the fade): reverses in place
        // and returns exactly to Stereo 300 samples later.
        auto q = configured (StereoMode::stereo);
        const int back = sw + block * std::max (1, (300 + block - 1) / block); // a block start about 300 samples later
        const auto d = render (*q, x, 2, fs, block, [&] (int pos)
        {
            if (pos == sw) setMode (*q, StereoMode::midSide);
            if (pos == back) setMode (*q, StereoMode::stereo);
        });
        if (back - sw < fade)
        {
            const int end = back + (back - sw);
            bool returned = true, bounded = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 48000; ++i)
                {
                    const float y = d[(size_t) ch][(size_t) i], ya = a[(size_t) ch][(size_t) i], yb = b[(size_t) ch][(size_t) i];
                    if (i >= end) returned = returned && y == ya;
                    bounded = bounded && y >= std::min (ya, yb) && y <= std::max (ya, yb);
                }
            int first = -1;
            for (int i = end; i < 48000 && first < 0; ++i)
                if (d[0][(size_t) i] != a[0][(size_t) i] || d[1][(size_t) i] != a[1][(size_t) i])
                    first = i;
            LQ_CHECK (returned && bounded, str ("block ", block, ": reversal: back to Stereo exactly ", returned, ", bounded ", bounded,
                                                " (sw ", sw, ", back ", back, ", first differing sample ", first, ")"));
        }
    }
}

LQ_TEST ("e4b G: host bypass and band bypass in M/S; the telemetry shows the STEREO bank only while it is heard")
{
    const double fs = 48000.0;
    const auto x = makeInput (24000, fs, 81);
    LabQ8AudioProcessor p;
    Bank mid = flatBank();
    mid.bands[2] = { 300.0, 0.0, 2.0, false, dsp::FilterType::bell };
    mid.dynamics[2] = { true, -40.0, 9.0, 2.0, 60.0 };
    Bank stereo = mid;
    setBank (p, params::Bank::stereo, stereo);
    setBank (p, params::Bank::mid, mid);
    setMode (p, StereoMode::midSide);
    setLayout (p, 2);
    p.prepareToPlay (fs, 480);
    p.getDynamicsTelemetry().setActive (true);
    labq8::DynamicsTelemetry::Snapshot s;
    p.getDynamicsTelemetry().take (s); // the editor's first read consumes the snapshot prepare() leaves
    juce::MidiBuffer midi;
    auto block = [&] (int pos, bool bypassed)
    {
        juce::AudioBuffer<float> buffer (2, 480);
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, 0, x[(size_t) ch].data() + pos, 480);
        auto copy = buffer;
        if (bypassed) p.processBlockBypassed (buffer, midi); else p.processBlock (buffer, midi);
        bool unchanged = true;
        for (int ch = 0; ch < 2; ++ch)
            unchanged = unchanged && std::memcmp (buffer.getReadPointer (ch), copy.getReadPointer (ch), sizeof (float) * 480) == 0;
        return unchanged;
    };
    for (int pos = 0; pos < 4800; pos += 480)
        block (pos, false);
    p.getDynamicsTelemetry().take (s);
    LQ_CHECK (s.hostBypassed, str ("M/S heard: the STEREO bank's dynamics are not offered (heard ", (int) p.getEngineForTesting().getHeardMode(),
                                   ", fading ", p.getEngineForTesting().isFading(), ", id ", s.id, ", offered ", s.offered, ", reduction b3 ", s.bands[2].reductionDb, ")"));
    LQ_CHECK (block (4800, true), "host bypass: the buffer passes untouched");
    LQ_CHECK (! block (5280, false), "after the host bypass the M/S path processes again");

    // Band bypass of the MID band: transparent again once its 20 ms ramp and
    // reduction fade are over.
    setParam (p, params::bankOf (params::Bank::mid).bands[2].bypass, 1.0f);
    for (int pos = 5760; pos < 14400; pos += 480)
        block (pos, false);
    LQ_CHECK (block (14400, false), "MID band bypassed: transparent");

    setMode (p, StereoMode::stereo);
    for (int pos = 14880; pos < 19200; pos += 480)
        block (pos, false);
    p.getDynamicsTelemetry().take (s);
    LQ_CHECK (! s.hostBypassed && s.bands[2].reductionDb > 0.0f, str ("Stereo heard: STEREO dynamics offered (reduction ", s.bands[2].reductionDb, ")"));
}

//==============================================================================
// H. State.

LQ_TEST ("e4b H: v5 sessions with different settings in the three banks restore their values and sound (restored sound a fixed point, bit for bit; Stereo and M/S, fixture included)")
{
    const double fs = 48000.0;
    const auto x = makeInput (24000, fs, 91);
    for (unsigned seed = 1; seed <= 4; ++seed)
        for (const auto mode : { StereoMode::stereo, StereoMode::midSide })
        {
            LabQ8AudioProcessor a;
            setBank (a, params::Bank::stereo, randomBank (seed + 800));
            setBank (a, params::Bank::mid, randomBank (seed + 900));
            setBank (a, params::Bank::side, randomBank (seed + 1000));
            setMode (a, mode);
            juce::MemoryBlock state;
            a.getStateInformation (state);
            LabQ8AudioProcessor b;
            b.setStateInformation (state.getData(), (int) state.getSize());
            // Values: exactly restored, except that frequency and Q (log
            // mapping, frozen since E0, the same in every bank) may move by a
            // few float ulp on the first save / load, as in v4 (E2c.1, E4a);
            // measured here: 3 ulp (9e-5 Hz at 488 Hz). 1e-6 relative.
            int wrong = 0;
            for (auto* q : a.getParameters())
            {
                const auto id = dynamic_cast<juce::RangedAudioParameter*> (q)->paramID;
                const auto va = a.getValueTreeState().getRawParameterValue (id)->load(), vb = b.getValueTreeState().getRawParameterValue (id)->load();
                const bool logParam = id.endsWith ("_freq") || id.endsWith ("_q");
                if (logParam ? std::abs (va - vb) > 1.0e-6f * std::abs (va) : va != vb)
                    if (++wrong <= 3)
                        ctx.note (str ("seed ", seed, ": ", id, " ", va, " -> ", vb));
            }
            LQ_CHECK (wrong == 0, str ("seed ", seed, " mode ", (int) mode, ": ", wrong, " values not restored"));

            // Sound: the restored session is a fixed point (restoring it again
            // gives the same samples) and matches the original to within that ulp.
            juce::MemoryBlock again;
            b.getStateInformation (again);
            LabQ8AudioProcessor c;
            c.setStateInformation (again.getData(), (int) again.getSize());
            const auto ra = render (a, x, 2, fs, 512), rb = render (b, x, 2, fs, 512), rc = render (c, x, 2, fs, 512);
            double diff = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (size_t i = 0; i < ra[0].size(); ++i)
                    diff = std::max (diff, (double) std::abs (ra[(size_t) ch][i] - rb[(size_t) ch][i]));
            LQ_CHECK (same (rb, rc), str ("seed ", seed, " mode ", (int) mode, ": restoring the restored session changes the sound"));
            LQ_CHECK (diff < 1.0e-6, str ("seed ", seed, " mode ", (int) mode, ": restored session differs from the original by ", diff));
        }

    // The E4a fixture (Mid/Side, every bank set) now plays M/S: it must
    // equal the reference built from its own MID / SIDE values.
    juce::MemoryBlock fixture;
    juce::File (LABQ8_TEST_FIXTURES_DIR).getChildFile ("e4a_custom_v5.bin").loadFileAsData (fixture);
    LabQ8AudioProcessor p;
    p.setStateInformation (fixture.getData(), (int) fixture.getSize());
    LQ_CHECK (p.getValueTreeState().getRawParameterValue (params::procModeID)->load() == 1.0f, "fixture is Mid/Side");
    auto bankFrom = [&] (params::Bank which)
    {
        Bank b;
        const auto ids = params::bankOf (which);
        auto v = [&] (const char* id) { return (double) p.getValueTreeState().getRawParameterValue (id)->load(); };
        for (int i = 0; i < 8; ++i)
        {
            const auto& bi = ids.bands[(size_t) i];
            const auto& di = ids.dynamics[(size_t) i];
            auto gain = v (bi.gain);
            if (std::abs (gain) < 0.005) gain = 0.0; // the adapter's neutral-gain rule
            b.bands[(size_t) i] = { v (bi.frequency), gain, v (bi.q), v (bi.bypass) >= 0.5, dsp::filterTypeFromIndex ((int) std::lround (v (bi.type))) };
            b.dynamics[(size_t) i] = { v (di.enabled) >= 0.5, v (di.threshold), v (di.range), v (di.attack), v (di.release) };
        }
        return b;
    };
    const auto got = render (p, x, 2, fs, 256);
    LQ_CHECK (same (got, referenceMidSide (bankFrom (params::Bank::mid), bankFrom (params::Bank::side), x, fs, 256)),
              "e4a_custom_v5 does not play its MID / SIDE banks");
}

//==============================================================================
// Robustness and I. Stress.

LQ_TEST ("e4b robustness: silence stays exactly silent, non-finite input is contained, 1-sample blocks, sample-rate changes")
{
    // Silence: exact zeros in every mode, no reduction.
    for (const auto mode : { StereoMode::stereo, StereoMode::midSide })
    {
        dsp::StereoModeEngine e;
        apply (e, StereoBank::stereo, randomBank (101));
        apply (e, StereoBank::mid, randomBank (102));
        apply (e, StereoBank::side, randomBank (103));
        e.setMode (mode);
        e.prepare (48000.0, 2);
        Stereo z { std::vector<float> (9600, 0.0f), std::vector<float> (9600, 0.0f) };
        const auto y = runEngine (e, z, 2, 1);
        LQ_CHECK (maxAbs (y[0]) == 0.0 && maxAbs (y[1]) == 0.0, "silence must stay exactly silent");
    }

    // Non-finite input in M/S: contained (finite output afterwards).
    {
        dsp::StereoModeEngine e;
        apply (e, StereoBank::mid, randomBank (104));
        apply (e, StereoBank::side, randomBank (105));
        e.setMode (StereoMode::midSide);
        e.prepare (48000.0, 2);
        auto x = makeInput (9600, 48000.0, 106);
        x[0][100] = std::numeric_limits<float>::quiet_NaN();
        x[1][200] = std::numeric_limits<float>::infinity();
        const auto y = runEngine (e, x, 2, 64);
        bool finite = true;
        for (int ch = 0; ch < 2; ++ch)
            for (size_t i = 300; i < y[0].size(); ++i)
                finite = finite && std::isfinite (y[(size_t) ch][i]);
        LQ_CHECK (finite, "NaN / Inf input must not poison the M/S path");
    }

    // Sample-rate changes: each prepare gives the reference at that rate and
    // a 20 ms fade.
    dsp::StereoModeEngine e;
    const auto mid = randomBank (107), side = randomBank (108);
    apply (e, StereoBank::mid, mid);
    apply (e, StereoBank::side, side);
    e.setMode (StereoMode::midSide);
    for (const double fs : { 44100.0, 96000.0, 48000.0, 192000.0 })
    {
        e.prepare (fs, 2);
        LQ_CHECK (e.getFadeLengthSamples() == (int) std::lround (0.02 * fs), str (fs, " Hz: fade ", e.getFadeLengthSamples(), " samples"));
        const auto x = makeInput ((int) (0.2 * fs), fs, 109);
        LQ_CHECK (same (runEngine (e, x, 2, 1), referenceMidSide (mid, side, x, fs, 256)), str (fs, " Hz: 1-sample blocks differ from the reference"));
    }
}

LQ_TEST ("e4b I: stress: random automation of all 241 parameters and mode toggles, random block sizes 1..2048; finite, bounded, no allocation")
{
    LabQ8AudioProcessor p;
    setLayout (p, 2);
    p.prepareToPlay (48000.0, 2048);
    std::mt19937 rng (111);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    std::uniform_int_distribution<int> sizes (1, 2048);
    const auto& all = p.getParameters();
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, 2048);
    long allocations = 0;
    bool finite = true;
    double peak = 0.0;
    int toggles = 0;
    for (int k = 0; k < 600; ++k)
    {
        for (int j = 0; j < 12; ++j)
            all[(int) (u (rng) * 240.99f)]->setValueNotifyingHost (u (rng));
        if (k % 3 == 0)
        {
            all[params::procModeIndex]->setValueNotifyingHost (u (rng) < 0.5f ? 0.0f : 1.0f);
            ++toggles;
        }
        const int n = sizes (rng);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                view.setSample (ch, i, 0.5f * (u (rng) - 0.5f));
        {
            labq8::test::ScopedAllocationCounter counter;
            p.processBlock (view, midi);
            allocations += counter.count();
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                finite = finite && std::isfinite (view.getSample (ch, i));
                peak = std::max (peak, (double) std::abs (view.getSample (ch, i)));
            }
    }
    LQ_CHECK (allocations == 0, str ("processBlock allocated ", allocations, " times"));
    LQ_CHECK (finite, "non-finite output");
    // +-0.25 input; 8 bands at most +24 dB each could in theory reach far
    // more, so this only guards against blow-ups.
    LQ_CHECK (peak < 1.0e4, str ("output peak ", peak));
    ctx.note (str (toggles, " mode writes, output peak ", peak));
}

LQ_TEST ("e4b I: concurrency: parameters (mode included) written from another thread while the audio thread processes")
{
    LabQ8AudioProcessor p;
    setLayout (p, 2);
    p.prepareToPlay (48000.0, 256);
    std::atomic<bool> stop { false };
    std::thread writer ([&]
    {
        std::mt19937 rng (121);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        const auto& all = p.getParameters();
        while (! stop.load())
            all[(int) (u (rng) * 240.99f)]->setValueNotifyingHost (u (rng));
    });
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, 256);
    bool finite = true;
    for (int k = 0; k < 3000; ++k)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                buffer.setSample (ch, i, 0.1f * (float) std::sin (0.01 * (k * 256 + i) * (ch + 1)));
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                finite = finite && std::isfinite (buffer.getSample (ch, i));
    }
    stop = true;
    writer.join();
    LQ_CHECK (finite, "non-finite output under concurrent parameter writes");
}

//==============================================================================
// E4b review: the path not heard (standby) and the flat path.

LQ_TEST ("e4b standby: under rapid automation, host bypass, transport gaps and a sample-rate change, the path not heard follows exactly: after the switch it equals a processor that heard it all along (both directions)")
{
    // Processor X hears `from` and switches to `to` at the end; processor Y
    // hears `to` all along. Same input, same automation of all three banks
    // every block, same bypassed blocks, same gaps without processBlock
    // calls, same prepareToPlay at a new rate. After X's fade, X == Y bit
    // for bit: its standby path saw exactly what Y's heard path saw.
    for (const auto from : { StereoMode::stereo, StereoMode::midSide })
    {
        const auto to = from == StereoMode::stereo ? StereoMode::midSide : StereoMode::stereo;
        LabQ8AudioProcessor x, y;
        for (auto* p : { &x, &y })
        {
            setBank (*p, params::Bank::stereo, randomBank (131));
            setBank (*p, params::Bank::mid, randomBank (132));
            setBank (*p, params::Bank::side, randomBank (133));
            setLayout (*p, 2);
        }
        setMode (x, from);
        setMode (y, to);

        std::mt19937 rng (134);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        juce::MidiBuffer midi;
        double fs = 48000.0;
        const int block = 256;
        x.prepareToPlay (fs, block);
        y.prepareToPlay (fs, block);
        int fadeEnd = -1, compared = 0;
        bool identical = true;
        for (int k = 0; k < 520; ++k)
        {
            if (k == 250) // sample-rate change, as a host does: prepare again
            {
                fs = 96000.0;
                x.prepareToPlay (fs, block);
                y.prepareToPlay (fs, block);
            }
            if (k >= 150 && k < 170) // transport stopped: no calls at all
                continue;

            // Automation: 6 random parameters of the three banks (not the mode).
            for (int j = 0; j < 6; ++j)
            {
                int index = (int) (u (rng) * 240.99f);
                if (index == params::procModeIndex)
                    index = 0;
                const float value = u (rng);
                x.getParameters()[index]->setValueNotifyingHost (value);
                y.getParameters()[index]->setValueNotifyingHost (value);
            }
            if (k == 480)
            {
                setMode (x, to);
                fadeEnd = (int) std::lround (0.02 * fs) / block + 1 + k;
            }

            juce::AudioBuffer<float> bx (2, block), by (2, block);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const auto v = 0.3f * (u (rng) - 0.5f) + 0.2f * (float) std::sin (0.013 * (k * block + i) * (ch + 1));
                    bx.setSample (ch, i, v);
                    by.setSample (ch, i, v);
                }
            if (k >= 100 && k < 110) // host bypass
            {
                x.processBlockBypassed (bx, midi);
                y.processBlockBypassed (by, midi);
            }
            else
            {
                x.processBlock (bx, midi);
                y.processBlock (by, midi);
            }
            if (fadeEnd >= 0 && k > fadeEnd)
            {
                ++compared;
                for (int ch = 0; ch < 2; ++ch)
                    identical = identical && std::memcmp (bx.getReadPointer (ch), by.getReadPointer (ch), sizeof (float) * block) == 0;
            }
        }
        LQ_CHECK (compared > 30 && identical, str (from == StereoMode::stereo ? "Stereo -> M/S" : "M/S -> Stereo", ": after the switch the output differs from the path heard all along (",
                                                   compared, " blocks compared)"));
    }
}

LQ_TEST ("e4b flat path: it never skips pending work: enabling dynamics, a gain, a type change or a bypass on flat banks leaves it in that same block, and the output equals the reference from then on")
{
    // The reference codes every sample; while the engine is flat it differs
    // from it by at most the codec's 1 ulp, and from the change on it must
    // be identical bit for bit (same filter, ramp and detector states).
    const double fs = 48000.0;
    const auto x = makeInput (48000, fs, 141);
    struct Change { const char* name; std::function<void (Bank&, Bank&)> apply; };
    const Change changes[] = {
        { "dynamics on (Bell at 0 dB)", [] (Bank& m, Bank&) { m.dynamics[2] = { true, -40.0, 9.0, 2.0, 60.0 }; m.bands[2].frequencyHz = 300.0; } },
        { "gain on SIDE", [] (Bank&, Bank& s) { s.bands[5].gainDb = 4.0; } },
        { "type change to Low Cut", [] (Bank& m, Bank&) { m.bands[1].type = dsp::FilterType::lowCut; } },
        { "Notch bypass released", [] (Bank& m, Bank&) { m.bands[0].type = dsp::FilterType::notch; m.bands[0].bypassed = false; } },
    };
    for (const auto& c : changes)
    {
        Bank mid = flatBank(), side = flatBank();
        if (std::string (c.name) == "Notch bypass released")
        {
            mid.bands[0].type = dsp::FilterType::notch;
            mid.bands[0].bypassed = true; // dry: flat
        }
        Bank midAfter = mid, sideAfter = side;
        c.apply (midAfter, sideAfter);

        dsp::StereoModeEngine e;
        dsp::EqCore rm, rs;
        apply (e, StereoBank::mid, mid); apply (e, StereoBank::side, side);
        apply (rm, mid); apply (rs, side);
        e.setMode (StereoMode::midSide);
        e.prepare (fs, 2);
        rm.prepare (fs, 1); rs.prepare (fs, 1);

        Stereo got = x, ref = x;
        std::vector<float> mb (256), sb (256);
        const int change = 100 * 256 + 128 + 37; // off the 64-sample flat grid on purpose
        bool flatBefore = true, exactAfter = true, closeBefore = true, leftFlat = false;
        double worstUlp = 0.0; int worstAt = -1; float worstG = 0, worstR = 0, worstX0 = 0, worstX1 = 0;
        for (int pos = 0; pos < 48000;)
        {
            if (pos == change)
            {
                apply (e, StereoBank::mid, midAfter); apply (e, StereoBank::side, sideAfter);
                apply (rm, midAfter); apply (rs, sideAfter);
            }
            const int n = std::min ({ 256, pos < change ? change - pos : 256, 48000 - pos });
            float* ch[2] = { got[0].data() + pos, got[1].data() + pos };
            e.process (ch, 2, n);
            if (pos >= change && ! leftFlat)
                leftFlat = ! e.isMidSideFlat();
            if (pos < change)
                flatBefore = flatBefore && e.isMidSideFlat();

            dsp::midside::encode (ref[0].data() + pos, ref[1].data() + pos, mb.data(), sb.data(), n);
            float* mp[1] = { mb.data() };
            float* sp[1] = { sb.data() };
            rm.process (mp, 1, n);
            rs.process (sp, 1, n);
            dsp::midside::decode (mb.data(), sb.data(), ref[0].data() + pos, ref[1].data() + pos, n);

            for (int k = 0; k < 2; ++k)
                for (int i = pos; i < pos + n; ++i)
                {
                    const float g = got[(size_t) k][(size_t) i], r = ref[(size_t) k][(size_t) i];
                    if (i >= change)
                        exactAfter = exactAfter && g == r;
                    else
                    {
                        const float big = std::max (std::abs (x[0][(size_t) i]), std::abs (x[1][(size_t) i]));
                        const double ratio = std::abs ((double) g - r) / (std::nextafter (big, 2.0f) - big);
                        if (ratio > worstUlp) { worstUlp = ratio; worstAt = i; worstG = g; worstR = r; worstX0 = x[0][(size_t) i]; worstX1 = x[1][(size_t) i]; }
                        closeBefore = closeBefore && ratio <= 1.0;
                    }
                }
            pos += n;
        }
        LQ_CHECK (flatBefore && leftFlat, str (c.name, ": flat before ", flatBefore, ", left the flat path in the change block ", leftFlat));
        LQ_CHECK (exactAfter, str (c.name, ": output differs from the reference after the change"));
        LQ_CHECK (closeBefore, str (c.name, ": flat output not within 1 ulp of the coded reference (worst ", worstUlp, " ulp at ", worstAt,
                                    ": engine ", worstG, " ref ", worstR, " input ", worstX0, " / ", worstX1, ")"));
    }
}
