#pragma once

#include "plugin/SpectrumTap.h"
#include "ui/SpectrumAnalyzer.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace labq8::ui
{
// UI-2b: which signal the analyzer shows (the header's "Analyzer" menu).
enum class AnalyzerSource { pre, post, both };

// UI-2b: the spectrum on the screen, under the EQ curve and its nodes. A child
// of the editor's content covering the plot (reference coordinates); it
// never takes the mouse.
//
// About 30 times per second (on the display's frames, VBlankAttachment) it
// copies the latest samples
// of the shown taps, analyses them when new audio arrived and lets the
// ballistics fall when none did (a stopped transport fades out instead of
// freezing). It repaints only when something visible moved, so a spectrum
// at rest costs no painting. Only the taps it shows copy audio.
//
// POST alone or PRE alone: a green area with a brighter outline (the
// master's style). PRE + POST: POST as the area, PRE as a light warm outline
// on top, without fill, so the two cannot be confused.
class SpectrumView final : public juce::Component
{
public:
    SpectrumView (SpectrumTap& pre, SpectrumTap& post);
    ~SpectrumView() override;

    // Whether the current view shows the spectrum (SPECTRUM or ALL).
    void setShown (bool shouldBeShown);
    void setSource (AnalyzerSource newSource);
    AnalyzerSource getSource() const noexcept { return source; }

    // One frame: `seconds` since the previous one. Called about 30 times per
    // second from the display's refresh; tests call it directly.
    void advance (double seconds);

    // For tests and measurements.
    const std::vector<double>& getFrequencies() const noexcept { return frequencies; }
    const std::vector<float>& getLevels (bool preTrace) const noexcept { return (preTrace ? pre : post).ballistics.levelDb; }
    bool isTraceShown (bool preTrace) const noexcept { return (preTrace ? pre : post).active; }
    bool isAtRest() const noexcept;
    int getRepaintRequests() const noexcept { return repaintRequests; }
    double getLastAnalysisMs() const noexcept { return lastAnalysisMs; }
    int getFftSize() const noexcept { return post.analyzer.getSize(); }

    void paint (juce::Graphics&) override;

private:
    struct Trace
    {
        explicit Trace (SpectrumTap& t) : tap (t) {}

        SpectrumTap& tap;
        SpectrumAnalyzer analyzer;
        SpectrumBallistics ballistics;
        std::vector<std::vector<float>> samples;
        std::vector<float> display;
        std::uint64_t from = 0, lastEnd = 0;
        std::uint32_t generation = 0;
        bool active = false, prepared = false;
    };

    void updateActivity();
    bool advance (Trace& trace, double seconds);
    void paintTrace (juce::Graphics&, const Trace& trace, bool filled, juce::Colour line) const;

    Trace pre, post;
    std::vector<double> frequencies;
    AnalyzerSource source = AnalyzerSource::post;
    bool shown = true;
    int repaintRequests = 0;
    double lastAnalysisMs = 0.0, lastFrame = -1.0;
    juce::VBlankAttachment vblank;

    JUCE_DECLARE_NON_COPYABLE (SpectrumView)
};
} // namespace labq8::ui
