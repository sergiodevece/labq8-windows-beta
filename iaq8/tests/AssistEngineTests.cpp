// Exact float comparison is intentional: bit-exactness is a tested property.
#pragma clang diagnostic ignored "-Wfloat-equal"

#include "AllocationGuard.h"
#include "TestHarness.h"
#include "TestSignals.h"

#include "dsp/AssistEngine.h"

#include <cmath>

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

LQ_TEST ("assist engine: preview path is bit-identical to a lone EqCore with the preview set")
{
    const auto preview = bellSet (1000.0, -12.0, 2.0);
    AssistEngine engine;
    engine.prepare (kFs, 2, 512);
    engine.setApplied (otherSet());
    engine.setPreview (preview);
    engine.setListen (Listen::preview);

    auto lone = makeLone (preview, 2);
    auto l = makeNoise (40000, 3), r = makeNoise (40000, 4);
    auto l2 = l, r2 = r;
    // Let the fade finish, then compare the steady state.
    runBlocks (l, r, 256, [&] (float* const* ch, int n) { engine.process (ch, 2, n); });
    runBlocks (l2, r2, 256, [&] (float* const* ch, int n) { lone.process (ch, 2, n); });
    LQ_CHECK (engine.getCompensationDb() == 0.0, "no compensation yet");
    bool same = true;
    for (size_t i = 20000; i < l.size(); ++i)
        same = same && l[i] == l2[i] && r[i] == r2[i];
    LQ_CHECK (same, "preview output equals lone EqCore after the fade");
}

LQ_TEST ("assist engine: A/B switch fades linearly over 20 ms")
{
    constexpr double amp = 0.05;
    AssistEngine engine;
    engine.prepare (kFs, 1, 512);
    engine.setApplied (flatSet());
    engine.setPreview (bellSet (1000.0, -12.0, 1.0));

    auto settle = makeSine (1000.0, kFs, 24000, amp);
    for (size_t pos = 0; pos < settle.size(); pos += 512)
    {
        float* ch[] = { settle.data() + pos };
        engine.process (ch, 1, (int) std::min<size_t> (512, settle.size() - pos));
    }

    const auto x = makeSine (1000.0, kFs, 4800, amp);
    auto y = x;
    engine.setListen (Listen::preview);
    for (size_t pos = 0; pos < y.size(); pos += 128)
    {
        float* ch[] = { y.data() + pos };
        engine.process (ch, 1, (int) std::min<size_t> (128, y.size() - pos));
    }

    // Gain ratio at samples with a large input (1 kHz bell: no phase shift at centre).
    const double g0 = 1.0, g1 = std::pow (10.0, -12.0 / 20.0);
    const double hi = g0 - 0.1 * (g0 - g1), lo = g0 - 0.9 * (g0 - g1);
    long t10 = -1, t90 = -1;
    for (size_t i = 0; i < x.size(); ++i)
    {
        if (std::abs (x[i]) < 0.8 * amp)
            continue;
        const double ratio = (double) y[i] / (double) x[i];
        if (t10 < 0 && ratio < hi) t10 = (long) i;
        if (t90 < 0 && ratio < lo) t90 = (long) i;
    }
    LQ_CHECK (t10 >= 0 && t90 >= 0, "fade found");
    const auto length = (double) (t90 - t10) / 0.8;
    const auto expected = std::round (0.020 * kFs);
    LQ_CHECK (std::abs (length - expected) <= 16.0, "fade length is 20 ms +-16 samples");

    double maxJump = 0.0;
    for (size_t i = 1; i < y.size(); ++i)
        maxJump = std::max (maxJump, (double) std::abs (y[i] - y[i - 1]));
    LQ_CHECK (maxJump <= 0.01, "no jumps between consecutive samples");
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
        if (b % 10 == 0)
            engine.setListen ((b / 10) % 2 == 0 ? Listen::preview : Listen::applied);
        engine.process (ch, 2, 512);
    }
    LQ_CHECK (counter.count() == 0, "no allocations in 100 blocks");
}
