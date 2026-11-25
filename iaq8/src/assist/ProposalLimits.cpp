#include "assist/ProposalLimits.h"

#include "dsp/BandLimits.h"
#include "parameters/ParameterIDs.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace iaq8::assist
{
namespace
{
using labq8::dsp::FilterType;

// Fixed-point text with '.' as decimal separator whatever the process or
// C++ global locale is (the host may change either).
std::string fixed (double v, int decimals)
{
    std::ostringstream os;
    os.imbue (std::locale::classic());
    os << std::fixed << std::setprecision (decimals) << v;
    return os.str();
}

// Patterns use {n} (band number), {a} and {b} (old and new value).
std::string makeLine (std::string pattern, int band, const std::string& a = {}, const std::string& b = {})
{
    const auto replace = [&pattern] (const std::string& key, const std::string& value)
    {
        if (const auto i = pattern.find (key); i != std::string::npos)
            pattern.replace (i, key.size(), value);
    };
    replace ("{n}", std::to_string (band));
    replace ("{a}", a);
    replace ("{b}", b);
    return pattern;
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

// Clamps `value` into [lo, hi]; on a change adds `pattern` with the old and
// new values written with `decimals` decimals.
void clampInto (double& value, double lo, double hi, int decimals, const char* pattern, int band, LimitReport& report)
{
    if (value < lo || value > hi)
    {
        const auto clamped = std::clamp (value, lo, hi);
        report.adjustments.push_back (makeLine (pattern, band, fixed (value, decimals), fixed (clamped, decimals)));
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
        report.adjustments.push_back (makeLine ("Banda {n} descartada: como mucho 8 bandas", (int) i + 1));

    const bool isVoice = source == Source::voice, isBass = source == Source::bass, isMix = source == Source::mix;

    for (size_t i = 0; i < std::min (proposal.bands.size(), maxBands); ++i)
    {
        const int n = (int) i + 1;
        auto b = proposal.bands[i].settings;

        if (! std::isfinite (b.frequencyHz) || ! std::isfinite (b.gainDb) || ! std::isfinite (b.q))
        {
            report.adjustments.push_back (makeLine ("Banda {n} descartada: valores no válidos", n));
            continue;
        }

        clampInto (b.frequencyHz, limits::minFrequencyHz, limits::maxFrequencyHz, 0, "Banda {n}: frecuencia {a} Hz ajustada a {b} Hz", n, report);
        if (b.type == FilterType::lowCut && isVoice)
            clampInto (b.frequencyHz, 0.0, limits::voiceMaxLowCutHz, 0, "Banda {n} (voz): corte de graves en {a} Hz bajado a {b} Hz", n, report);
        if (b.type == FilterType::lowCut && isBass)
            clampInto (b.frequencyHz, 0.0, limits::bassMaxLowCutHz, 0, "Banda {n} (bajo): corte de graves en {a} Hz bajado a {b} Hz", n, report);

        if (isCutLike (b.type))
        {
            b.gainDb = 0.0; // ignored by cuts and the notch: not an adjustment
        }
        else
        {
            clampInto (b.gainDb, limits::minGainDb, limits::maxGainDb, 1, "Banda {n}: ganancia {a} dB ajustada a {b} dB", n, report);
            if (isMix)
                clampInto (b.gainDb, limits::mixMinGainDb, limits::mixMaxGainDb, 1, "Banda {n} (mezcla): ganancia {a} dB ajustada a {b} dB", n, report);
            if (isVoice && b.gainDb > 0.0 && b.frequencyHz < limits::voiceNoBoostBelowHz)
            {
                report.adjustments.push_back (makeLine ("Banda {n} descartada (voz): sin realces por debajo de 80 Hz", n));
                continue;
            }
        }

        constexpr const char* qLine = "Banda {n}: Q {a} ajustada a {b}";
        switch (b.type)
        {
            case FilterType::bell:
                clampInto (b.q, limits::minBellQ, limits::maxBellQ, 2, qLine, n, report);
                break;
            case FilterType::notch:
                clampInto (b.q, limits::minBellQ,
                           namesResonance (proposal.bands[i].candidateId, analysis) ? limits::maxResonanceNotchQ : limits::maxBellQ,
                           2, qLine, n, report);
                break;
            case FilterType::lowShelf:
            case FilterType::highShelf:
                clampInto (b.q, limits::minShelfQ, limits::maxShelfQ, 2, qLine, n, report);
                break;
            case FilterType::lowCut:
            case FilterType::highCut:
                clampInto (b.q, limits::minCutQ, limits::maxCutQ, 3, qLine, n, report);
                break;
        }

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
        report.adjustments.push_back (makeLine ("Banda {n} descartada: como mucho 5 bandas activas", (int) victim + 1));
        out[victim] = unusedBand (victim);
        used[victim] = false;
    }
    return out;
}
} // namespace iaq8::assist
