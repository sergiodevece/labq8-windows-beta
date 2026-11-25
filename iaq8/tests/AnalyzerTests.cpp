// Tests of the track analysis: long-term spectrum, trend and measured problem
// candidates. Synthetic 10 s signals at 48 kHz, all deterministic.

// Exact float comparisons are intentional (copied constants, exact values).
#pragma clang diagnostic ignored "-Wfloat-equal"

#include "AnalyzerSignals.h"
#include "TestHarness.h"

#include "analysis/TrackAnalyzer.h"
#include "dsp/BiquadDesign.h"

#include <cmath>
#include <numbers>
#include <vector>

using namespace iaq8;
using analysis::AnalysisSummary;
using analysis::Candidate;
using analysis::CandidateKind;
using assist::Source;
using iaq8::test::Signal;
using labq8::test::str;

namespace
{
constexpr double kFs = 48000.0;
constexpr size_t kTen = 480000; // 10 s at 48 kHz

const char* kindName (CandidateKind k)
{
    switch (k)
    {
        case CandidateKind::excess:    return "excess";
        case CandidateKind::deficit:   return "deficit";
        case CandidateKind::resonance: return "resonance";
        case CandidateKind::lowRumble: return "lowRumble";
        case CandidateKind::sibilance: return "sibilance";
    }
    return "?";
}

std::string describe (const AnalysisSummary& s)
{
    std::string out = str ("slope=", s.slopeDbPerOct, " dB/oct, ", s.candidates.size(), " candidates");
    for (const auto& c : s.candidates)
        out += str ("\n    ", c.id, " ", kindName (c.kind), " f=", c.freqHz, " Hz mag=", c.magnitudeDb,
                    " dB width=", c.widthOct, " oct");
    return out;
}

double octavesBetween (double a, double b) { return std::abs (std::log2 (a / b)); }

// The candidate of `kind` closest to `freqHz` (nullptr when there is none).
const Candidate* nearest (const AnalysisSummary& s, CandidateKind kind, double freqHz)
{
    const Candidate* best = nullptr;
    for (const auto& c : s.candidates)
        if (c.kind == kind && (best == nullptr || octavesBetween (c.freqHz, freqHz) < octavesBetween (best->freqHz, freqHz)))
            best = &c;
    return best;
}

bool hasKind (const AnalysisSummary& s, CandidateKind kind)
{
    for (const auto& c : s.candidates)
        if (c.kind == kind)
            return true;
    return false;
}

std::vector<std::vector<float>> mono (const Signal& x) { return { x }; }
std::vector<std::vector<float>> dual (const Signal& x) { return { x, x }; }

// Pink noise without energy below `hz` (a voice-like low end).
Signal pinkAbove (double hz, std::uint32_t seed, const std::function<double (double)>& extraDb = {})
{
    return test::makePink (kTen, kFs, seed, -20.0, [hz, extraDb] (double f)
    {
        return (f < hz ? -60.0 : 0.0) + (extraDb ? extraDb (f) : 0.0);
    });
}
} // namespace

LQ_TEST ("analyzer: flat pink noise has no candidates and slope about -3 dB/oct")
{
    const auto pink = test::makePink (kTen, kFs, 1);

    for (auto source : { Source::mix, Source::other, Source::bass })
    {
        const auto s = analysis::analyse (dual (pink), kFs, source);
        ctx.note (str ("source ", (int) source, ": ", describe (s)));

        LQ_CHECK (s.enoughSignal, "10 s of pink noise is enough signal");
        LQ_CHECK (s.candidates.empty(), str ("no candidates, got ", s.candidates.size()));
        LQ_CHECK (std::abs (s.slopeDbPerOct + 3.0) <= 0.5, str ("slope ", s.slopeDbPerOct, " dB/oct"));
        LQ_CHECK ((int) s.spectrumDb.size() == AnalysisSummary::spectrumPoints, "60 spectrum points");
        LQ_CHECK (std::abs (s.rmsDbfs + 20.0) < 0.01, str ("rms ", s.rmsDbfs));
        LQ_CHECK (std::abs (s.crestDb - (s.peakDbfs - s.rmsDbfs)) < 1e-9, "crest = peak - rms");
        LQ_CHECK (s.channels == 2 && std::abs (s.seconds - 10.0) < 1e-9 && s.sampleRate == kFs, "general data");
    }

    const auto mix = analysis::analyse (dual (pink), kFs, Source::mix);
    LQ_CHECK (mix.fitFromHz == 30.0 && mix.fitToHz == 16000.0, "mix fits 30-16000 Hz");

    const auto again = analysis::analyse (dual (pink), kFs, Source::mix);
    LQ_CHECK (again.spectrumDb == mix.spectrumDb && again.slopeDbPerOct == mix.slopeDbPerOct, "deterministic");

    // Same at the other FFT sizes (orders 13 and 14).
    for (auto fs : { 44100.0, 96000.0 })
    {
        const auto s = analysis::analyse (dual (test::makePink ((size_t) (10.0 * fs), fs, 1)), fs, Source::mix);
        ctx.note (str (fs, " Hz: ", describe (s)));
        LQ_CHECK (s.candidates.empty() && std::abs (s.slopeDbPerOct + 3.0) <= 0.5, str (fs, " Hz: flat, slope ", s.slopeDbPerOct));
    }

    // The 1/6-oct spectrum follows the same -3 dB/oct over 100 Hz-10 kHz.
    if ((int) mix.spectrumDb.size() != AnalysisSummary::spectrumPoints)
        return;
    const auto i100 = (size_t) std::lround (6.0 * std::log2 (100.0 / 20.0));
    const auto i10k = (size_t) std::lround (6.0 * std::log2 (10000.0 / 20.0));
    const auto perOct = (mix.spectrumDb[i10k] - mix.spectrumDb[i100]) / ((double) (i10k - i100) / 6.0);
    ctx.note (str ("spectrumDb 100 Hz->10 kHz: ", perOct, " dB/oct"));
    LQ_CHECK (std::abs (perOct + 3.0) <= 0.5, str ("spectrum slope ", perOct));
}

LQ_TEST ("analyzer: +6 dB bell at 250 Hz Q1 on pink noise is an excess near 250 Hz")
{
    auto x = test::makePink (kTen, kFs, 2);
    test::applyBiquad (x, labq8::dsp::designBell (kFs, 250.0, 6.0, 1.0));
    const auto s = analysis::analyse (dual (x), kFs, Source::other);
    ctx.note (describe (s));

    const auto* c = nearest (s, CandidateKind::excess, 250.0);
    LQ_CHECK (c != nullptr, "an excess is reported");
    if (c == nullptr)
        return;

    LQ_CHECK (octavesBetween (c->freqHz, 250.0) <= 1.0 / 6.0, str ("excess at ", c->freqHz, " Hz"));
    LQ_CHECK (c->magnitudeDb >= 3.0 && c->magnitudeDb <= 7.0, str ("magnitude ", c->magnitudeDb, " dB"));
    LQ_CHECK (s.candidates.front().id == "c1" && &s.candidates.front() == c, "it is the strongest candidate (c1)");
    LQ_CHECK (! hasKind (s, CandidateKind::resonance), "a broad bell is not a resonance");
}

LQ_TEST ("analyzer: narrow +12 dB peak at 3150 Hz Q12 is a resonance")
{
    auto x = test::makePink (kTen, kFs, 3);
    test::applyBiquad (x, labq8::dsp::designBell (kFs, 3150.0, 12.0, 12.0));
    const auto s = analysis::analyse (dual (x), kFs, Source::other);
    ctx.note (describe (s));

    const auto* c = nearest (s, CandidateKind::resonance, 3150.0);
    LQ_CHECK (c != nullptr, "a resonance is reported");
    if (c == nullptr)
        return;

    LQ_CHECK (octavesBetween (c->freqHz, 3150.0) <= 1.0 / 12.0, str ("resonance at ", c->freqHz, " Hz"));
    LQ_CHECK (c->widthOct > 0.0 && c->widthOct < 1.0 / 6.0, str ("width ", c->widthOct, " oct"));

    // The same peak, smeared by the 1/3-oct smoothing, is not reported twice.
    const auto* e = nearest (s, CandidateKind::excess, 3150.0);
    LQ_CHECK (e == nullptr || octavesBetween (e->freqHz, 3150.0) > 1.0 / 3.0, "no duplicate excess at the resonance");
}

LQ_TEST ("analyzer: voice with energy below 80 Hz reports low_rumble")
{
    // Pink noise plus a 50 Hz sine at -12 dB relative to the noise RMS.
    auto x = test::makePink (kTen, kFs, 4);
    const auto amp = std::pow (10.0, (-20.0 - 12.0) / 20.0) * std::sqrt (2.0);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] += (float) (amp * std::sin (2.0 * std::numbers::pi * 50.0 * (double) i / kFs));

    const auto s = analysis::analyse (dual (x), kFs, Source::voice);
    ctx.note (describe (s));
    const auto* c = nearest (s, CandidateKind::lowRumble, 50.0);
    LQ_CHECK (c != nullptr, "lowRumble is reported");
    if (c != nullptr)
        LQ_CHECK (octavesBetween (c->freqHz, 50.0) <= 1.0 / 6.0, str ("rumble at ", c->freqHz, " Hz"));

    // Control: a voice-like signal with nothing below 100 Hz has no rumble,
    // and lowRumble applies to voice and guitars only.
    const auto clean = analysis::analyse (dual (pinkAbove (100.0, 4)), kFs, Source::voice);
    ctx.note (str ("clean voice: ", describe (clean)));
    LQ_CHECK (! hasKind (clean, CandidateKind::lowRumble), "no rumble without energy below 100 Hz");
    LQ_CHECK (! hasKind (analysis::analyse (dual (x), kFs, Source::mix), CandidateKind::lowRumble), "not for a mix");
    LQ_CHECK (hasKind (analysis::analyse (dual (x), kFs, Source::acousticGuitar), CandidateKind::lowRumble),
              "also for guitars");
}

LQ_TEST ("analyzer: voice with +8 dB 5-9 kHz reports sibilance")
{
    const auto boosted = pinkAbove (100.0, 5, [] (double f) { return f >= 5000.0 && f <= 9000.0 ? 8.0 : 0.0; });
    const auto s = analysis::analyse (dual (boosted), kFs, Source::voice);
    ctx.note (describe (s));

    const auto* c = nearest (s, CandidateKind::sibilance, 6700.0);
    LQ_CHECK (c != nullptr, "sibilance is reported");
    if (c != nullptr)
        LQ_CHECK (c->freqHz >= 5000.0 && c->freqHz <= 9000.0 && c->magnitudeDb > 4.0,
                  str ("sibilance at ", c->freqHz, " Hz, ", c->magnitudeDb, " dB"));

    for (const auto& e : s.candidates)
        LQ_CHECK (! (e.kind == CandidateKind::excess && e.freqHz >= 5000.0 && e.freqHz <= 9000.0),
                  "the band is reported as sibilance, not also as an excess");

    LQ_CHECK (! hasKind (analysis::analyse (dual (pinkAbove (100.0, 5)), kFs, Source::voice), CandidateKind::sibilance),
              "no sibilance without the boost");
    LQ_CHECK (! hasKind (analysis::analyse (dual (boosted), kFs, Source::mix), CandidateKind::sibilance),
              "sibilance applies to voice only");
}

LQ_TEST ("analyzer: mono and stereo detection")
{
    const auto a = test::makePink (kTen, kFs, 6);
    const auto b = test::makePink (kTen, kFs, 7);

    const auto same = analysis::analyse (dual (a), kFs, Source::mix);
    ctx.note (str ("L=R: sideToMid=", same.sideToMidDb, " dB corr=", same.correlation));
    LQ_CHECK (same.isMono && same.sideToMidDb < -40.0 && same.correlation > 0.999, "L=R is mono");

    const auto one = analysis::analyse (mono (a), kFs, Source::mix);
    LQ_CHECK (one.channels == 1 && one.isMono && one.correlation == 1.0, "a single channel is mono");

    const auto indep = analysis::analyse ({ a, b }, kFs, Source::mix);
    ctx.note (str ("independent: sideToMid=", indep.sideToMidDb, " dB corr=", indep.correlation));
    LQ_CHECK (! indep.isMono, "independent noises are stereo");
    LQ_CHECK (std::abs (indep.correlation) < 0.2, str ("correlation ", indep.correlation));
    LQ_CHECK (std::abs (indep.sideToMidDb) < 1.0, str ("side/mid ", indep.sideToMidDb, " dB"));
}

LQ_TEST ("analyzer: silence or under 6 s is not enough signal")
{
    const auto check = [&ctx] (const AnalysisSummary& s, const char* what)
    {
        LQ_CHECK (! s.enoughSignal, str (what, ": not enough signal"));
        LQ_CHECK (s.candidates.empty(), str (what, ": no candidates"));
        LQ_CHECK ((int) s.spectrumDb.size() == AnalysisSummary::spectrumPoints, str (what, ": 60 spectrum points"));
    };

    check (analysis::analyse (dual (Signal (kTen, 0.0f)), kFs, Source::voice), "10 s of silence");
    check (analysis::analyse (dual (test::makePink (kTen, kFs, 8, -70.0)), kFs, Source::voice), "10 s at -70 dBFS");
    check (analysis::analyse (dual (test::makePink (240000, kFs, 8)), kFs, Source::voice), "5 s of pink noise");
    check (analysis::analyse ({}, kFs, Source::voice), "no channels");
    check (analysis::analyse (dual (test::makePink (kTen, kFs, 8)), 0.0, Source::voice), "invalid sample rate");

    const auto justEnough = analysis::analyse (dual (test::makePink (312000, kFs, 8)), kFs, Source::mix);
    LQ_CHECK (justEnough.enoughSignal, "6.5 s of pink noise is enough");
}
