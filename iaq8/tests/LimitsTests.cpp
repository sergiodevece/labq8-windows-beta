// Exact float comparisons are intentional (limits are exact constants).
#pragma clang diagnostic ignored "-Wfloat-equal"

// Tests of the per-source safety limits applied to every proposal.

#include "TestHarness.h"

#include "assist/ProposalLimits.h"
#include "dsp/BiquadDesign.h"
#include "parameters/ParameterIDs.h"

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <limits>
#include <locale>

using namespace iaq8;
using assist::Source;
using labq8::dsp::FilterType;
using labq8::test::str;

namespace
{
assist::ProposedBand band (FilterType type, double freq, double gain, double q, std::string candidate = {}, bool bypassed = false)
{
    return { { freq, gain, q, bypassed, type }, std::move (candidate), "" };
}

struct Result
{
    dsp::BandSet bands;
    std::vector<std::string> lines;
};

Result run (std::vector<assist::ProposedBand> bands, Source source = Source::acousticGuitar, const analysis::AnalysisSummary* a = nullptr)
{
    assist::Proposal p;
    p.explanation = "x";
    p.bands = std::move (bands);
    assist::LimitReport report;
    auto out = assist::sanitize (p, source, a, report);
    return { out, report.adjustments };
}

bool hasLine (const Result& r, const std::string& text)
{
    for (const auto& l : r.lines)
        if (l.find (text) != std::string::npos)
            return true;
    return false;
}

std::string allLines (const Result& r)
{
    std::string s;
    for (const auto& l : r.lines)
        s += "[" + l + "] ";
    return s;
}

bool isUnused (const labq8::dsp::BandSettings& b, size_t slot)
{
    return b.bypassed && b.type == FilterType::bell && b.gainDb == 0.0 && b.q == 1.0
           && b.frequencyHz == labq8::params::defaultFrequencyHz[slot];
}

analysis::AnalysisSummary withCandidates()
{
    analysis::AnalysisSummary a;
    a.candidates = { { "c1", analysis::CandidateKind::excess, 300, 4, 0.8 },
                     { "c2", analysis::CandidateKind::resonance, 2480, 8, 0.04 } };
    return a;
}
} // namespace

LQ_TEST ("limits: a safe proposal passes unchanged, unused slots are defaults")
{
    const auto r = run ({ band (FilterType::bell, 250, -3, 1.4), band (FilterType::highShelf, 8000, 2, 0.7), band (FilterType::lowCut, 60, 0, 0.707) });
    LQ_CHECK (r.lines.empty(), "no adjustments: " + allLines (r));
    LQ_CHECK (r.bands[0].frequencyHz == 250 && r.bands[0].gainDb == -3 && r.bands[0].q == 1.4 && ! r.bands[0].bypassed, "band 1 kept");
    LQ_CHECK (r.bands[1].type == FilterType::highShelf && r.bands[1].gainDb == 2, "band 2 kept");
    LQ_CHECK (r.bands[2].type == FilterType::lowCut && r.bands[2].frequencyHz == 60, "band 3 kept");
    for (size_t i = 3; i < 8; ++i)
        LQ_CHECK (isUnused (r.bands[i], i), str ("slot ", i + 1, " is a bypassed default bell"));
}

LQ_TEST ("limits: at most 5 active bands, smallest |gain| discarded, cuts count")
{
    const auto r = run ({ band (FilterType::lowCut, 40, 0, 0.707),    // cut: kept
                          band (FilterType::bell, 200, -1.0, 1),     // smallest |gain|: discarded
                          band (FilterType::bell, 500, 4.0, 1),
                          band (FilterType::bell, 1000, -2.0, 1),    // second smallest: discarded
                          band (FilterType::bell, 3000, -5.0, 2),
                          band (FilterType::highCut, 18000, 0, 0.707),// cut: kept
                          band (FilterType::bell, 5000, 2.5, 1),
                          band (FilterType::bell, 7000, 3.0, 1, {}, true) }); // bypassed: not active, kept
    int active = 0;
    for (const auto& b : r.bands)
        active += b.bypassed ? 0 : 1;
    LQ_CHECK (active == 5, str ("5 active, got ", active));
    LQ_CHECK (isUnused (r.bands[1], 1) && isUnused (r.bands[3], 3), "the two smallest |gain| bells are discarded");
    LQ_CHECK (r.bands[0].type == FilterType::lowCut && ! r.bands[0].bypassed, "low cut kept");
    LQ_CHECK (r.bands[5].type == FilterType::highCut && ! r.bands[5].bypassed, "high cut kept");
    LQ_CHECK (r.bands[6].gainDb == 2.5 && ! r.bands[6].bypassed, "larger |gain| kept");
    LQ_CHECK (r.bands[7].bypassed && r.bands[7].frequencyHz == 7000 && r.bands[7].gainDb == 3.0, "bypassed band kept as is");
    LQ_CHECK (r.lines.size() == 2 && hasLine (r, "Banda 2 descartada") && hasLine (r, "Banda 4 descartada") && hasLine (r, "5 bandas activas"),
              "one line per discarded band: " + allLines (r));

    // Six cuts: the last one goes.
    std::vector<assist::ProposedBand> cuts;
    for (int i = 0; i < 6; ++i)
        cuts.push_back (band (i % 2 == 0 ? FilterType::lowCut : FilterType::notch, 30.0 + 1000.0 * i, 0, i % 2 == 0 ? 0.7 : 2.0));
    const auto c = run (cuts);
    LQ_CHECK (isUnused (c.bands[5], 5) && ! c.bands[4].bypassed && hasLine (c, "Banda 6 descartada"), "the last cut is discarded");
}

LQ_TEST ("limits: gain -12..+6 dB")
{
    const auto r = run ({ band (FilterType::bell, 1000, 9, 1), band (FilterType::lowShelf, 200, -15, 0.7) });
    LQ_CHECK (r.bands[0].gainDb == 6.0 && r.bands[1].gainDb == -12.0, "gains clamped");
    LQ_CHECK (r.lines.size() == 2 && hasLine (r, "Banda 1: ganancia 9.0 dB ajustada a 6.0 dB") && hasLine (r, "Banda 2: ganancia -15.0 dB ajustada a -12.0 dB"),
              allLines (r));
}

LQ_TEST ("limits: bell Q 0.5..8")
{
    const auto r = run ({ band (FilterType::bell, 1000, -3, 12), band (FilterType::bell, 2000, -3, 0.2) });
    LQ_CHECK (r.bands[0].q == 8.0 && r.bands[1].q == 0.5, "Q clamped");
    LQ_CHECK (r.lines.size() == 2 && hasLine (r, "Banda 1: Q 12.00 ajustada a 8.00") && hasLine (r, "Banda 2: Q 0.20 ajustada a 0.50"), allLines (r));
}

LQ_TEST ("limits: notch Q up to 18 only on a measured resonance")
{
    const auto a = withCandidates();
    const auto onResonance = run ({ band (FilterType::notch, 2480, 0, 15, "c2") }, Source::acousticGuitar, &a);
    LQ_CHECK (onResonance.bands[0].q == 15.0 && onResonance.lines.empty(), "Q 15 kept on a resonance: " + allLines (onResonance));
    const auto above = run ({ band (FilterType::notch, 2480, 0, 25, "c2") }, Source::acousticGuitar, &a);
    LQ_CHECK (above.bands[0].q == 18.0 && hasLine (above, "Q 25.00 ajustada a 18.00"), "resonance notch capped at 18: " + allLines (above));

    const auto onExcess = run ({ band (FilterType::notch, 300, 0, 15, "c1") }, Source::acousticGuitar, &a);
    LQ_CHECK (onExcess.bands[0].q == 8.0 && hasLine (onExcess, "Banda 1: Q 15.00 ajustada a 8.00"), "not a resonance: 8. " + allLines (onExcess));
    const auto noCandidate = run ({ band (FilterType::notch, 300, 0, 15) }, Source::acousticGuitar, &a);
    LQ_CHECK (noCandidate.bands[0].q == 8.0 && noCandidate.lines.size() == 1, "no candidate: 8");
    const auto noAnalysis = run ({ band (FilterType::notch, 2480, 0, 15, "c2") });
    LQ_CHECK (noAnalysis.bands[0].q == 8.0 && noAnalysis.lines.size() == 1, "no analysis: 8");
    const auto unknownId = run ({ band (FilterType::notch, 2480, 0, 15, "c9") }, Source::acousticGuitar, &a);
    LQ_CHECK (unknownId.bands[0].q == 8.0, "unknown candidate id: 8");
}

LQ_TEST ("limits: shelf Q 0.5..1")
{
    const auto r = run ({ band (FilterType::lowShelf, 200, 3, 4), band (FilterType::highShelf, 8000, 2, 0.2), band (FilterType::highShelf, 9000, 2, 0.8) });
    LQ_CHECK (r.bands[0].q == 1.0 && r.bands[1].q == 0.5 && r.bands[2].q == 0.8, "shelf Q clamped");
    LQ_CHECK (r.lines.size() == 2 && hasLine (r, "Banda 1: Q 4.00 ajustada a 1.00") && hasLine (r, "Banda 2: Q 0.20 ajustada a 0.50"), allLines (r));
}

LQ_TEST ("limits: cut Q 0.5..0.707")
{
    const auto r = run ({ band (FilterType::lowCut, 60, 0, 2.0), band (FilterType::highCut, 15000, 0, 18.0),
                          band (FilterType::lowCut, 50, 0, 0.1), band (FilterType::highCut, 16000, 0, 0.6) });
    LQ_CHECK (r.bands[0].q == 0.707 && r.bands[1].q == 0.707 && r.bands[2].q == 0.5 && r.bands[3].q == 0.6, "cut Q clamped");
    LQ_CHECK (r.lines.size() == 3 && hasLine (r, "Banda 1: Q 2.000 ajustada a 0.707") && hasLine (r, "Banda 2: Q 18.000 ajustada a 0.707")
                  && hasLine (r, "Banda 3: Q 0.100 ajustada a 0.500"), allLines (r));
}

LQ_TEST ("limits: lines use '.' decimals in any locale")
{
    // Both the C locale (printf) and the C++ global locale set to Spanish.
    const std::string oldC = std::setlocale (LC_NUMERIC, nullptr);
    const auto oldGlobal = std::locale();
    const char* names[] = { "es_ES.UTF-8", "es_ES", "de_DE.UTF-8", "fr_FR.UTF-8" };
    const char* used = nullptr;
    for (auto n : names)
        if (std::setlocale (LC_NUMERIC, n) != nullptr)
        {
            used = n;
            break;
        }
    if (used == nullptr)
    {
        ctx.skip ("no comma-decimal locale installed");
        return;
    }
    try { std::locale::global (std::locale (used)); } catch (...) {}

    char probe[16];
    std::snprintf (probe, sizeof (probe), "%.1f", 9.0);
    ctx.note (str ("locale ", used, ": printf gives ", probe));
    const auto r = run ({ band (FilterType::bell, 1000, 9, 1) });

    std::locale::global (oldGlobal);
    std::setlocale (LC_NUMERIC, oldC.c_str());

    LQ_CHECK (std::string (probe) == "9,0", "the locale really uses a decimal comma (the test is meaningful)");
    LQ_CHECK (hasLine (r, "Banda 1: ganancia 9.0 dB ajustada a 6.0 dB"), "'.' decimals: " + allLines (r));
}

LQ_TEST ("limits: frequency 20..20000 Hz")
{
    const auto r = run ({ band (FilterType::bell, 10, -3, 1), band (FilterType::highCut, 25000, 0, 0.7) });
    LQ_CHECK (r.bands[0].frequencyHz == 20.0 && r.bands[1].frequencyHz == 20000.0, "frequency clamped");
    LQ_CHECK (r.lines.size() == 2 && hasLine (r, "Banda 1: frecuencia 10 Hz ajustada a 20 Hz") && hasLine (r, "Banda 2: frecuencia 25000 Hz ajustada a 20000 Hz"),
              allLines (r));
}

LQ_TEST ("limits: voice, no boosts below 80 Hz")
{
    const auto r = run ({ band (FilterType::bell, 60, 3, 1), band (FilterType::lowShelf, 70, 2, 0.7),
                          band (FilterType::bell, 60, -3, 1), band (FilterType::bell, 120, 3, 1) }, Source::voice);
    LQ_CHECK (isUnused (r.bands[0], 0) && isUnused (r.bands[1], 1), "boosts below 80 Hz discarded");
    LQ_CHECK (r.bands[2].gainDb == -3 && ! r.bands[2].bypassed, "a cut below 80 Hz stays");
    LQ_CHECK (r.bands[3].gainDb == 3 && ! r.bands[3].bypassed, "a boost above 80 Hz stays");
    LQ_CHECK (r.lines.size() == 2 && hasLine (r, "Banda 1 descartada (voz)") && hasLine (r, "Banda 2 descartada (voz)"), allLines (r));

    const auto other = run ({ band (FilterType::bell, 60, 3, 1) }, Source::bass);
    LQ_CHECK (other.lines.empty() && other.bands[0].gainDb == 3, "other sources may boost below 80 Hz");
}

LQ_TEST ("limits: voice low cut <= 150 Hz")
{
    const auto r = run ({ band (FilterType::lowCut, 220, 0, 0.707), band (FilterType::lowCut, 120, 0, 0.707) }, Source::voice);
    LQ_CHECK (r.bands[0].frequencyHz == 150.0 && r.bands[1].frequencyHz == 120.0, "low cut capped at 150 Hz");
    LQ_CHECK (r.lines.size() == 1 && hasLine (r, "Banda 1 (voz): corte de graves en 220 Hz bajado a 150 Hz"), allLines (r));
}

LQ_TEST ("limits: bass low cut <= 50 Hz")
{
    const auto r = run ({ band (FilterType::lowCut, 80, 0, 0.707) }, Source::bass);
    LQ_CHECK (r.bands[0].frequencyHz == 50.0, "low cut capped at 50 Hz");
    LQ_CHECK (r.lines.size() == 1 && hasLine (r, "Banda 1 (bajo): corte de graves en 80 Hz bajado a 50 Hz"), allLines (r));
    const auto guitar = run ({ band (FilterType::lowCut, 80, 0, 0.707) }, Source::electricGuitar);
    LQ_CHECK (guitar.bands[0].frequencyHz == 80.0 && guitar.lines.empty(), "other sources keep it");
}

LQ_TEST ("limits: mix gain -6..+4 dB")
{
    const auto r = run ({ band (FilterType::bell, 1000, 5, 1), band (FilterType::highShelf, 9000, -8, 0.7), band (FilterType::bell, 300, -4, 1) }, Source::mix);
    LQ_CHECK (r.bands[0].gainDb == 4.0 && r.bands[1].gainDb == -6.0 && r.bands[2].gainDb == -4.0, "mix gains clamped");
    LQ_CHECK (r.lines.size() == 2 && hasLine (r, "Banda 1 (mezcla): ganancia 5.0 dB ajustada a 4.0 dB")
                  && hasLine (r, "Banda 2 (mezcla): ganancia -8.0 dB ajustada a -6.0 dB"), allLines (r));
}

LQ_TEST ("limits: non-finite values and more than 8 bands are discarded")
{
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    std::vector<assist::ProposedBand> v { band (FilterType::bell, nan, -3, 1), band (FilterType::bell, 500, -3, std::numeric_limits<double>::infinity()) };
    for (int i = 0; i < 7; ++i)
        v.push_back (band (FilterType::bell, 1000, 0, 1, {}, true));
    const auto r = run (v);
    LQ_CHECK (isUnused (r.bands[0], 0) && isUnused (r.bands[1], 1), "non-finite bands discarded");
    LQ_CHECK (hasLine (r, "Banda 1 descartada: valores no v") && hasLine (r, "Banda 2 descartada: valores no v"), allLines (r));
    LQ_CHECK (hasLine (r, "Banda 9 descartada"), "9th band discarded: " + allLines (r));

    // Cuts ignore gain: it is zeroed without a line.
    const auto cut = run ({ band (FilterType::lowCut, 60, 30, 0.7) });
    LQ_CHECK (cut.bands[0].gainDb == 0.0 && cut.lines.empty(), "cut gain zeroed silently");
}

namespace
{
// Combined response (dB) of the active gain bands at the 48 kHz reference rate.
double combinedDb (const dsp::BandSet& bands, double hz)
{
    double db = 0.0;
    for (const auto& b : bands)
        if (! b.bypassed && labq8::dsp::usesGain (b.type))
            db += 20.0 * std::log10 (labq8::dsp::magnitudeAt (labq8::dsp::designFilter (b.type, 48000.0, b.frequencyHz, b.gainDb, b.q), hz, 48000.0));
    return db;
}

// Max and min of the combined response on a fine grid (1/48 octave, 20 Hz..20 kHz)
// restricted to [lo, hi].
std::pair<double, double> combinedRange (const dsp::BandSet& bands, double lo = 20.0, double hi = 20000.0)
{
    double mx = -1.0e9, mn = 1.0e9;
    for (double f = 20.0; f <= 20000.0 * 1.0001; f *= std::pow (2.0, 1.0 / 48.0))
        if (f >= lo && f <= hi)
        {
            const auto db = combinedDb (bands, f);
            mx = std::max (mx, db);
            mn = std::min (mn, db);
        }
    return { mx, mn };
}
} // namespace

LQ_TEST ("limits: stacked boosts are limited to +6 dB combined")
{
    std::vector<assist::ProposedBand> five;
    for (int i = 0; i < 5; ++i)
        five.push_back (band (FilterType::bell, 1000, 6, 1));
    const auto r = run (five);
    const auto [mx, mn] = combinedRange (r.bands);
    const auto atCentre = combinedDb (r.bands, 1000.0);
    LQ_CHECK (mx <= 6.05 && atCentre <= 6.05, str ("combined max ", mx, " dB, at 1 kHz ", atCentre, " dB"));
    LQ_CHECK (atCentre >= 5.9, str ("still close to the limit: ", atCentre));
    for (size_t i = 0; i < 5; ++i)
        LQ_CHECK (! r.bands[i].bypassed && r.bands[i].gainDb > 0.0 && r.bands[i].gainDb < 6.0, str ("band ", i + 1, " scaled, not dropped: ", r.bands[i].gainDb));
    LQ_CHECK (r.lines.size() == 1 && hasLine (r, "combinad"), "one line for the scaling: " + allLines (r));
    ctx.note (allLines (r));

    // Shelf + bells: low shelf +6 under two +6 bells in its plateau.
    const auto s = run ({ band (FilterType::lowShelf, 300, 6, 0.7), band (FilterType::bell, 100, 6, 1), band (FilterType::bell, 150, 6, 2) });
    const auto [smx, smn] = combinedRange (s.bands);
    LQ_CHECK (smx <= 6.05, str ("shelf + bells combined max ", smx));
    LQ_CHECK (hasLine (s, "combinad"), "a line: " + allLines (s));

    // Stacked cuts: five -12 dB bells down to -12 dB combined (mix: -6).
    std::vector<assist::ProposedBand> cuts;
    for (int i = 0; i < 5; ++i)
        cuts.push_back (band (FilterType::bell, 2000, -12, 2));
    const auto c = run (cuts);
    const auto [cmx, cmn] = combinedRange (c.bands);
    LQ_CHECK (cmn >= -12.05 && combinedDb (c.bands, 2000.0) >= -12.05, str ("combined min ", cmn));
    LQ_CHECK (c.lines.size() == 1, "one line: " + allLines (c));
    const auto m = run ({ band (FilterType::bell, 2000, -6, 2), band (FilterType::highShelf, 1500, -6, 0.7) }, Source::mix);
    LQ_CHECK (combinedRange (m.bands).second >= -6.05, str ("mix combined min ", combinedRange (m.bands).second));
    const auto mb = run ({ band (FilterType::bell, 5000, 4, 1), band (FilterType::bell, 5000, 4, 1) }, Source::mix);
    LQ_CHECK (combinedRange (mb.bands).first <= 4.05, str ("mix combined max ", combinedRange (mb.bands).first));

    // Separate boosts do not interact: nothing changes.
    const auto apart = run ({ band (FilterType::bell, 100, 6, 4), band (FilterType::bell, 5000, 6, 4) });
    LQ_CHECK (apart.lines.empty() && apart.bands[0].gainDb == 6.0 && apart.bands[1].gainDb == 6.0, "separate boosts kept: " + allLines (apart));
}

LQ_TEST ("limits: voice, combined boost below 80 Hz at most 0.5 dB")
{
    const auto r = run ({ band (FilterType::lowShelf, 81, 6, 0.7), band (FilterType::bell, 2000, 3, 1) }, Source::voice);
    const auto below = combinedRange (r.bands, 20.0, 80.0).first;
    LQ_CHECK (below <= 0.55, str ("combined boost below 80 Hz: ", below, " dB. ", allLines (r)));
    LQ_CHECK (hasLine (r, "Banda 1") && hasLine (r, "80 Hz"), "a line for the shelf: " + allLines (r));
    ctx.note (allLines (r));
    LQ_CHECK (r.bands[1].gainDb == 3.0 && ! r.bands[1].bypassed, "an unrelated boost stays");

    // Two bells just above 80 Hz.
    const auto two = run ({ band (FilterType::bell, 90, 4, 1), band (FilterType::bell, 100, 4, 1) }, Source::voice);
    LQ_CHECK (combinedRange (two.bands, 20.0, 80.0).first <= 0.55, str ("two bells: ", combinedRange (two.bands, 20.0, 80.0).first));

    // Other sources: unchanged.
    const auto bass = run ({ band (FilterType::lowShelf, 81, 6, 0.7) }, Source::bass);
    LQ_CHECK (bass.lines.empty() && bass.bands[0].gainDb == 6.0, "bass keeps its low shelf");
}
