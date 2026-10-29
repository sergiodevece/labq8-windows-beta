#include "ui/UiModel.h"

#include "dsp/BiquadDesign.h"
#include "ui/UiGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace labq8::ui
{
namespace g = geometry;

namespace
{
    std::string format (const char* pattern, double value)
    {
        char text[32];
        std::snprintf (text, sizeof (text), pattern, value);
        return text;
    }

    // Shows "-20" for -20.0 and "-20.5" otherwise (displays of the design
    // print whole numbers without decimals).
    std::string trimmed (double value, const char* unit)
    {
        const auto rounded = std::round (value * 10.0) / 10.0;
        if (std::abs (rounded - std::round (rounded)) < 1e-9)
            return format ("%.0f", rounded) + unit;
        return format ("%.1f", rounded) + unit;
    }
} // namespace

std::string formatValue (PanelControl control, double value)
{
    switch (control)
    {
        case PanelControl::frequency:
            return value < 1000.0 ? format ("%.0f Hz", value) : format (value < 10000.0 ? "%.2f kHz" : "%.1f kHz", value / 1000.0);
        case PanelControl::gain:      return format ("%.1f dB", std::abs (value) < 0.05 ? 0.0 : value);
        case PanelControl::q:         return format (value < 1.0 ? "%.2f" : "%.1f", value);
        case PanelControl::threshold: return trimmed (value, " dB");
        case PanelControl::range:     return format ("%.1f dB", value);
        case PanelControl::attack:    return value < 10.0 ? format ("%.1f ms", value) : format ("%.0f ms", value);
        case PanelControl::release:   return format ("%.0f ms", value);
    }
    return {};
}

float frequencyToX (double hz)
{
    const auto& a = g::frequencyAnchors;
    hz = std::max (hz, 1.0);
    size_t i = 0;
    while (i + 2 < a.size() && hz > a[i + 1].hz)
        ++i;
    // Piecewise log between the printed labels (extrapolated past 20 kHz).
    const auto t = std::log (hz / a[i].hz) / std::log ((double) a[i + 1].hz / a[i].hz);
    return (float) (a[i].x + t * (a[i + 1].x - a[i].x));
}

double xToFrequency (float x)
{
    const auto& a = g::frequencyAnchors;
    size_t i = 0;
    while (i + 2 < a.size() && x > a[i + 1].x)
        ++i;
    const auto t = (x - a[i].x) / (a[i + 1].x - a[i].x);
    return a[i].hz * std::pow ((double) a[i + 1].hz / a[i].hz, (double) t);
}

float decibelsToY (double db)
{
    return (float) (g::plotZeroDbY - db * g::plotPixelsPerDb);
}

std::vector<double> bandResponseDb (const BandView& band, double sampleRate, const std::vector<double>& frequencies)
{
    std::vector<double> db (frequencies.size(), 0.0);
    // Bypassed bands are flat (gain topologies go to 0 dB, cuts / notch to
    // the dry path); Bell and shelves at 0 dB are flat as well.
    if (band.bypassed || (dsp::usesGain (band.type) && std::abs (band.gainDb) < 0.005))
        return db;

    const auto q = dsp::effectiveQ (band.type, dsp::clampQ (band.q));
    const auto c = dsp::designFilter (band.type, sampleRate, band.frequencyHz, band.gainDb, q);
    for (size_t i = 0; i < frequencies.size(); ++i)
        if (frequencies[i] < 0.5 * sampleRate)
            db[i] = 20.0 * std::log10 (std::max (1.0e-9, dsp::magnitudeAt (c, frequencies[i], sampleRate)));
    return db;
}

std::vector<double> cascadeResponseDb (const std::array<BandView, 8>& bands, double sampleRate, const std::vector<double>& frequencies)
{
    std::vector<double> total (frequencies.size(), 0.0);
    for (const auto& band : bands)
    {
        const auto db = bandResponseDb (band, sampleRate, frequencies);
        for (size_t i = 0; i < total.size(); ++i)
            total[i] += db[i];
    }
    return total;
}
} // namespace labq8::ui
