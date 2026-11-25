#pragma once

#include "ui/Iaq8LookAndFeel.h"
#include "ui/Iaq8Preferences.h"
#include "ui/LevelMeterView.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <memory>

namespace iaq8
{
class Iaq8AudioProcessor;
}

namespace iaq8::ui
{
// The IAQ8 editor (Plan 3): the approved mockup laid out in its 1672 x 941
// reference space (ui/Iaq8Geometry.h) and scaled as a whole, as LABQ8's
// editor.
//
// Layers:
//   - background: iaq8_chassis.png, resampled once per size and density;
//   - content: a component in reference coordinates, scaled with an
//     AffineTransform; it paints the worn silkscreen (top row labels, lamp
//     captions and the text under the logo) from images cached per scale.
//     The screen, keys, top row controls and meters (later tasks) are its
//     children.
//
// Settings: a click on the panel's top-left screw or on the logo (or a
// right-click anywhere on the chassis) opens the settings menu: zoom, the
// assistant's access code (an asynchronous AlertWindow with a masked field,
// saved only through assist::Settings, never in the DAW state and never
// shown back or logged) and "Acerca de IAQ8".
//
// Zoom: the six sizes of LABQ8 (40 to 140 %, 60 % by default), only those
// that fit the screen, remembered in IAQ8's own preferences file. There is
// no resize corner: the host follows the editor.
//
// Message thread only. The editor never blocks and never touches the network.
class Iaq8Editor final : public juce::AudioProcessorEditor
{
public:
    static constexpr auto zoomChoices = preferences::zoomChoices;

    // Room left on the screen for the host's window around the editor (title
    // bar and plug-in header of Logic Pro), in points.
    static constexpr int hostWindowAllowanceWidth = 0, hostWindowAllowanceHeight = 80;

    // Settings menu: items 1..6 are zoomChoices[n - 1]; then these.
    static constexpr int accessCodeItem = 101, aboutItem = 102;

    explicit Iaq8Editor (Iaq8AudioProcessor& processor);
    ~Iaq8Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Resizes the editor (the AU / VST3 wrapper resizes the host's window)
    // and stores the choice. Only the six choices, and only those that fit
    // the screen (40 % always does); returns false and changes nothing
    // otherwise.
    bool setZoom (float zoom);
    float getScale() const noexcept;

    juce::PopupMenu createSettingsMenu() const;
    void handleSettingsMenuResult (int itemId);
    void showSettingsMenu (juce::Component& target);

    // Whether the editor at `zoom`, plus the host window allowance, fits in
    // `available` (a display's area without menu bar and Dock).
    static bool zoomFits (float zoom, juce::Rectangle<int> available);
    // Usable area of the display the editor is on (or, before it is shown,
    // of the display under the mouse, where the host opens it).
    juce::Rectangle<int> getAvailableScreenArea() const;
    // Tests and tools: pretend every display has this area (empty: real displays).
    static void setScreenAreaForTesting (juce::Rectangle<int> area);

    // Tests and snapshots.
    juce::Component& getContent() noexcept { return *content; }
    juce::Component& getSettingsHotSpot (int index); // 0: top-left screw, 1: logo
    juce::AlertWindow* getOpenDialog() const noexcept { return dialog.get(); }
    Iaq8AudioProcessor& getIaq8Processor() noexcept { return iaq8Processor; }

private:
    using DialogHandler = std::function<void (int result, juce::AlertWindow&)>;
    void openDialog (std::unique_ptr<juce::AlertWindow> window, DialogHandler onClose);
    void dialogFinished (const juce::AlertWindow* window, int result);
    void showAccessCodeDialog();
    void showAboutDialog();
    juce::PopupMenu createZoomMenu() const;

    Iaq8AudioProcessor& iaq8Processor;
    Iaq8LookAndFeel lookAndFeel;

    std::unique_ptr<juce::Component> content;
    std::array<std::unique_ptr<juce::Component>, 2> hotSpots;
    std::unique_ptr<labq8::ui::LevelMeterView> meters;
    std::unique_ptr<juce::AlertWindow> dialog;
    DialogHandler onDialogClosed;
    juce::TooltipWindow tooltips { this, 600 };
    juce::Image scaledBackground;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Iaq8Editor)
};
} // namespace iaq8::ui
