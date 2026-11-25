#pragma once

#include "assist/Source.h"

#include <string>
#include <vector>

namespace iaq8::analysis
{
// The AI never sees audio: it gets this compact, measured summary of the
// captured track and a short list of candidate problem frequencies.
enum class CandidateKind { excess, deficit, resonance, lowRumble, sibilance };

// freqHz, magnitudeDb and widthOct per kind:
//  excess / deficit  peak (min) of the 1/3-oct spectrum minus the trend, its
//                    frequency, and the width of the region past the threshold.
//  resonance         peak of the 1/24-oct spectrum minus the local reference,
//                    its frequency, and the width where it is above half that
//                    peak (in dB).
//  lowRumble         dB by which the energy below the source's lower limit
//                    exceeds the allowed share of the total, the frequency of
//                    the strongest 1/3-oct band down there, and the width from
//                    gridStartHz to the limit.
//  sibilance         mean of (1/3 oct - trend) over 5-9 kHz, the frequency of
//                    its maximum there, and the band's width.
struct Candidate
{
    std::string id;
    CandidateKind kind;
    double freqHz, magnitudeDb, widthOct;
};

struct AnalysisSummary
{
    int version = 1;
    double sampleRate = 0, seconds = 0;
    int channels = 0;
    double rmsDbfs = -120, peakDbfs = -120, crestDb = 0;
    bool isMono = true;
    double sideToMidDb = -120, correlation = 1;
    static constexpr double spectrumStartHz = 20.0, spectrumStepOct = 1.0 / 6.0;
    static constexpr int spectrumPoints = 60;
    std::vector<double> spectrumDb;        // 60 points: 20 Hz * 2^(i/6), i = 0..59
    double slopeDbPerOct = 0, fitFromHz = 0, fitToHz = 0;
    std::vector<Candidate> candidates;     // at most 8, by |magnitudeDb| desc; ids "c1".."c8"
    bool enoughSignal = false;
};

// Pure and deterministic. Runs on a background thread (allocates); never on
// the audio thread. `channels` holds 1 or 2 channels of useful signal (as
// CaptureBuffer::copyOut gives them) at `sampleRate`.
//
// spectrumDb is a power spectral density: the 1/6-octave average of the mid
// signal (L+R)/2 in dB re 1 FS^2/Hz, floored at -120 (the internal curves
// use the same floor). Pink noise falls at
// -3 dB/oct, like slopeDbPerOct. It is meaningful only when enoughSignal is
// true. With too little signal (under 6 s, or quieter than -60 dBFS)
// enoughSignal is false, there are no candidates and spectrumDb holds 60 x -120.
AnalysisSummary analyse (const std::vector<std::vector<float>>& channels, double sampleRate, assist::Source source);
} // namespace iaq8::analysis
