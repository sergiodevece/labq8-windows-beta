#pragma once

#include "dsp/AssistEngine.h" // BandSet

#include <vector>

namespace iaq8::analysis
{
struct RenderResult
{
    std::vector<std::vector<float>> before; // the input times the common headroom gain (== input when headroomDb is 0)
    std::vector<std::vector<float>> after;  // same shape as the input
    double compensationDb = 0.0;            // whole-file K-weighted match applied to `after`
    double peakBefore = 0.0;                // linear sample peaks of the input and of the matched EQ output,
    double peakAfter = 0.0;                 // both before the headroom gain
    double headroomDb = 0.0;                // common gain (<= 0) applied to BOTH before and after
};

// Offline "after" render: runs `channels` through labq8::dsp::EqCore with
// `bands`, then applies one constant gain that matches the whole-file
// K-weighted loudness of the result to the input's (10*log10(msBefore/msAfter),
// clamped to +-LoudnessMatcher::maxCompensationDb, as in the plugin). The
// EQ starts settled: the targets are set and a short silent pre-roll lets the
// parameter ramps and path crossfades finish, so the first samples are not a
// ramp from the default bands.
//
// Headroom: if max(peakBefore, peakAfter) exceeds 0.98 (about -0.18 dBFS), one
// common gain 0.98/peak is applied to both `before` and `after`, so a 24-bit
// write never clips one side only and the loudness match is kept. `before` is
// then not level-identical to the source (needed for a fair A/B). Silent input or output: no compensation (0 dB).
// Pure; not real-time.
RenderResult render (const std::vector<std::vector<float>>& channels, double sampleRate, const dsp::BandSet& bands);
} // namespace iaq8::analysis
