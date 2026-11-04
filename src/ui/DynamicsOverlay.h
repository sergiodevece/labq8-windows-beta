#pragma once

#include "plugin/DynamicsTelemetry.h"
#include "ui/UiModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <vector>

namespace labq8::ui
{
// UI-2c: the dynamic response on the screen (views DYN and ALL), under the
// static EQ curve and its nodes. A child of the editor's content covering the
// plot (reference coordinates); it never takes the mouse. The static curve
// keeps its own cached image (ResponseScreen): nothing here repaints it.
//
// What it draws (approved D1): for each Bell band whose dynamic section is
// really reducing, the response the EQ applies at that instant, as a dotted
// line in the band's colour, and the area between the static curve and it
// filled in that colour. Nodes stay on the static curve (what is edited).
// In DYN, the effective reduction of each acting band is written under its
// dynamic point ("−3.2"). With no band reducing nothing is drawn.
//
// The instant (DynamicsTelemetry, A2 + A4): all eight bands of one
// sub-block end, the most reducing one since the previous frame. Per band
// the curve is the static curve (from the parameters, like the nodes) plus
// the change the reduction makes to that band's design at the DSP's own
// smoothed gain, frequency and Q:
//     delta_b(f) = |H(f; g - r_eff)| - |H(f; g)|  in dB,  r_eff with the +-24 dB clamp.
// The cascade is a product, so the deltas add in dB exactly.
//
// About 30 frames per second (display frames). It repaints only when a
// reduction moved by 0.01 dB or more (about 0.05 px). When no audio arrives
// for 100 ms (approved D3) the overlay is removed at once; nothing is
// invented. Host bypass and a new stream (prepareToPlay) remove it too.
class DynamicsOverlay final : public juce::Component
{
public:
    using BandReader = std::function<std::array<BandView, 8>()>;
    using RateReader = std::function<double()>;

    static constexpr double staleSeconds = 0.1;
    static constexpr double repaintStepDb = 0.01;

    DynamicsOverlay (DynamicsTelemetry& telemetry, BandReader readBands, RateReader sampleRate);
    ~DynamicsOverlay() override;

    // Whether the current view shows the dynamics (DYN or ALL), and whether
    // it writes the reductions (DYN).
    void setShown (bool shouldBeShown, bool withReadouts);
    bool isShown() const noexcept { return shown; }
    bool areReadoutsShown() const noexcept { return readouts; }

    // The parameters changed (the static curve moved): the editor's poll.
    void parametersChanged();

    // One frame: `seconds` since the previous one. Called about 30 times per
    // second from the display's refresh; tests call it directly.
    void advance (double seconds);

    // For tests and measurements.
    bool isShowingDynamics() const noexcept { return showing; }
    const std::array<double, 8>& getEffectiveReductions() const noexcept { return drawn; }
    const std::vector<double>& getFrequencies() const noexcept { return frequencies; }
    const std::vector<double>& getStaticResponseDb() const noexcept { return staticDb; }
    const std::vector<double>& getDynamicResponseDb() const noexcept { return dynamicDb; }
    const std::vector<double>& getBandDeltaDb (int band) const noexcept { return deltas[(size_t) band]; }
    std::vector<juce::Rectangle<float>> getReadoutBounds() const; // DYN, reference space, one per written band
    int getRepaintRequests() const noexcept { return repaintRequests; }
    int getPaintCount() const noexcept { return paintCount; }
    double getLastComputeMs() const noexcept { return lastComputeMs; }

    void paint (juce::Graphics&) override;

private:
    void clear();
    void compute();
    void requestRepaint();
    juce::String readoutText (int band) const;
    juce::Rectangle<float> readoutBounds (int band) const;

    DynamicsTelemetry& telemetry;
    BandReader readBands;
    RateReader sampleRate;

    bool shown = false, readouts = false, showing = false, staticDirty = true;
    std::uint64_t lastOffered = 0, activationOffered = 0;
    std::uint32_t generation = 0;
    bool started = false;
    double sinceAudio = 0.0, rate = 0.0;

    DynamicsTelemetry::Snapshot snapshot;
    std::array<double, 8> drawn {};
    std::vector<double> frequencies, staticDb, dynamicDb;
    std::array<std::vector<double>, 8> deltas;
    // Each band's response without its reduction, kept while its gain,
    // frequency and Q (and the rate) do not change.
    std::array<std::vector<double>, 8> withoutDb;
    std::array<std::array<float, 3>, 8> withoutKey {};

    int repaintRequests = 0, paintCount = 0;
    double lastComputeMs = 0.0, lastFrame = -1.0;
    juce::VBlankAttachment vblank;

    JUCE_DECLARE_NON_COPYABLE (DynamicsOverlay)
};
} // namespace labq8::ui
