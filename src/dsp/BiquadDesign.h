#pragma once

#include "dsp/FilterTypes.h"

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

// The remaining designs are also from the RBJ cookbook, i.e. bilinear
// transforms prewarped at f0 of these analog prototypes (A = 10^(gainDb/40)):
//   low shelf   A * (s^2 + s*sqrt(A)/Q + A) / (A*s^2 + s*sqrt(A)/Q + 1)
//   high shelf  A * (A*s^2 + s*sqrt(A)/Q + 1) / (s^2 + s*sqrt(A)/Q + A)
//   low cut     s^2 / (s^2 + s/Q + 1)          (2nd-order high-pass, 12 dB/oct)
//   high cut    1 / (s^2 + s/Q + 1)            (2nd-order low-pass, 12 dB/oct)
//   notch       (s^2 + 1) / (s^2 + s/Q + 1)
// Inputs are clamped exactly like designBell. Shelves at 0 dB are an exact
// identity (b == a); cuts and notch ignore gain.
BiquadCoefficients designLowShelf (double sampleRate, double frequencyHz, double gainDb, double q) noexcept;
BiquadCoefficients designHighShelf (double sampleRate, double frequencyHz, double gainDb, double q) noexcept;
BiquadCoefficients designLowCut (double sampleRate, double frequencyHz, double q) noexcept;
BiquadCoefficients designHighCut (double sampleRate, double frequencyHz, double q) noexcept;
BiquadCoefficients designNotch (double sampleRate, double frequencyHz, double q) noexcept;

// Detection band-pass for the dynamic engine (E2a): RBJ band-pass with
// constant 0 dB peak gain, bilinear of H(s) = (s/Q) / (s^2 + s/Q + 1)
// prewarped at f0, so |H(f0)| == 1 for every f0 and Q. Not an audio
// topology (it is not in FilterType). Inputs are clamped like designBell.
BiquadCoefficients designBandPass (double sampleRate, double frequencyHz, double q) noexcept;

// Dispatches to the design for `type` (gainDb ignored where it does not apply).
BiquadCoefficients designFilter (FilterType type, double sampleRate, double frequencyHz, double gainDb, double q) noexcept;

// |H(e^jw)| evaluated at frequencyHz. Used by tests now and by the future
// curve display / analysis engine.
double magnitudeAt (const BiquadCoefficients& c, double frequencyHz, double sampleRate) noexcept;

// True when both poles are strictly inside the unit circle
// (stability triangle: |a2| < 1 and |a1| < 1 + a2).
bool isStable (const BiquadCoefficients& c) noexcept;
} // namespace labq8::dsp
