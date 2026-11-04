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
float spectrumDbToY (double dbfs); // UI-2b: 0 dBFS on the top grid line, -96 dBFS on the bottom one (clamped to the plot)

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

//==============================================================================
// UI-2a: IN / OUT sample-peak meters (26 cells per column, counted from the
// bottom).

// Level (dBFS) at which a cell lights: the printed scale at the cell's
// centre row, except the "0" cell (exactly 0 dBFS) and the top one (+12).
double meterCellThresholdDb (int cellFromBottom);
// Cells lit at `levelDb`: all those whose threshold it reaches (0..26).
int meterLitCells (double levelDb);

// Colour of a lit cell: green up to 0 dBFS, amber above it up to +6 dBFS,
// red above +6 dBFS (over level, not confirmed clipping).
enum class MeterColour { green, amber, red };
MeterColour meterCellColour (int cellFromBottom);

// Peak ballistics of one column: rises at once to a new peak, falls
// linearly in dB. Driven by the editor's frames.
struct MeterBallistics
{
    static constexpr double fallDbPerSecond = 11.8;
    static constexpr double restDb = -100.0;   // fallen below every cell
    static constexpr double ceilingDb = 13.0;  // above the top cell (+inf)

    // `peak`: linear sample peak since the previous frame (0: none);
    // `seconds`: time since the previous frame. Returns the level in dB.
    double update (float peak, double seconds);
    bool atRest() const noexcept { return levelDb <= restDb; }

    double levelDb = restDb;
};
} // namespace labq8::ui
