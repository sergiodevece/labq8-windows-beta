// Exact comparisons are intentional: block-size independence is bit-exact.
#pragma clang diagnostic ignored "-Wfloat-equal"

#include "AllocationGuard.h"
#include "TestHarness.h"
#include "TestSignals.h"

#include "dsp/AssistEngine.h"
#include "dsp/LoudnessMatcher.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace iaq8::dsp;
using namespace labq8::test::signals;
using labq8::dsp::BandSettings;
using labq8::dsp::EqCore;
using labq8::dsp::FilterType;

namespace
{
constexpr double kFs = 48000.0;

BandSet makeSet (BandSettings b)
{
    BandSet s {};
    s[3] = b;
    return s;
}

// Independent K-weighted mean-square level in dB over [begin, end) (mono),
// own biquad implementation, same design functions.
double kLevelDb (const Signal& x, size_t begin, size_t end)
{
    const auto hs = labq8::dsp::designHighShelf (kFs, 1681.97, 3.9998, 0.7071);
    const auto lc = labq8::dsp::designLowCut (kFs, 38.135, 0.5003);
    double s1[2] = {}, s2[2] = {}, sum = 0.0;
    const auto run = [] (const labq8::dsp::BiquadCoefficients& c, double* z, double in)
    {
        const double y = c.b0 * in + z[0];
        z[0] = c.b1 * in - c.a1 * y + z[1];
        z[1] = c.b2 * in - c.a2 * y;
        return y;
    };
    for (size_t i = 0; i < end; ++i)
    {
        const auto y = run (lc, s2, run (hs, s1, (double) x[i]));
        if (i >= begin)
            sum += y * y;
    }
    return 10.0 * std::log10 (sum / (double) (end - begin));
}

// A tone plus a little noise: the bell moves its level, noise keeps it realistic.
Signal makeProgram (size_t n, unsigned seed)
{
    auto x = makeNoise (n, seed, 0.05f);
    const auto t = makeSine (1000.0, kFs, n, 0.2);
    for (size_t i = 0; i < n; ++i)
        x[i] += t[i];
    return x;
}

void runEngine (AssistEngine& e, Signal& x, int block)
{
    for (size_t pos = 0; pos < x.size(); pos += (size_t) block)
    {
        float* ch[] = { x.data() + pos };
        e.process (ch, 1, (int) std::min ((size_t) block, x.size() - pos));
    }
}
} // namespace

LQ_TEST ("loudness: +3 dB bell at 1 kHz on noise is matched within 0.5 dB")
{
    const auto bell = makeSet ({ 1000.0, 3.0, 1.0, false, FilterType::bell });
    AssistEngine engine;
    engine.prepare (kFs, 1, 512);
    engine.setPreview (bell);
    engine.setListen (Listen::preview);

    const auto n = samples (6.0, kFs);
    for (const auto program : { 0, 1 })
    {
        engine.prepare (kFs, 1, 512); // also resets the matcher
        auto x = program == 0 ? makeNoise (n, 31) : makeProgram (n, 32);
        auto preview = x;
        runEngine (engine, preview, 480);

        EqCore flat;
        flat.prepare (kFs, 1);
        auto applied = x;
        float* ch[] = { applied.data() };
        flat.process (ch, 1, (int) n);

        const auto a = samples (5.0, kFs);
        const auto dPreview = kLevelDb (preview, a, n);
        const auto dApplied = kLevelDb (applied, a, n);
        LQ_CHECK (std::abs (dPreview - dApplied) < 0.5, "K-weighted levels match within 0.5 dB");
        if (program == 1)
            LQ_CHECK (engine.getCompensationDb() < -1.0, "the bell really was louder (so compensation did work)");
    }
}

LQ_TEST ("loudness: compensation is clamped to +-6 dB")
{
    AssistEngine engine;
    engine.prepare (kFs, 1, 512);
    // Wide +12 dB high shelf: preview much louder than applied.
    engine.setPreview (makeSet ({ 200.0, 12.0, 0.7, false, FilterType::highShelf }));
    engine.setListen (Listen::preview);
    auto x = makeNoise (samples (12.0, kFs), 41);
    runEngine (engine, x, 512);
    LQ_CHECK (std::abs (engine.getCompensationDb() + 6.0) < 0.01, "-6.0 dB");

    // And the opposite direction.
    engine.prepare (kFs, 1, 512);
    engine.setPreview (makeSet ({ 200.0, -12.0, 0.7, false, FilterType::highShelf }));
    auto y = makeNoise (samples (12.0, kFs), 42);
    runEngine (engine, y, 512);
    LQ_CHECK (std::abs (engine.getCompensationDb() - 6.0) < 0.01, "+6.0 dB");
}

LQ_TEST ("loudness: silence holds the last compensation")
{
    AssistEngine engine;
    engine.prepare (kFs, 1, 512);
    engine.setPreview (makeSet ({ 1000.0, -9.0, 0.8, false, FilterType::bell }));
    engine.setListen (Listen::preview);
    auto x = makeProgram (samples (8.0, kFs), 51);
    runEngine (engine, x, 512);
    const auto before = engine.getCompensationDb();
    LQ_CHECK (before > 1.0, "a real compensation is in place");

    Signal zeros (samples (5.0, kFs), 0.0f);
    runEngine (engine, zeros, 512);
    LQ_CHECK (std::abs (engine.getCompensationDb() - before) < 0.1, "unchanged by 5 s of zeros");
}

LQ_TEST ("loudness: matcher holds below silenceDb and does not start from silence")
{
    LoudnessMatcher m;
    m.prepare (kFs, 1);
    Signal quiet = makeNoise (samples (6.0, kFs), 61, 1.0e-5f); // far below -70 dB
    auto loud = quiet;
    for (auto& v : loud)
        v *= 4.0f;
    const float* a[] = { quiet.data() };
    const float* p[] = { loud.data() };
    m.push (a, p, 1, (int) quiet.size());
    LQ_CHECK (m.getCompensationDb() == 0.0, "quiet material never moves the compensation");
}

LQ_TEST ("loudness: matcher is independent of the block partition")
{
    const auto run = [] (int block)
    {
        LoudnessMatcher m;
        m.prepare (kFs, 2);
        auto l = makeNoise (30000, 71), r = makeNoise (30000, 72);
        auto pl = l, pr = r;
        for (auto& v : pl) v *= 0.4f;
        for (auto& v : pr) v *= 0.4f;
        for (size_t pos = 0; pos < l.size(); pos += (size_t) block)
        {
            const auto len = (int) std::min ((size_t) block, l.size() - pos);
            const float* a[] = { l.data() + pos, r.data() + pos };
            const float* p[] = { pl.data() + pos, pr.data() + pos };
            m.push (a, p, 2, len);
        }
        return m.getCompensationDb();
    };
    const auto one = run (1), big = run (7000);
    LQ_CHECK (one == big, "same compensation for any block size");
    LQ_CHECK (one > 0.5 && one < 6.0, "plausible value (preview 8 dB quieter, smoothing still moving)");
}

LQ_TEST ("loudness: applied path is never compensated")
{
    const auto applied = makeSet ({ 400.0, 4.0, 1.0, false, FilterType::bell });
    AssistEngine engine;
    engine.prepare (kFs, 2, 512);
    engine.setApplied (applied);
    engine.setPreview (makeSet ({ 1000.0, 9.0, 0.8, false, FilterType::bell })); // louder preview
    engine.setListen (Listen::applied);

    EqCore lone;
    lone.prepare (kFs, 2);
    lone.setBandTargets (3, applied[3]);
    auto l = makeProgram (samples (4.0, kFs), 81), r = makeProgram (samples (4.0, kFs), 82);
    auto l2 = l, r2 = r;
    for (size_t pos = 0; pos < l.size(); pos += 300)
    {
        const auto n = (int) std::min<size_t> (300, l.size() - pos);
        float* ch[] = { l.data() + pos, r.data() + pos };
        float* ch2[] = { l2.data() + pos, r2.data() + pos };
        engine.process (ch, 2, n);
        lone.process (ch2, 2, n);
    }
    LQ_CHECK (engine.getCompensationDb() < -1.0, "compensation is active for the preview path");
    LQ_CHECK (l == l2 && r == r2, "applied output is bit-identical to a lone EqCore");
}

LQ_TEST ("loudness: matcher and compensated engine do not allocate")
{
    LoudnessMatcher m;
    m.prepare (kFs, 2);
    AssistEngine engine;
    engine.prepare (kFs, 2, 512);
    engine.setPreview (makeSet ({ 1000.0, -9.0, 0.8, false, FilterType::bell }));
    engine.setListen (Listen::preview);
    auto l = makeNoise (512, 91), r = makeNoise (512, 92);
    auto pl = l, pr = r;
    float* ch[] = { l.data(), r.data() };
    const float* a[] = { l.data(), r.data() };
    const float* p[] = { pl.data(), pr.data() };

    labq8::test::ScopedAllocationCounter counter;
    for (int b = 0; b < 100; ++b)
    {
        m.push (a, p, 2, 512);
        engine.process (ch, 2, 512);
    }
    (void) m.getCompensationDb();
    m.reset();
    LQ_CHECK (counter.count() == 0, "no allocations in 100 blocks");
}
