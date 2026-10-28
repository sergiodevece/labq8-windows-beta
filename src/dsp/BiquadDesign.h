#pragma once

// Pure C++ biquad design helpers. No JUCE dependency, no allocation,
// safe to call from the audio thread.

namespace labq8::dsp
{
// Normalised biquad (a0 == 1), transposed direct form II convention:
//   H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2)
struct BiquadCoefficients
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
};

// Returns a usable sample rate: invalid or out-of-range values fall back to
// limits::fallbackSampleRate.
double sanitiseSampleRate (double sampleRate) noexcept;

// Clamp helpers. Non-finite input maps to the parameter default.
double clampFrequency (double frequencyHz, double sampleRate) noexcept;
double clampGainDb (double gainDb) noexcept;
double clampQ (double q) noexcept;

// RBJ "Audio EQ Cookbook" peaking (Bell) filter. Equivalent to the bilinear
// transform of H(s) = (s^2 + s*A/Q + 1) / (s^2 + s/(A*Q) + 1), prewarped at
// the centre frequency, with A = 10^(gainDb/40). Inputs are clamped first.
// At gainDb == 0 the result is exactly an identity filter (b == a).
BiquadCoefficients designBell (double sampleRate, double frequencyHz, double gainDb, double q) noexcept;

// |H(e^jw)| evaluated at frequencyHz. Used by tests now and by the future
// curve display / analysis engine.
double magnitudeAt (const BiquadCoefficients& c, double frequencyHz, double sampleRate) noexcept;

// True when both poles are strictly inside the unit circle
// (stability triangle: |a2| < 1 and |a1| < 1 + a2).
bool isStable (const BiquadCoefficients& c) noexcept;
} // namespace labq8::dsp
