#include "analysis/TrackAnalyzer.h"

#include "analysis/AnalysisTuning.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace iaq8::analysis
{
namespace
{
namespace tn = tuning;

double ampToDbfs (double amplitude) { return amplitude > 0 ? std::max (tn::floorDb, 20.0 * std::log10 (amplitude)) : tn::floorDb; }
// Density in dB re 1 FS^2/Hz for the internal curves (1/3 oct, 1/24 oct,
// reference): only a very low floor, so quiet or band-limited material does
// not saturate the top of the fit range. The -120 dB floor is applied by
// outputDb, only to the spectrumDb that is reported.
inline constexpr double internalMinDensity = 1.0e-30;
double densityDb (double density) { return 10.0 * std::log10 (std::max (density, internalMinDensity)); }
double outputDb (double density) { return std::max (tn::floorDb, densityDb (density)); }

//==============================================================================
// One-sided Welch power density of x (FS^2/Hz per bin), with band averages.
class LongTermSpectrum
{
public:
    LongTermSpectrum (const std::vector<float>& x, double sampleRate)
    {
        const auto order = tn::fftOrderFor (sampleRate);
        const auto size = size_t (1) << order;
        const auto hop = std::max<size_t> (1, (size_t) std::lround ((double) size * (1.0 - tn::welchOverlap)));
        binHz = sampleRate / (double) size;

        std::vector<double> window (size);
        double windowPower = 0;
        for (size_t i = 0; i < size; ++i) // periodic Hann
        {
            window[i] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (double) i / (double) size);
            windowPower += window[i] * window[i];
        }

        const auto bins = size / 2 + 1;
        psd.assign (bins, 0.0);
        juce::dsp::FFT fft (order);
        std::vector<float> buf (2 * size);
        size_t segments = 0;

        for (size_t start = 0; start + size <= x.size(); start += hop, ++segments)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (size_t i = 0; i < size; ++i)
                buf[i] = (float) (x[start + i] * window[i]);

            fft.performRealOnlyForwardTransform (buf.data(), true);

            for (size_t k = 0; k < bins; ++k)
                psd[k] += (double) buf[2 * k] * buf[2 * k] + (double) buf[2 * k + 1] * buf[2 * k + 1];
        }

        const auto norm = 1.0 / ((double) std::max<size_t> (1, segments) * sampleRate * windowPower);
        for (size_t k = 0; k < bins; ++k)
            psd[k] *= (k == 0 || k == bins - 1 ? 1.0 : 2.0) * norm;

        prefix.assign (bins + 1, 0.0);
        for (size_t k = 0; k < bins; ++k)
            prefix[k + 1] = prefix[k] + psd[k];
    }

    // Mean density over [loHz, hiHz]; each bin is constant over its own width.
    double density (double loHz, double hiHz) const
    {
        const auto lo = clampBins (loHz / binHz), hi = clampBins (hiHz / binHz);
        if (hi - lo < 1.0e-9)
            return psd[(size_t) std::clamp (std::lround (lo), 0L, (long) psd.size() - 1)];
        return (cumulative (hi) - cumulative (lo)) / (hi - lo);
    }

    double bandDensity (double centreHz, double widthOct) const
    {
        return density (centreHz * std::exp2 (-0.5 * widthOct), centreHz * std::exp2 (0.5 * widthOct));
    }

    // Energy (FS^2) between loHz and hiHz.
    double energy (double loHz, double hiHz) const
    {
        return (cumulative (clampBins (hiHz / binHz)) - cumulative (clampBins (loHz / binHz))) * binHz;
    }

    double getBinHz() const { return binHz; }

private:
    std::vector<double> psd, prefix;
    double binHz = 1;

    double clampBins (double u) const { return std::clamp (u, -0.5, (double) psd.size() - 0.5); }

    // Integral of the density from bin -0.5 to u (in bins).
    double cumulative (double u) const
    {
        const auto j = (size_t) std::floor (u + 0.5);
        if (j >= psd.size())
            return prefix.back();
        return prefix[j] + psd[j] * (u + 0.5 - (double) j);
    }
};

//==============================================================================
// Log-frequency detection grid: x[i] = log2 (gridStartHz) + i * gridStepOct.
struct Grid
{
    double x0 = 0, step = tn::gridStepOct;
    int size = 0;

    double x (double index) const { return x0 + index * step; }
    double hz (double index) const { return std::exp2 (x (index)); }
    int firstAtOrAbove (double f) const { return std::max (0, (int) std::ceil ((std::log2 (f) - x0) / step - 1.0e-9)); }
    int lastAtOrBelow (double f) const { return std::min (size - 1, (int) std::floor ((std::log2 (f) - x0) / step + 1.0e-9)); }
};

struct Peak { double index, value; };

// Parabolic refinement of v's maximum at k, using neighbours inside [lo, hi].
Peak refinePeak (const std::vector<double>& v, int k, int lo, int hi)
{
    if (k <= lo || k >= hi)
        return { (double) k, v[(size_t) k] };
    const auto a = v[(size_t) k - 1], b = v[(size_t) k], c = v[(size_t) k + 1];
    const auto den = a - 2.0 * b + c;
    if (den >= 0)
        return { (double) k, b };
    const auto p = std::clamp (0.5 * (a - c) / den, -0.5, 0.5);
    return { (double) k + p, b - 0.25 * (a - c) * p };
}

// Fractional index where v crosses thr between i and j (v[i] > thr >= v[j]).
double crossing (const std::vector<double>& v, int i, int j, double thr)
{
    const auto vi = v[(size_t) i], vj = v[(size_t) j];
    return (double) i + (double) (j - i) * (vi - thr) / (vi - vj);
}

// A contiguous run of v > thr inside [lo, hi]: its edges (fractional
// indices, interpolated at the crossings) and its refined maximum.
struct Run { double left, right; Peak peak; };

std::vector<Run> runsAbove (const std::vector<double>& v, int lo, int hi, double thr)
{
    std::vector<Run> runs;
    for (int i = lo; i <= hi; ++i)
    {
        if (v[(size_t) i] <= thr)
            continue;

        auto end = i, best = i;
        while (end + 1 <= hi && v[(size_t) end + 1] > thr)
            if (v[(size_t) ++end] > v[(size_t) best])
                best = end;

        Run r;
        r.left = i > lo ? crossing (v, i, i - 1, thr) : (double) i;
        r.right = end < hi ? crossing (v, end, end + 1, thr) : (double) end;
        r.peak = refinePeak (v, best, lo, hi);
        runs.push_back (r);
        i = end;
    }
    return runs;
}

// Width (in indices) of the region around peak index k where v > level.
double widthAbove (const std::vector<double>& v, int k, int lo, int hi, double level)
{
    auto l = k, r = k;
    while (l > lo && v[(size_t) l - 1] > level) --l;
    while (r < hi && v[(size_t) r + 1] > level) ++r;
    const auto left = l > lo ? crossing (v, l, l - 1, level) : (double) l;
    const auto right = r < hi ? crossing (v, r, r + 1, level) : (double) r;
    return right - left;
}

struct Line { double slope = 0, intercept = 0; };

// Least-squares line of y against grid.x over the points of [lo, hi] marked
// in keep (keep[i - lo]).
Line fitLine (const Grid& grid, const std::vector<double>& y, int lo, int hi, const std::vector<char>& keep)
{
    double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = lo; i <= hi; ++i)
        if (keep[(size_t) (i - lo)] != 0)
        {
            const auto x = grid.x (i), v = y[(size_t) i];
            n += 1; sx += x; sy += v; sxx += x * x; sxy += x * v;
        }

    const auto den = n * sxx - sx * sx;
    if (n < 2 || den <= 0)
        return { 0.0, n > 0 ? sy / n : 0.0 };
    const auto slope = (n * sxy - sx * sy) / den;
    return { slope, (sy - slope * sx) / n };
}

// Least squares, then refits without the points further than a threshold
// from the current line (the very features the trend is used to measure, which
// would otherwise tilt it and shrink them). The threshold starts at
// trendOutlierDb and tightens by trendOutlierStepDb per refit down to
// trendOutlierFinalDb, so the core of a feature goes first and its skirts
// after, once the line no longer leans on them. It stops after trendRefits
// refits, or when the kept set no longer changes at the final threshold. A
// refit that would keep fewer than trendMinKeptFraction of the points is not
// made: the line so far is kept (the plain fit, if it is the first).
Line robustTrend (const Grid& grid, const std::vector<double>& y, int lo, int hi)
{
    const auto count = (size_t) (hi - lo + 1);
    std::vector<char> keep (count, 1), next (count);
    auto line = fitLine (grid, y, lo, hi, keep);

    for (int iteration = 0; iteration < tn::trendRefits; ++iteration)
    {
        const auto threshold = std::max (tn::trendOutlierFinalDb, tn::trendOutlierDb - iteration * tn::trendOutlierStepDb);
        size_t kept = 0;
        for (int i = lo; i <= hi; ++i)
        {
            const auto residual = y[(size_t) i] - (line.intercept + line.slope * grid.x (i));
            next[(size_t) (i - lo)] = std::abs (residual) <= threshold ? 1 : 0;
            kept += next[(size_t) (i - lo)] != 0 ? 1 : 0;
        }

        if ((double) kept < tn::trendMinKeptFraction * (double) count)
            break;
        if (next == keep)
        {
            if (threshold <= tn::trendOutlierFinalDb)
                break;
            continue;
        }

        keep = next;
        line = fitLine (grid, y, lo, hi, keep);
    }
    return line;
}

struct Found
{
    Candidate c;
    double loHz = 0, hiHz = 0; // region of an excess (to drop duplicates)
};

void measureLevels (const std::vector<std::vector<float>>& ch, size_t n, AnalysisSummary& s)
{
    double sumSquares = 0, peak = 0;
    for (const auto& c : ch)
        for (size_t i = 0; i < n; ++i)
        {
            const double v = std::isfinite (c[i]) ? c[i] : 0.0f;
            sumSquares += v * v;
            peak = std::max (peak, std::abs (v));
        }

    s.rmsDbfs = ampToDbfs (std::sqrt (sumSquares / ((double) n * (double) ch.size())));
    s.peakDbfs = ampToDbfs (peak);
    s.crestDb = s.peakDbfs - s.rmsDbfs;
}

void measureStereo (const std::vector<float>& left, const std::vector<float>& right, size_t n, AnalysisSummary& s)
{
    double sl = 0, sr = 0, sll = 0, srr = 0, slr = 0, mid = 0, side = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double l = std::isfinite (left[i]) ? left[i] : 0.0f;
        const double r = std::isfinite (right[i]) ? right[i] : 0.0f;
        sl += l; sr += r; sll += l * l; srr += r * r; slr += l * r;
        mid += 0.25 * (l + r) * (l + r);
        side += 0.25 * (l - r) * (l - r);
    }

    if (side <= 0)
        s.sideToMidDb = tn::floorDb;
    else
        s.sideToMidDb = mid > 0 ? std::clamp (10.0 * std::log10 (side / mid), tn::floorDb, -tn::floorDb) : -tn::floorDb;

    const auto count = (double) n;
    const auto varL = sll - sl * sl / count, varR = srr - sr * sr / count, cov = slr - sl * sr / count;
    if (varL > 0 && varR > 0)
        s.correlation = std::clamp (cov / std::sqrt (varL * varR), -1.0, 1.0);
    else
        s.correlation = (varL > 0) == (varR > 0) ? 1.0 : 0.0; // both constant: identical; one: unrelated

    s.isMono = s.sideToMidDb < tn::monoSideToMidDb;
}

bool appliesLowRumble (assist::Source s)
{
    return s == assist::Source::voice || s == assist::Source::acousticGuitar || s == assist::Source::electricGuitar;
}
} // namespace

//==============================================================================
AnalysisSummary analyse (const std::vector<std::vector<float>>& channels, double sampleRate, assist::Source source)
{
    AnalysisSummary s;
    s.sampleRate = sampleRate;
    s.channels = (int) channels.size();
    s.spectrumDb.assign ((size_t) AnalysisSummary::spectrumPoints, tn::floorDb);
    const auto range = tn::fitRangeFor (source);
    s.fitFromHz = range.fromHz;
    s.fitToHz = range.toHz;

    if (channels.empty() || ! std::isfinite (sampleRate) || sampleRate <= 0)
        return s;

    size_t n = channels.front().size();
    for (const auto& c : channels)
        n = std::min (n, c.size());
    if (n == 0)
        return s;

    s.seconds = (double) n / sampleRate;
    measureLevels (channels, n, s);
    if (channels.size() >= 2)
        measureStereo (channels[0], channels[1], n, s);

    s.enoughSignal = s.seconds >= tn::minSeconds && s.rmsDbfs > tn::minRmsDbfs;
    if (! s.enoughSignal)
        return s;

    //==========================================================================
    // Long-term spectrum of the mid signal.
    std::vector<float> mid (n);
    const auto scale = 1.0 / (double) channels.size();
    for (size_t i = 0; i < n; ++i)
    {
        double sum = 0;
        for (const auto& c : channels)
            sum += std::isfinite (c[i]) ? c[i] : 0.0f;
        mid[i] = (float) (sum * scale);
    }
    const LongTermSpectrum spectrum (mid, sampleRate);

    for (int i = 0; i < AnalysisSummary::spectrumPoints; ++i)
        s.spectrumDb[(size_t) i] = outputDb (spectrum.bandDensity (AnalysisSummary::spectrumStartHz * std::exp2 (i * AnalysisSummary::spectrumStepOct),
                                                                    tn::spectrumSmoothingOct));

    Grid grid;
    grid.x0 = std::log2 (tn::gridStartHz);
    const auto maxHz = std::min (tn::gridMaxHz, tn::gridMaxFraction * sampleRate);
    grid.size = (int) std::floor ((std::log2 (maxHz) - grid.x0) / grid.step + 1.0e-9) + 1;
    if (grid.size < 3)
        return s;

    std::vector<double> third ((size_t) grid.size), fine ((size_t) grid.size), reference ((size_t) grid.size);
    for (int i = 0; i < grid.size; ++i)
    {
        const auto f = grid.hz (i);
        third[(size_t) i] = densityDb (spectrum.bandDensity (f, tn::trendSmoothingOct));
        fine[(size_t) i] = densityDb (spectrum.bandDensity (f, tn::resonanceSmoothingOct));
        reference[(size_t) i] = densityDb (spectrum.bandDensity (f, tn::resonanceReferenceOct));
    }

    //==========================================================================
    // Trend of the 1/3-oct density (dB) against log2 (f) over the fit range.
    s.fitToHz = std::min (range.toHz, maxHz);
    const auto lo = grid.firstAtOrAbove (s.fitFromHz), hi = grid.lastAtOrBelow (s.fitToHz);
    if (hi - lo < 2)
        return s;

    const auto line = robustTrend (grid, third, lo, hi);
    const auto slope = line.slope, intercept = line.intercept;
    s.slopeDbPerOct = slope;

    std::vector<double> diff ((size_t) grid.size), negDiff ((size_t) grid.size), resonance ((size_t) grid.size);
    for (int i = 0; i < grid.size; ++i)
    {
        diff[(size_t) i] = third[(size_t) i] - (intercept + slope * grid.x (i));
        negDiff[(size_t) i] = -diff[(size_t) i];
        resonance[(size_t) i] = fine[(size_t) i] - reference[(size_t) i];
    }

    //==========================================================================
    // Candidates.
    std::vector<Found> found;
    const auto octaves = [&grid] (double fromIndex, double toIndex) { return (toIndex - fromIndex) * grid.step; };

    std::vector<Candidate> resonances;
    for (const auto& r : runsAbove (resonance, lo, hi, tn::resonanceDb))
    {
        const auto k = (int) std::lround (r.peak.index);
        const auto width = octaves (0.0, widthAbove (resonance, k, lo, hi, 0.5 * r.peak.value));
        if (width < tn::resonanceMaxOct)
            resonances.push_back ({ {}, CandidateKind::resonance, grid.hz (r.peak.index), r.peak.value, width });
    }

    for (const auto& r : runsAbove (diff, lo, hi, tn::excessDb))
        if (octaves (r.left, r.right) >= tn::excessMinOct)
            found.push_back ({ { {}, CandidateKind::excess, grid.hz (r.peak.index), r.peak.value, octaves (r.left, r.right) },
                               grid.hz (r.left), grid.hz (r.right) });

    for (const auto& r : runsAbove (negDiff, lo, hi, -tn::deficitDb))
        if (octaves (r.left, r.right) >= tn::deficitMinOct)
            found.push_back ({ { {}, CandidateKind::deficit, grid.hz (r.peak.index), -r.peak.value, octaves (r.left, r.right) } });

    // An excess that contains a resonance at least as strong as itself is
    // that resonance seen through the 1/3-oct smoothing: report it once.
    std::erase_if (found, [&resonances] (const Found& f)
    {
        return f.c.kind == CandidateKind::excess
            && std::any_of (resonances.begin(), resonances.end(), [&f] (const Candidate& r)
                   { return r.freqHz >= f.loHz && r.freqHz <= f.hiHz && f.c.magnitudeDb <= r.magnitudeDb; });
    });
    for (const auto& r : resonances)
        found.push_back ({ r });

    if (appliesLowRumble (source))
    {
        const auto from = std::max (tn::lowRumbleFromHz, 1.5 * spectrum.getBinHz());
        const auto below = spectrum.energy (from, s.fitFromHz), total = spectrum.energy (from, 0.5 * sampleRate);
        const auto shareDb = below > 0 && total > 0 ? 10.0 * std::log10 (below / total) : tn::floorDb;
        const auto top = grid.lastAtOrBelow (s.fitFromHz);

        if (shareDb > tn::lowRumbleDb && top >= 0)
        {
            // Strongest 1/3-oct band power (density * bandwidth) down there.
            std::vector<double> bandPower ((size_t) top + 1);
            for (int i = 0; i <= top; ++i)
                bandPower[(size_t) i] = third[(size_t) i] + 10.0 * std::log10 (grid.hz (i));
            const auto k = (int) (std::max_element (bandPower.begin(), bandPower.end()) - bandPower.begin());
            const auto peak = refinePeak (bandPower, k, 0, top);
            found.push_back ({ { {}, CandidateKind::lowRumble, grid.hz (peak.index), shareDb - tn::lowRumbleDb,
                                 std::log2 (s.fitFromHz / tn::gridStartHz) } });
        }
    }

    if (source == assist::Source::voice)
    {
        const auto first = grid.firstAtOrAbove (tn::sibilanceFromHz), last = grid.lastAtOrBelow (tn::sibilanceToHz);
        if (last >= first)
        {
            double mean = 0;
            auto best = first;
            for (int i = first; i <= last; ++i)
            {
                mean += diff[(size_t) i];
                if (diff[(size_t) i] > diff[(size_t) best])
                    best = i;
            }
            mean /= (double) (last - first + 1);

            if (mean > tn::sibilanceDb)
            {
                // The band is reported as sibilance, not also as an excess.
                std::erase_if (found, [] (const Found& f)
                {
                    return f.c.kind == CandidateKind::excess && f.c.freqHz >= tn::sibilanceFromHz && f.c.freqHz <= tn::sibilanceToHz;
                });
                found.push_back ({ { {}, CandidateKind::sibilance, grid.hz (refinePeak (diff, best, first, last).index), mean,
                                     std::log2 (tn::sibilanceToHz / tn::sibilanceFromHz) } });
            }
        }
    }

    std::stable_sort (found.begin(), found.end(), [] (const Found& a, const Found& b)
    {
        const auto ma = std::abs (a.c.magnitudeDb), mb = std::abs (b.c.magnitudeDb);
        if (ma > mb || mb > ma)
            return ma > mb;
        return a.c.freqHz < b.c.freqHz;
    });
    if (found.size() > (size_t) tn::maxCandidates)
        found.resize ((size_t) tn::maxCandidates);

    for (size_t i = 0; i < found.size(); ++i)
    {
        s.candidates.push_back (found[i].c);
        s.candidates.back().id = "c" + std::to_string (i + 1);
    }
    return s;
}
} // namespace iaq8::analysis
