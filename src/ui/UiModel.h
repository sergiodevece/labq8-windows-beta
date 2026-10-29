#pragma once

#include "dsp/FilterTypes.h"

#include <array>
#include <string>
#include <vector>

// UI-1: pure functions behind the editor (no JUCE): value formatting for the
// numeric displays, the screen's frequency / dB axes and the static EQ
// response drawn from the parameter values. Tested in tests/EditorTests.cpp.

namespace labq8::ui
{
// The seven knobs of the shared panel, in geometry order.
enum class PanelControl { frequency, gain, q, threshold, range, attack, release };

// Text of a numeric display, from the parameter's real value.
std::string formatValue (PanelControl control, double value);

// Screen axes (reference pixels of the approved design).
float frequencyToX (double hz);
double xToFrequency (float x);
float decibelsToY (double db); // +-24 dB on the seven grid lines

// One band as the screen sees it (straight from the parameters).
struct BandView
{
    double frequencyHz = 1000.0, gainDb = 0.0, q = 1.0;
    dsp::FilterType type = dsp::FilterType::bell;
    bool bypassed = false;
};

// Static response (dB) of one band / of the cascade at the given
// frequencies: the same designs, clamps and effective Q as the DSP. The
// dynamic gain reduction is not included (no telemetry in UI-1).
std::vector<double> bandResponseDb (const BandView& band, double sampleRate, const std::vector<double>& frequencies);
std::vector<double> cascadeResponseDb (const std::array<BandView, 8>& bands, double sampleRate, const std::vector<double>& frequencies);
} // namespace labq8::ui
