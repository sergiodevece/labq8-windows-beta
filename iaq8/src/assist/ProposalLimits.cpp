#include "assist/ProposalLimits.h"

#include "dsp/BandLimits.h"
#include "dsp/BiquadDesign.h"
#include "parameters/ParameterIDs.h"

#include <algorithm>
#include <array>
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

// Combined response of the active gain bands (bell, shelves). Cuts and the
// notch only ever lower the level, so they are left out: a boost they would
// tame is still limited as if they were not there.
constexpr double referenceRateHz = 48000.0; // session independent
constexpr double toleranceDb = 0.01;        // what counts as over a limit
constexpr double contributorDb = 0.05;      // a band below this at the worst point is not scaled
constexpr int maxIterations = 48;

bool isGainBand (const labq8::dsp::BandSettings& b, bool used) { return used && ! b.bypassed && labq8::dsp::usesGain (b.type); }

double bandDb (const labq8::dsp::BandSettings& b, double hz)
{
    const auto c = labq8::dsp::designFilter (b.type, referenceRateHz, b.frequencyHz, b.gainDb, b.q);
    return 20.0 * std::log10 (labq8::dsp::magnitudeAt (c, hz, referenceRateHz));
}

// 1/12-octave grid 20 Hz..20 kHz plus every gain band's own frequency (a
// bell peaks exactly there, so stacked bells are measured at their peak).
std::vector<double> evaluationGrid (const dsp::BandSet& bands, const std::array<bool, 8>& used)
{
    std::vector<double> grid;
    for (int k = 0;; ++k)
    {
        const auto hz = limits::minFrequencyHz * std::pow (2.0, k / 12.0);
        if (hz >= limits::maxFrequencyHz)
            break;
        grid.push_back (hz);
    }
    grid.push_back (limits::maxFrequencyHz);
    for (size_t i = 0; i < bands.size(); ++i)
        if (isGainBand (bands[i], used[i]))
            grid.push_back (bands[i].frequencyHz);
    std::sort (grid.begin(), grid.end());
    return grid;
}

struct Worst
{
    double db = 0.0, hz = 0.0;
};

// The highest (sign > 0) or lowest (sign < 0) combined level of the gain
// bands among `only` (all of them when null) on the grid points below `belowHz`.
Worst worstPoint (const dsp::BandSet& bands, const std::array<bool, 8>& used, const std::vector<double>& grid,
                  double sign, double belowHz, const size_t* only = nullptr)
{
    Worst worst;
    bool first = true;
    for (const auto hz : grid)
    {
        if (hz >= belowHz)
            break;
        double db = 0.0;
        for (size_t i = 0; i < bands.size(); ++i)
            if (isGainBand (bands[i], used[i]) && (only == nullptr || *only == i))
                db += bandDb (bands[i], hz);
        if (first || sign * db > sign * worst.db)
            worst = { db, hz };
        first = false;
    }
    return worst;
}

std::string bandList (const std::vector<int>& numbers)
{
    std::string s = numbers.size() == 1 ? "Banda " : "Bandas ";
    for (size_t k = 0; k < numbers.size(); ++k)
        s += (k == 0 ? "" : ", ") + std::to_string (numbers[k]);
    return s;
}

struct Scaling
{
    std::vector<int> numbers; // 1-based slots whose gain changed, in slot order
    double beforeDb = 0.0, afterDb = 0.0; // worst combined level before and after
};

// Scales the gains of one sign (sign > 0: boosts, < 0: cuts) of the bands
// that make the combined level pass `limitDb` (on grid points below
// `belowHz`), all by the same factor, until it fits. Deterministic: a fixed
// number of linearised steps at the worst grid point; if that were ever not
// enough, those gains go to 0 (a 0 dB bell or shelf is flat, so the limit
// then holds).
Scaling limitCombined (dsp::BandSet& bands, const std::array<bool, 8>& used, double sign, double limitDb, double belowHz)
{
    const auto grid = evaluationGrid (bands, used);
    std::array<double, 8> original {};
    for (size_t i = 0; i < bands.size(); ++i)
        original[i] = bands[i].gainDb;

    Scaling result;
    const auto over = [&] (const Worst& w) { return sign * (w.db - limitDb) > toleranceDb; };
    auto worst = worstPoint (bands, used, grid, sign, belowHz);
    result.beforeDb = worst.db;

    for (int iteration = 0; iteration < maxIterations && over (worst); ++iteration)
    {
        std::array<bool, 8> scaled {};
        double contribution = 0.0;
        for (size_t i = 0; i < bands.size(); ++i)
        {
            if (! isGainBand (bands[i], used[i]) || sign * bands[i].gainDb <= 0.0)
                continue;
            const auto db = bandDb (bands[i], worst.hz);
            if (sign * db >= contributorDb)
            {
                scaled[i] = true;
                contribution += db;
            }
        }
        if (sign * contribution <= 0.0)
            break;
        // Linearised: the scaled bands' share times `factor` plus the rest fits.
        const auto factor = std::clamp ((limitDb - (worst.db - contribution)) / contribution, 0.0, 1.0);
        for (size_t i = 0; i < bands.size(); ++i)
            if (scaled[i])
                bands[i].gainDb *= factor;
        worst = worstPoint (bands, used, grid, sign, belowHz);
    }

    if (over (worst))
    {
        for (size_t i = 0; i < bands.size(); ++i)
            if (isGainBand (bands[i], used[i]) && sign * bands[i].gainDb > 0.0
                && sign * worstPoint (bands, used, grid, sign, belowHz, &i).db > 0.0)
                bands[i].gainDb = 0.0;
        worst = worstPoint (bands, used, grid, sign, belowHz);
    }

    for (size_t i = 0; i < bands.size(); ++i)
        if (bands[i].gainDb < original[i] || bands[i].gainDb > original[i])
            result.numbers.push_back ((int) i + 1);
    result.afterDb = worst.db;
    return result;
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

    // The combined response of the bands, not only each band (stacked bells
    // or a shelf under a bell add up). Lowering cuts can raise what a boost
    // next to them shows, so the passes repeat until none changes anything.
    const auto maxDb = isMix ? limits::mixMaxGainDb : limits::maxGainDb;
    const auto minDb = isMix ? limits::mixMinGainDb : limits::minGainDb;
    for (int round = 0; round < 4; ++round)
    {
        bool changedAny = false;
        if (isVoice)
        {
            const auto s = limitCombined (out, used, 1.0, limits::voiceMaxCombinedBoostBelowDb, limits::voiceNoBoostBelowHz);
            for (const auto n : s.numbers)
            {
                auto& b = out[(size_t) n - 1];
                const auto before = proposal.bands[(size_t) n - 1].settings.gainDb;
                if (b.gainDb < 0.05)
                {
                    report.adjustments.push_back (makeLine ("Banda {n} descartada (voz): sin realces por debajo de 80 Hz", n));
                    b = unusedBand ((size_t) n - 1);
                    used[(size_t) n - 1] = false;
                }
                else
                {
                    report.adjustments.push_back (makeLine ("Banda {n} (voz): ganancia {a} dB reducida a {b} dB, sin realces por debajo de 80 Hz", n,
                                                            fixed (std::min (before, maxDb), 1), fixed (b.gainDb, 1)));
                }
            }
            changedAny = changedAny || ! s.numbers.empty();
        }
        for (const auto sign : { 1.0, -1.0 })
        {
            const auto s = limitCombined (out, used, sign, sign > 0.0 ? maxDb : minDb, limits::maxFrequencyHz * 2.0);
            if (s.numbers.empty())
                continue;
            report.adjustments.push_back (bandList (s.numbers)
                                          + (sign > 0.0 ? ": realce combinado de " + fixed (s.beforeDb, 1) + " dB reducido a "
                                                        : ": atenuación combinada de " + fixed (s.beforeDb, 1) + " dB reducida a ")
                                          + fixed (s.afterDb, 1) + " dB");
            changedAny = true;
        }
        if (! changedAny)
            break;
    }
    return out;
}
} // namespace iaq8::assist
