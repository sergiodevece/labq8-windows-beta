// Exact float comparisons are intentional (limits are exact constants).
#pragma clang diagnostic ignored "-Wfloat-equal"

// Tests of the per-source safety limits applied to every proposal.

#include "TestHarness.h"

#include "assist/ProposalLimits.h"
#include "parameters/ParameterIDs.h"

#include <limits>

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
    const auto r = run ({ band (FilterType::bell, 250, -3, 1.4), band (FilterType::highShelf, 8000, 2, 0.7), band (FilterType::lowCut, 60, 0, 0.71) });
    LQ_CHECK (r.lines.empty(), "no adjustments: " + allLines (r));
    LQ_CHECK (r.bands[0].frequencyHz == 250 && r.bands[0].gainDb == -3 && r.bands[0].q == 1.4 && ! r.bands[0].bypassed, "band 1 kept");
    LQ_CHECK (r.bands[1].type == FilterType::highShelf && r.bands[1].gainDb == 2, "band 2 kept");
    LQ_CHECK (r.bands[2].type == FilterType::lowCut && r.bands[2].frequencyHz == 60, "band 3 kept");
    for (size_t i = 3; i < 8; ++i)
        LQ_CHECK (isUnused (r.bands[i], i), str ("slot ", i + 1, " is a bypassed default bell"));
}

LQ_TEST ("limits: at most 5 active bands, smallest |gain| discarded, cuts count")
{
    const auto r = run ({ band (FilterType::lowCut, 40, 0, 0.71),    // cut: kept
                          band (FilterType::bell, 200, -1.0, 1),     // smallest |gain|: discarded
                          band (FilterType::bell, 500, 4.0, 1),
                          band (FilterType::bell, 1000, -2.0, 1),    // second smallest: discarded
                          band (FilterType::bell, 3000, -5.0, 2),
                          band (FilterType::highCut, 18000, 0, 0.71),// cut: kept
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
        cuts.push_back (band (i % 2 == 0 ? FilterType::lowCut : FilterType::notch, 30.0 + 1000.0 * i, 0, 2));
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
    const auto r = run ({ band (FilterType::lowCut, 220, 0, 0.71), band (FilterType::lowCut, 120, 0, 0.71) }, Source::voice);
    LQ_CHECK (r.bands[0].frequencyHz == 150.0 && r.bands[1].frequencyHz == 120.0, "low cut capped at 150 Hz");
    LQ_CHECK (r.lines.size() == 1 && hasLine (r, "Banda 1 (voz): corte de graves en 220 Hz bajado a 150 Hz"), allLines (r));
}

LQ_TEST ("limits: bass low cut <= 50 Hz")
{
    const auto r = run ({ band (FilterType::lowCut, 80, 0, 0.71) }, Source::bass);
    LQ_CHECK (r.bands[0].frequencyHz == 50.0, "low cut capped at 50 Hz");
    LQ_CHECK (r.lines.size() == 1 && hasLine (r, "Banda 1 (bajo): corte de graves en 80 Hz bajado a 50 Hz"), allLines (r));
    const auto guitar = run ({ band (FilterType::lowCut, 80, 0, 0.71) }, Source::electricGuitar);
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
