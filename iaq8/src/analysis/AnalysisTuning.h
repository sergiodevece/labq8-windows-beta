#pragma once

#include "assist/Source.h"

// Every threshold and constant of the track analysis, in one place. These are
// the plan's initial values, with one retune (resonanceReferenceOct) and the
// trend refit added by the Task 7 review ruling; they are to be refined with
// real material.
//
// Known limits of these values:
//  - Resolution: the FFT bin is fs/8192 (5.9 Hz at 48 kHz) and Hann spreads a
//    tone over about 4 bins, so below roughly 200 Hz a narrow resonance cannot
//    be told apart from its surroundings (it cannot be narrower than 1/6 oct).
//  - The trend is a robust least-squares line (refitted without the points
//    more than 3 dB, then 2, then 1 dB away). The skirts of a broad feature
//    under 1 dB stay in the fit, so a Q1 bell still reads about 85 % of its
//    smoothed height (+6 dB reads about 5.1 dB, +4.5 dB about 3.8 dB). A
//    feature covering about half the fit range or more is taken as the trend.
//  - Fit range per source: voice 100-12000 Hz, bass 40-5000, mix 30-16000 and
//    the rest (guitars, drums, other) 60-14000; the top is also capped at
//    min(gridMaxHz, gridMaxFraction * fs). Candidates are searched only inside
//    it; outside it the trend is an extrapolation.
//  - Tonal material (sustained notes, harmonics) can show as resonances; the
//    list is capped at maxCandidates by |magnitude|.

namespace iaq8::analysis::tuning
{
//==============================================================================
// Enough signal to analyse (global constraint: at least 6 s of useful signal).
inline constexpr double minSeconds = 6.0;
inline constexpr double minRmsDbfs = -60.0; // the capture's useful-signal floor
inline constexpr double floorDb = -120.0;   // value of every level that is "nothing"

//==============================================================================
// Long-term spectrum: Welch average of the mid signal (L+R)/2, periodic Hann
// window, 50 % overlap, FFT order as LABQ8's SpectrumAnalyzer::fftOrderFor.
inline constexpr double welchOverlap = 0.5;

inline int fftOrderFor (double sampleRate) noexcept
{
    return sampleRate <= 50000.0 ? 13 : sampleRate <= 100000.0 ? 14 : 15;
}

// Fractional-octave smoothing widths (box average of the power density over
// [f * 2^-w/2, f * 2^+w/2]).
inline constexpr double spectrumSmoothingOct = 1.0 / 6.0;  // spectrumDb
inline constexpr double trendSmoothingOct = 1.0 / 3.0;     // trend, excesses, deficits, sibilance
inline constexpr double resonanceSmoothingOct = 1.0 / 24.0;
// Retuned from the plan's 1/3 oct: a 1/3-oct reference is lifted by the
// resonance itself, so a +12 dB Q12 peak measured only 4.6 dB over it (below
// resonanceDb). Over 1 oct the same peak measures 7.7 dB, while broad shapes
// (+6 dB Q1 bell, the 5-9 kHz band, pink noise) stay under 2.4 dB.
inline constexpr double resonanceReferenceOct = 1.0;

// Detection grid: log-spaced, gridStepOct apart, from gridStartHz up to
// min(gridMaxHz, gridMaxFraction * sampleRate).
inline constexpr double gridStartHz = 20.0;
inline constexpr double gridStepOct = 1.0 / 48.0;
inline constexpr double gridMaxHz = 20000.0;
inline constexpr double gridMaxFraction = 0.45;

//==============================================================================
// Trend: least-squares line of the 1/3-octave density in dB against log2(f),
// between the source's limits, refitted without outliers (below).
struct FitRange { double fromHz, toHz; };

// Robust refit (TrackAnalyzer robustTrend): points further than a threshold
// from the line (either side) are dropped and the line refitted. The threshold
// starts at trendOutlierDb (= excessDb) and tightens by trendOutlierStepDb per
// refit down to trendOutlierFinalDb, for at most trendRefits refits or until
// the kept set stops changing at the final threshold. A refit that would keep
// fewer than trendMinKeptFraction of the points is not made (the line so far,
// the plain fit if it is the first, stays). A fixed 3 dB threshold leaves a
// Q1 bell's 0-3 dB skirts in the fit: a +6 dB bell read 4.5 dB and a +4.5 dB
// bell went undetected.
inline constexpr double trendOutlierDb = 3.0;
inline constexpr double trendOutlierStepDb = 1.0;
inline constexpr double trendOutlierFinalDb = 1.0;
inline constexpr int trendRefits = 6;
inline constexpr double trendMinKeptFraction = 0.5;

inline FitRange fitRangeFor (assist::Source source) noexcept
{
    switch (source)
    {
        case assist::Source::voice: return { 100.0, 12000.0 };
        case assist::Source::bass:  return { 40.0, 5000.0 };
        case assist::Source::mix:   return { 30.0, 16000.0 };
        case assist::Source::acousticGuitar:
        case assist::Source::electricGuitar:
        case assist::Source::drums:
        case assist::Source::other: break;
    }
    return { 60.0, 14000.0 };
}

//==============================================================================
// Candidates (searched inside the fit range).
inline constexpr double excessDb = 3.0;          // 1/3 oct - trend above this ...
inline constexpr double excessMinOct = 1.0 / 3.0; // ... over at least this width
inline constexpr double deficitDb = -4.0;          // 1/3 oct - trend below this ...
inline constexpr double deficitMinOct = 2.0 / 3.0; // ... over at least this width
inline constexpr double resonanceDb = 6.0;           // 1/24 oct - reference above this ...
inline constexpr double resonanceMaxOct = 1.0 / 6.0; // ... and narrower than this

// lowRumble: energy below the fit range's lower limit, relative to the total
// (both integrated from lowRumbleFromHz), above this. Voice and guitars only.
inline constexpr double lowRumbleDb = -20.0;
inline constexpr double lowRumbleFromHz = 10.0;

// sibilance: mean of (1/3 oct - trend) over the band above this. Voice only.
inline constexpr double sibilanceFromHz = 5000.0, sibilanceToHz = 9000.0;
inline constexpr double sibilanceDb = 4.0;

inline constexpr int maxCandidates = 8;

//==============================================================================
// Stereo: mono when side/mid energy is below this.
inline constexpr double monoSideToMidDb = -40.0;
} // namespace iaq8::analysis::tuning
