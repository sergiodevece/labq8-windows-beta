#pragma once

// Safe operating ranges for one parametric band.
// The parameter layer exposes exactly these ranges to the host; the DSP layer
// clamps to them again so it stays safe even when driven directly (tests,
// future engines) with out-of-range or non-finite values.

namespace labq8::dsp::limits
{
inline constexpr double minFrequencyHz     = 20.0;
inline constexpr double maxFrequencyHz     = 20000.0;
inline constexpr double defaultFrequencyHz = 1000.0;

// Centre frequency never goes above this fraction of the sample rate, so the
// band stays well defined when the session runs below 40.8 kHz.
inline constexpr double maxNyquistFraction = 0.49;

inline constexpr double minGainDb     = -24.0;
inline constexpr double maxGainDb     = 24.0;
inline constexpr double defaultGainDb = 0.0;

inline constexpr double minQ     = 0.1;
inline constexpr double maxQ     = 18.0;
inline constexpr double defaultQ = 1.0;

// Sample rates outside this window are treated as invalid by the DSP.
inline constexpr double minSampleRate      = 8000.0;
inline constexpr double maxSampleRate      = 768000.0;
inline constexpr double fallbackSampleRate = 44100.0;
} // namespace labq8::dsp::limits
