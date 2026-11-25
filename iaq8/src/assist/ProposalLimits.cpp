#include "assist/ProposalLimits.h"

#include "dsp/BandLimits.h"
#include "parameters/ParameterIDs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace iaq8::assist
{
namespace
{
using labq8::dsp::FilterType;

// Lines use '.' as decimal separator (C locale), e.g. "Banda 1: ganancia 9.0 dB ajustada a 6.0 dB".
std::string format (const char* pattern, int band, double a, double b)
{
    char buffer[160];
    std::snprintf (buffer, sizeof (buffer), pattern, band, a, b);
    return buffer;
}

std::string format (const char* pattern, int band)
{
    char buffer[160];
    std::snprintf (buffer, sizeof (buffer), pattern, band);
    return buffer;
}

// The texts below spell these values out.
static_assert (limits::maxActiveBands == 5 && maxBands == 8 && limits::voiceNoBoostBelowHz == 80.0);

bool isCutLike (FilterType t) { return ! labq8::dsp::usesGain (t); } // cuts and notch

bool namesResonance (const std::string& candidateId, const analysis::AnalysisSummary* analysis)
{
    if (analysis == nullptr || candidateId.empty())
        return false;
    for (const auto& c : analysis->candidates)
        if (c.id == candidateId)
            return c.kind == analysis::CandidateKind::resonance;
    return false;
}

// Clamps `value` into [lo, hi]; on a change adds `pattern` (band, old, new).
void clampInto (double& value, double lo, double hi, const char* pattern, int band, LimitReport& report)
{
    if (value < lo || value > hi)
    {
        const auto clamped = std::clamp (value, lo, hi);
        report.adjustments.push_back (format (pattern, band, value, clamped));
        value = clamped;
    }
}
} // namespace

labq8::dsp::BandSettings unusedBand (size_t slot)
{
    return { labq8::params::defaultFrequencyHz[slot % labq8::params::defaultFrequencyHz.size()],
             labq8::dsp::limits::defaultGainDb, labq8::dsp::limits::defaultQ, true, FilterType::bell };
}

dsp::BandSet sanitize (const Proposal& proposal, Source source, const analysis::AnalysisSummary* analysis, LimitReport& report)
{
    dsp::BandSet out {};
    std::array<bool, 8> used {};
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = unusedBand (i);

    for (size_t i = maxBands; i < proposal.bands.size(); ++i)
        report.adjustments.push_back (format ("Banda %d descartada: como mucho 8 bandas", (int) i + 1));

    const bool isVoice = source == Source::voice, isBass = source == Source::bass, isMix = source == Source::mix;

    for (size_t i = 0; i < std::min (proposal.bands.size(), maxBands); ++i)
    {
        const int n = (int) i + 1;
        auto b = proposal.bands[i].settings;

        if (! std::isfinite (b.frequencyHz) || ! std::isfinite (b.gainDb) || ! std::isfinite (b.q))
        {
            report.adjustments.push_back (format ("Banda %d descartada: valores no válidos", n));
            continue;
        }

        clampInto (b.frequencyHz, limits::minFrequencyHz, limits::maxFrequencyHz, "Banda %d: frecuencia %.0f Hz ajustada a %.0f Hz", n, report);
        if (b.type == FilterType::lowCut && isVoice)
            clampInto (b.frequencyHz, 0.0, limits::voiceMaxLowCutHz, "Banda %d (voz): corte de graves en %.0f Hz bajado a %.0f Hz", n, report);
        if (b.type == FilterType::lowCut && isBass)
            clampInto (b.frequencyHz, 0.0, limits::bassMaxLowCutHz, "Banda %d (bajo): corte de graves en %.0f Hz bajado a %.0f Hz", n, report);

        if (isCutLike (b.type))
        {
            b.gainDb = 0.0; // ignored by cuts and the notch: not an adjustment
        }
        else
        {
            clampInto (b.gainDb, limits::minGainDb, limits::maxGainDb, "Banda %d: ganancia %.1f dB ajustada a %.1f dB", n, report);
            if (isMix)
                clampInto (b.gainDb, limits::mixMinGainDb, limits::mixMaxGainDb, "Banda %d (mezcla): ganancia %.1f dB ajustada a %.1f dB", n, report);
            if (isVoice && b.gainDb > 0.0 && b.frequencyHz < limits::voiceNoBoostBelowHz)
            {
                report.adjustments.push_back (format ("Banda %d descartada (voz): sin realces por debajo de 80 Hz", n));
                continue;
            }
        }

        if (b.type == FilterType::bell)
            clampInto (b.q, limits::minBellQ, limits::maxBellQ, "Banda %d: Q %.2f ajustada a %.2f", n, report);
        else if (b.type == FilterType::notch)
            clampInto (b.q, limits::minBellQ,
                       namesResonance (proposal.bands[i].candidateId, analysis) ? limits::maxResonanceNotchQ : limits::maxBellQ,
                       "Banda %d: Q %.2f ajustada a %.2f", n, report);
        else
            clampInto (b.q, labq8::dsp::limits::minQ, labq8::dsp::limits::maxQ, "Banda %d: Q %.2f ajustada a %.2f", n, report);

        out[i] = b;
        used[i] = true;
    }

    // At most maxActiveBands active: drop gain bands by smallest |gain| first,
    // then cuts/notches; ties (and cuts among themselves) drop the later slot.
    for (;;)
    {
        int active = 0;
        for (size_t i = 0; i < out.size(); ++i)
            active += (used[i] && ! out[i].bypassed) ? 1 : 0;
        if (active <= limits::maxActiveBands)
            break;

        size_t victim = out.size();
        for (size_t i = 0; i < out.size(); ++i)
        {
            if (! used[i] || out[i].bypassed)
                continue;
            if (victim == out.size())
            {
                victim = i;
                continue;
            }
            const auto& v = out[victim];
            const auto& c = out[i];
            const bool vCut = isCutLike (v.type), cCut = isCutLike (c.type);
            const bool better = (vCut != cCut) ? vCut               // a gain band goes before any cut
                              : vCut ? true                          // cuts: the later slot
                                     : std::abs (c.gainDb) <= std::abs (v.gainDb); // smallest |gain|, later slot on ties
            if (better)
                victim = i;
        }
        report.adjustments.push_back (format ("Banda %d descartada: como mucho 5 bandas activas", (int) victim + 1));
        out[victim] = unusedBand (victim);
        used[victim] = false;
    }
    return out;
}
} // namespace iaq8::assist
