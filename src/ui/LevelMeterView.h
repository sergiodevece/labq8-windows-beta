#pragma once

#include "plugin/LevelMeter.h"
#include "ui/UiGeometry.h"
#include "ui/UiModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace labq8::ui
{
// UI-2a: the four IN L / IN R / OUT L / OUT R columns of the approved
// design, showing the processor's sample peaks. A child of the editor's
// content (reference coordinates), covering the meters' area; it only draws
// the lit cells over the background's unlit ones.
//
// Every frame of the display (VBlankAttachment) it takes the peaks since the
// previous frame, advances the ballistics by the real elapsed time and
// repaints a column only when its lit cells change, so a meter at rest
// costs no painting. A mono track shows its one channel in both columns.
class LevelMeterView final : public juce::Component
{
public:
    static constexpr int numColumns = 4; // IN L, IN R, OUT L, OUT R

    LevelMeterView (LevelMeter& input, LevelMeter& output);

    // One frame: `seconds` since the previous one. Called by the display's
    // refresh; tests call it directly.
    void advance (double seconds);

    int getLitCells (int column) const noexcept { return lit[(size_t) column]; }
    double getLevelDb (int column) const noexcept { return ballistics[(size_t) column].levelDb; }
    int getRepaintRequests() const noexcept { return repaintRequests; }

    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<int> slot (int column, int cellFromTop) const;
    void buildCells();

    LevelMeter& input;
    LevelMeter& output;
    std::array<MeterBallistics, numColumns> ballistics;
    std::array<int, numColumns> lit {};
    // The drawn image of each cell lit, column by column, top to bottom.
    std::array<std::array<juce::Image, geometry::numMeterCells>, numColumns> cells;
    int repaintRequests = 0;
    double lastFrame = -1.0;
    juce::VBlankAttachment vblank;

    JUCE_DECLARE_NON_COPYABLE (LevelMeterView)
};
} // namespace labq8::ui
