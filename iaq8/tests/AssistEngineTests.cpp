// Exact float comparison is intentional: bit-exactness is a tested property.
#pragma clang diagnostic ignored "-Wfloat-equal"

#include "AllocationGuard.h"
#include "TestHarness.h"
#include "TestSignals.h"

#include "dsp/AssistEngine.h"

#include <array>
#include <cmath>
#include <utility>
#include <vector>

using namespace iaq8::dsp;
using namespace labq8::test::signals;
using labq8::dsp::BandSettings;
using labq8::dsp::EqCore;

namespace
{
constexpr double kFs = 48000.0;

BandSet flatSet() { return {}; }

BandSet bellSet (double f, double gainDb, double q)
{
    BandSet s {};
    s[3] = { f, gainDb, q, false, labq8::dsp::FilterType::bell };
    return s;
}

BandSet otherSet()
{
    BandSet s {};
    s[0] = { 120.0, 5.0, 1.2, false, labq8::dsp::FilterType::bell };
    s[5] = { 4000.0, -6.0, 2.0, false, labq8::dsp::FilterType::bell };
    return s;
}

EqCore makeLone (const BandSet& set, int channels)
{
    EqCore core;
    core.prepare (kFs, channels);
    for (size_t i = 0; i < set.size(); ++i)
        core.setBandTargets ((int) i, set[i]);
    return core;
}

// Runs a stereo signal through `process` in blocks of `block`.
template <typename F>
void runBlocks (Signal& l, Signal& r, int block, F&& process)
{
    for (size_t pos = 0; pos < l.size(); pos += (size_t) block)
    {
        const auto n = (int) std::min ((size_t) block, l.size() - pos);
        float* ch[] = { l.data() + pos, r.data() + pos };
        process (ch, n);
    }
}
} // namespace

LQ_TEST ("assist engine: applied path is bit-identical to a lone EqCore")
{
    const auto applied = otherSet();
    AssistEngine engine;
    engine.prepare (kFs, 2, 512);
    engine.setApplied (applied);
    engine.setPreview (bellSet (1000.0, -12.0, 2.0));
    engine.setListen (Listen::applied);

    auto lone = makeLone (applied, 2);
    auto l = makeNoise (20000, 1), r = makeNoise (20000, 2);
    auto l2 = l, r2 = r;
    runBlocks (l, r, 300, [&] (float* const* ch, int n) { engine.process (ch, 2, n); });
    runBlocks (l2, r2, 300, [&] (float* const* ch, int n) { lone.process (ch, 2, n); });
    LQ_CHECK (l == l2 && r == r2, "applied output equals lone EqCore");
}

LQ_TEST ("assist engine: preview output equals EqCore(preview) x 10^(comp/20)")
{
    const auto preview = bellSet (800.0, 10.0, 0.7);
    AssistEngine engine;
    engine.prepare (kFs, 2, 512);
    engine.setApplied (otherSet());
    engine.setPreview (preview);
    engine.setListen (Listen::preview);

    auto lone = makeLone (preview, 2);
    auto l = makeNoise (40000, 3), r = makeNoise (40000, 4);
    auto l2 = l, r2 = r;
    // 256-sample blocks. The compensation gain moves per sample, so the gain
    // that getCompensationDb() reports after a block is the one applied to
    // that block's last sample (after the 20 ms fade has finished).
    double worst = 0.0;
    bool movedGain = false;
    for (size_t pos = 0; pos < l.size() - 256; pos += 256) // whole blocks only
    {
        float* ch[] = { l.data() + pos, r.data() + pos };
        float* ch2[] = { l2.data() + pos, r2.data() + pos };
        engine.process (ch, 2, 256);
        lone.process (ch2, 2, 256);
        if (pos >= 2048)
        {
            const auto g = std::pow (10.0, engine.getCompensationDb() / 20.0);
            movedGain = movedGain || std::abs (engine.getCompensationDb()) > 0.5;
            worst = std::max (worst, std::abs ((double) l[pos + 255] - (double) l2[pos + 255] * g));
            worst = std::max (worst, std::abs ((double) r[pos + 255] - (double) r2[pos + 255] * g));
        }
    }
    LQ_CHECK (movedGain, "the test exercises a real compensation");
    LQ_CHECK (worst < 1.0e-6, "preview output equals EqCore(preview) x compensation gain");
}

namespace
{
// Engine (flat applied, -12 dB bell preview) plus two lone-EqCore references,
// all settled on the same noise-free sine. The per-sample mix weight of the
// engine is recovered as w = (y - a) / (p - a) from the references.
struct FadeRig
{
    static constexpr double amp = 0.5;
    static constexpr size_t settleLength = 480000; // 10 s: compensation settles on its +6 dB clamp
    static constexpr double compGain = 1.9952623149688795; // 10^(6/20)
    int fadeN = (int) std::lround (0.020 * kFs);
    AssistEngine engine;
    EqCore flat, bell;
    size_t phase = 0; // absolute sample index of the sine

    explicit FadeRig (int maxBlock = 512)
    {
        engine.prepare (kFs, 1, maxBlock);
        engine.setApplied (flatSet());
        engine.setPreview (bellSet (1000.0, -12.0, 1.0));
        flat = makeLone (flatSet(), 1);
        bell = makeLone (bellSet (1000.0, -12.0, 1.0), 1);
        auto x = makeSine (1000.0, kFs, settleLength, amp);
        auto e = x, f = x, b = x;
        engine.process (std::array<float*, 1> { e.data() }.data(), 1, (int) e.size());
        flat.process (std::array<float*, 1> { f.data() }.data(), 1, (int) f.size());
        bell.process (std::array<float*, 1> { b.data() }.data(), 1, (int) b.size());
        phase = settleLength;
    }

    struct Out { Signal x, y, a, p; };

    // Runs `n` samples in blocks of `block`; `atBlock(blockIndex)` runs before each block.
    template <typename F>
    Out run (size_t n, int block, F&& atBlock)
    {
        Out o;
        o.x = makeSine (1000.0, kFs, phase + n, amp);
        o.x.erase (o.x.begin(), o.x.begin() + (long) phase);
        o.y = o.a = o.p = o.x;
        phase += n;
        int blockIndex = 0;
        for (size_t pos = 0; pos < n; pos += (size_t) block, ++blockIndex)
        {
            atBlock (blockIndex);
            const auto len = (int) std::min ((size_t) block, n - pos);
            float* cy[] = { o.y.data() + pos };
            engine.process (cy, 1, len);
        }
        float* ca[] = { o.a.data() };
        float* cp[] = { o.p.data() };
        flat.process (ca, 1, (int) n);
        bell.process (cp, 1, (int) n);
        for (auto& v : o.p)
            v = (float) ((double) v * compGain); // the preview path carries the compensation
        return o;
    }

    // Mix weight per sample; NaN where the references do not separate the paths.
    static std::vector<double> weights (const Out& o)
    {
        std::vector<double> w (o.x.size(), std::nan (""));
        for (size_t i = 0; i < w.size(); ++i)
        {
            const double d = (double) o.p[i] - (double) o.a[i];
            if (std::abs (d) > 0.1)
                w[i] = ((double) o.y[i] - (double) o.a[i]) / d;
        }
        return w;
    }
};

// Reference weight model: linear ramp of 1/N per sample towards the target.
std::vector<double> modelWeights (size_t n, int N, double start, const std::vector<std::pair<size_t, double>>& targetChanges)
{
    std::vector<double> w (n);
    double pos = start, target = start;
    size_t next = 0;
    for (size_t i = 0; i < n; ++i)
    {
        while (next < targetChanges.size() && targetChanges[next].first == i)
            target = targetChanges[next++].second;
        if (pos < target) pos = std::min (target, pos + 1.0 / N);
        else if (pos > target) pos = std::max (target, pos - 1.0 / N);
        w[i] = pos;
    }
    return w;
}

bool weightsMatch (const std::vector<double>& measured, const std::vector<double>& model, double tol)
{
    for (size_t i = 0; i < measured.size(); ++i)
        if (! std::isnan (measured[i]) && std::abs (measured[i] - model[i]) > tol)
            return false;
    return true;
}
} // namespace

LQ_TEST ("assist engine: A/B switch fades linearly over 20 ms, starting at the call, for any host block size")
{
    for (const int block : { 1, 64, 480, 2000 })
    {
        FadeRig rig;
        constexpr size_t n = 2400;
        const auto o = rig.run (n, block, [&] (int b) { if (b == 0) rig.engine.setListen (Listen::preview); });
        const auto w = FadeRig::weights (o);
        const auto model = modelWeights (n, rig.fadeN, 0.0, { { 0, 1.0 } });
        LQ_CHECK (rig.fadeN == 960, "20 ms at 48 kHz is 960 samples");
        LQ_CHECK (weightsMatch (w, model, 2.0e-3), "weight is the exact linear ramp from sample 0");
        // Spot checks of the shape itself, independent of the model.
        size_t seen = 0;
        for (size_t i = 0; i < n; ++i)
            if (! std::isnan (w[i]) && i < 960)
                ++seen;
        LQ_CHECK (seen > 100, "enough measurable samples inside the fade");
        for (size_t i = 0; i < n; ++i)
        {
            if (std::isnan (w[i]))
                continue;
            if (i < 960)
                LQ_CHECK (std::abs (w[i] - (double) (i + 1) / 960.0) < 2.0e-3, "linear (i+1)/N");
            else
                LQ_CHECK (std::abs (w[i] - 1.0) < 2.0e-3, "complete after round(0.020*fs) samples");
        }
    }
}

LQ_TEST ("assist engine: mid-fade re-toggle reverses continuously")
{
    FadeRig rig;
    constexpr size_t n = 2400;
    // Block of 100: listen=preview at block 0 (samples 0..), applied at block 3 (sample 300).
    const auto o = rig.run (n, 100, [&] (int b)
    {
        if (b == 0) rig.engine.setListen (Listen::preview);
        if (b == 3) rig.engine.setListen (Listen::applied);
    });
    const auto w = FadeRig::weights (o);
    const auto model = modelWeights (n, rig.fadeN, 0.0, { { 0, 1.0 }, { 300, 0.0 } });
    LQ_CHECK (weightsMatch (w, model, 2.0e-3), "weight rises 300 samples then falls back");
    LQ_CHECK (std::abs (model[299] - 300.0 / 960.0) < 1.0e-9 && model[1500] == 0.0, "model sanity");
    double maxStep = 0.0;
    for (size_t i = 1; i < n; ++i)
        maxStep = std::max (maxStep, (double) std::abs (o.y[i] - o.y[i - 1]));
    LQ_CHECK (maxStep < 0.5 * 2.0 * 3.14159265 * 1000.0 / kFs * 1.05, "no discontinuity beyond the sine slope");
}

LQ_TEST ("assist engine: clearPreview while listening to preview returns to applied smoothly")
{
    FadeRig rig;
    // Reach full preview first.
    rig.run (2400, 512, [&] (int b) { if (b == 0) rig.engine.setListen (Listen::preview); });
    constexpr size_t n = 6000;
    auto o = rig.run (n, 128, [&] (int b) { if (b == 0) rig.engine.clearPreview(); });
    LQ_CHECK (! rig.engine.hasPreview(), "preview cleared");
    // y mixes the applied sine (amplitude 0.5) with the compensated -12 dB bell (0.25), so it never
    // exceeds the applied amplitude: jumps are bounded by the plain sine slope. The preview core
    // keeps the old set (and its compensation) until the fade has finished: no swell.
    double maxJump = 0.0;
    for (size_t i = 1; i < n; ++i)
        maxJump = std::max (maxJump, (double) std::abs (o.y[i] - o.y[i - 1]));
    LQ_CHECK (maxJump < 0.5 * 2.0 * 3.14159265 * 1000.0 / kFs * 1.05, "smooth");
    // After the fade the preview core is retargeted to applied, and the output ends as the clean applied signal.
    double err = 0.0;
    for (size_t i = 3000; i < n; ++i)
        err = std::max (err, (double) std::abs (o.y[i] - o.x[i]));
    LQ_CHECK (err < 1.0e-5, "ends on the applied path");
    LQ_CHECK ((double) std::abs (o.y[0] - o.p[0]) < 0.02, "starts where the preview was (compensated)");
}

LQ_TEST ("assist engine: blocks larger than maxBlockSize give the same output as small blocks")
{
    const auto run = [] (int maxBlock, int hostBlock)
    {
        AssistEngine engine;
        engine.prepare (kFs, 2, maxBlock);
        engine.setApplied (otherSet());
        engine.setPreview (bellSet (1000.0, -12.0, 2.0));
        auto l = makeNoise (5000, 11), r = makeNoise (5000, 12);
        int b = 0;
        runBlocks (l, r, hostBlock, [&] (float* const* ch, int n)
        {
            if (b++ == 1) engine.setListen (Listen::preview);
            engine.process (ch, 2, n);
        });
        return std::pair { l, r };
    };
    // One call of 1000 samples through a 64-sample scratch: the chunk loop runs 16 times.
    const auto big = run (64, 1000);
    const auto ref = run (1000, 1000);
    LQ_CHECK (big.first == ref.first && big.second == ref.second, "chunked equals single pass");
    const auto small = run (64, 64);
    // Different block sizes move the listen change by whole blocks; compare after both fades finished.
    bool same = true;
    for (size_t i = 3000; i < 5000; ++i)
        same = same && big.first[i] == small.first[i] && big.second[i] == small.second[i];
    LQ_CHECK (same, "steady state independent of block size");
}

LQ_TEST ("assist engine: process before prepare or with empty input is a no-op")
{
    AssistEngine engine; // never prepared
    auto l = makeNoise (256, 21);
    const auto copy = l;
    float* ch[] = { l.data() };
    engine.process (ch, 1, 256);
    LQ_CHECK (l == copy, "unprepared: untouched, and it returned");

    engine.prepare (kFs, 1, 64);
    engine.setApplied (otherSet());
    engine.process (ch, 0, 256);
    engine.process (ch, 1, 0);
    engine.process (ch, 1, -5);
    LQ_CHECK (l == copy, "empty requests leave the buffer alone");
}

LQ_TEST ("assist engine: listen preview without a preview plays applied")
{
    const auto applied = otherSet();
    AssistEngine engine;
    engine.prepare (kFs, 2, 512);
    engine.setApplied (applied);
    engine.setListen (Listen::preview);
    LQ_CHECK (! engine.hasPreview(), "no preview");

    auto lone = makeLone (applied, 2);
    auto l = makeNoise (10000, 5), r = makeNoise (10000, 6);
    auto l2 = l, r2 = r;
    runBlocks (l, r, 200, [&] (float* const* ch, int n) { engine.process (ch, 2, n); });
    runBlocks (l2, r2, 200, [&] (float* const* ch, int n) { lone.process (ch, 2, n); });
    LQ_CHECK (l == l2 && r == r2, "plays applied");

    // Setting then clearing a preview returns to applied.
    engine.setPreview (bellSet (1000.0, -12.0, 2.0));
    LQ_CHECK (engine.hasPreview(), "preview set");
    engine.clearPreview();
    LQ_CHECK (! engine.hasPreview(), "preview cleared");
}

LQ_TEST ("assist engine: process does not allocate")
{
    AssistEngine engine;
    engine.prepare (kFs, 2, 512);
    engine.setApplied (otherSet());
    engine.setPreview (bellSet (1000.0, -12.0, 2.0));
    auto l = makeNoise (512, 7), r = makeNoise (512, 8);
    float* ch[] = { l.data(), r.data() };

    labq8::test::ScopedAllocationCounter counter;
    for (int b = 0; b < 100; ++b)
    {
        engine.setApplied (otherSet());
        if (b % 3 == 0)
            engine.setPreview (bellSet (1000.0, -12.0, 2.0));
        if (b % 7 == 0)
            engine.clearPreview();
        if (b % 10 == 0)
            engine.setListen ((b / 10) % 2 == 0 ? Listen::preview : Listen::applied);
        engine.process (ch, 2, 512);
    }
    LQ_CHECK (counter.count() == 0, "no allocations in 100 blocks");
}
