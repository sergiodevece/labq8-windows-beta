#include "TestHarness.h"

#include "AnalyzerSignals.h"
#include "analysis/OfflineRender.h"
#include "dsp/LoudnessMatcher.h"
#include "dsp/BiquadDesign.h"
#include "assist/ProposalLimits.h"

#include <algorithm>
#include <cmath>

using namespace iaq8;
using labq8::dsp::BandSettings;
using labq8::dsp::FilterType;

namespace
{
constexpr double kFs = 48000.0;

dsp::BandSet emptySet()
{
    dsp::BandSet s {};
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = assist::unusedBand (i);
    return s;
}

// Mean power (dB) of x within +-1/8 octave of f, from one zero-padded FFT
// (hundreds of bins, so the noise-like estimate is stable).
double bandPowerDb (const std::vector<float>& x, double f)
{
    constexpr int order = 19; // 524288 >= 6 s at 48 kHz
    const size_t size = size_t (1) << order;
    std::vector<float> buf (2 * size, 0.0f);
    std::copy (x.begin(), x.end(), buf.begin());
    juce::dsp::FFT (order).performFrequencyOnlyForwardTransform (buf.data());
    const auto lo = (size_t) (f * std::pow (2.0, -0.125) * (double) size / kFs);
    const auto hi = (size_t) (f * std::pow (2.0, 0.125) * (double) size / kFs);
    double sum = 0;
    for (size_t k = lo; k <= hi; ++k)
        sum += (double) buf[k] * buf[k];
    return 10.0 * std::log10 (sum);
}
} // namespace

LQ_TEST ("render: a -6 dB bell at 1 kHz lowers the after spectrum by ~6 dB")
{
    const auto n = (size_t) (kFs * 6);
    std::vector<std::vector<float>> in { test::makePink (n, kFs, 1u) };
    auto bands = emptySet();
    bands[0] = BandSettings { 1000.0, -6.0, 1.0, false, FilterType::bell };
    const auto r = analysis::render (in, kFs, bands);
    LQ_CHECK (r.after.size() == 1 && r.after[0].size() == n, "shape");
    // compensationDb is applied to `after`: remove it to read the pure EQ effect.
    const auto delta = bandPowerDb (r.after[0], 1000.0) - bandPowerDb (in[0], 1000.0) - r.compensationDb;
    LQ_CHECK (std::abs (delta - (-6.0)) < 0.8, "1 kHz drops ~6 dB");
}

LQ_TEST ("render: after K loudness matches before within 0.5 dB")
{
    const auto n = (size_t) (kFs * 6);
    std::vector<std::vector<float>> in { test::makePink (n, kFs, 2u) };
    auto bands = emptySet();
    bands[0] = BandSettings { 1000.0, -6.0, 1.0, false, FilterType::bell };
    bands[1] = BandSettings { 200.0, 3.0, 0.8, false, FilterType::lowShelf };
    const auto r = analysis::render (in, kFs, bands);
    const auto before = 10.0 * std::log10 (iaq8::dsp::LoudnessMatcher::wholeSignalMeanSquare (in, kFs));
    const auto after = 10.0 * std::log10 (iaq8::dsp::LoudnessMatcher::wholeSignalMeanSquare (r.after, kFs));
    LQ_CHECK (std::abs (after - before) < 0.5, "K loudness matched");
    LQ_CHECK (std::abs (r.compensationDb) <= 6.0 + 1.0e-9, "clamp");
}

LQ_TEST ("render: an all-bypassed band set is the identity")
{
    const auto n = (size_t) (kFs * 2);
    std::vector<std::vector<float>> in { test::makePink (n, kFs, 3u), test::makePink (n, kFs, 4u) };
    const auto r = analysis::render (in, kFs, emptySet());
    LQ_CHECK (r.after.size() == 2, "channels");
    double maxDiff = 0;
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < n; ++i)
            maxDiff = std::max (maxDiff, (double) std::abs (r.after[c][i] - in[c][i]));
    LQ_CHECK (maxDiff < 1.0e-6, "identity");
    LQ_CHECK (std::abs (r.compensationDb) < 1.0e-6, "no compensation");
}
