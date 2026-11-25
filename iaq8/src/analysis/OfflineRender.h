#pragma once

#include "dsp/AssistEngine.h" // BandSet

#include <vector>

namespace iaq8::analysis
{
struct RenderResult
{
    std::vector<std::vector<float>> after; // same shape as the input
    double compensationDb = 0.0;           // whole-file K-weighted match applied to `after`
};

// Offline "after" render: runs `channels` through labq8::dsp::EqCore with
// `bands`, then applies one constant gain that matches the whole-file
// K-weighted loudness of the result to the input's (10*log10(msBefore/msAfter),
// clamped to +-LoudnessMatcher::maxCompensationDb, as in the plugin). The
// EQ starts settled: the targets are set and a short silent pre-roll lets the
// parameter ramps and path crossfades finish, so the first samples are not a
// ramp from the default bands. Silent input or output: no compensation (0 dB).
// Pure; not real-time.
RenderResult render (const std::vector<std::vector<float>>& channels, double sampleRate, const dsp::BandSet& bands);
} // namespace iaq8::analysis
