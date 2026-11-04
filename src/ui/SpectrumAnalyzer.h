#pragma once

#include <juce_dsp/juce_dsp.h>

#include <memory>
#include <vector>

namespace labq8::ui
{
// UI-2b: the spectrum of the latest samples of an analysis point (PRE or
// POST), for the screen. Runs on the message thread, never on the audio.
//
// - FFT of 8192 samples at 44.1 / 48 kHz, 16384 at 88.2 / 96 kHz, 32768 at
//   176.4 / 192 kHz: about 170 ms and 5.9 Hz per bin at any rate;
// - 4-term Blackman-Harris window (side lobes at -92 dB);
// - calibrated by the window's coherent gain: a sine of amplitude A reads
//   20 log10 A dBFS at its peak (up to 0.83 dB less between two bins);
// - stereo: the mean power of the channels (L = R reads as one channel;
//   L = -R does not cancel; a signal in one channel only reads 3 dB lower);
// - display points: the largest bin inside a point's band when there is
//   one, otherwise interpolated (in dB) between the two nearest bins.
class SpectrumAnalyzer
{
public:
    static constexpr double floorDb = -96.0; // bottom of the screen
    static constexpr double restDb = -120.0;  // nothing, well below the floor

    static int fftOrderFor (double sampleRate);

    // Prepares for a sample rate and the display points' frequencies
    // (ascending). Allocates: never called on the audio thread. `fftOrder`
    // 0: by the rate (fftOrderFor); otherwise forced (measurements only).
    void prepare (double sampleRate, const std::vector<double>& pointFrequencies, int fftOrder = 0);
    int getSize() const noexcept { return size; }
    double getSampleRate() const noexcept { return rate; }

    // Analyses `getSize()` samples of each channel (mean power across them).
    void analyse (const float* const* channels, int numChannels);

    // Calibrated level of each bin 0 .. size / 2 (dB, restDb for nothing).
    const std::vector<float>& getBinsDb() const noexcept { return binsDb; }
    // Level of each display point from the last analysis (restDb at or above
    // Nyquist).
    void toDisplay (std::vector<float>& pointsDb) const;

private:
    struct Point
    {
        int first = 0, last = -1;     // bins inside the point's band (last < first: none)
        float position = 0.0f;        // else: fractional bin to interpolate at
        bool valid = false;           // below Nyquist
    };

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, work, power, binsDb;
    std::vector<Point> points;
    double rate = 0.0, windowSum = 1.0;
    int size = 0;
};

// Time behaviour of the drawn spectrum: rises at once, falls linearly in dB
// at 17.4 dB/s (an exponential power decay with a 250 ms time constant),
// driven by the real time between frames.
struct SpectrumBallistics
{
    static constexpr double fallDbPerSecond = 17.371779; // 10 log10(e) / 0.25 s

    // `analysed`: the new display levels, or nullptr when no new audio
    // arrived (the spectrum only falls). Returns whether any point moved.
    bool update (const std::vector<float>* analysed, size_t numPoints, double seconds);
    // Every point at or below the floor: nothing is drawn.
    bool atRest() const noexcept;
    void reset (size_t numPoints);

    std::vector<float> levelDb;
};
} // namespace labq8::ui
