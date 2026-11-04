#pragma once

#include "ui/UiModel.h"
#include "ui/DynamicsOverlay.h"
#include "ui/LevelMeterView.h"
#include "ui/SpectrumView.h"
#include "ui/UiPreferences.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>

namespace labq8
{
class LabQ8AudioProcessor;
}

namespace labq8::ui
{
class KnobControl;
class ValueDisplay;
class BandButton;
class BandHeader;
class FilterTypeSelector;
class DynamicSwitch;
class BypassIndicator;
class ResponseScreen;
class SettingsButton;
class ViewTab;
class AnalyzerSelector;
class SpectrumScale;

// UI-1 editor: the approved design LABQ8_UI_DESIGN_APPROVED_01, laid out in
// its 1672 x 941 reference space and scaled as a whole (fixed aspect ratio).
//
// Layers (docs/UI_IMPLEMENTATION.md):
//   A. static graphics   assets/ui/background.png (derived from the master)
//   B. interactive       knobs, Filter Type, Dynamic EQ, band ON/OFF
//                        buttons, screen nodes and Q handles, settings
//   C. values / states   displays, band header and colour indicator
//   D. visualisation     static EQ response and nodes (from the parameters),
//                        IN / OUT sample-peak meters (UI-2a, LevelMeterView),
//                        spectrum analyzer (UI-2b, SpectrumView), dynamic
//                        response (UI-2c, DynamicsOverlay)
//
// UI-1.1: the eight buttons only switch their band on / off (b{n}_bypass);
// bands are selected and edited from their nodes on the screen; the zoom
// (40 / 60 / 80 / 100 / 120 / 140 %, 60 % by default) is a global preference
// of the user, chosen from the menu of the settings icon (or the context
// menu). There is no resize corner: the host only follows the editor.
//
// The editor only talks to the public JUCE parameters (attachments, with
// begin / end gestures). It never touches the DSP: selecting a band only
// re-points the shared panel at that band's parameters.
class LabQ8Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr float defaultScale = preferences::defaultZoom;
    static constexpr auto zoomChoices = preferences::zoomChoices;

    // Room left on the screen for the host's window around the editor (title
    // bar and plug-in header of Logic Pro), in points.
    static constexpr int hostWindowAllowanceWidth = 0, hostWindowAllowanceHeight = 80;

    explicit LabQ8Editor (LabQ8AudioProcessor& processor);
    ~LabQ8Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Band shown by the shared panel (0..7). Not a parameter: no audio effect.
    void selectBand (int band);
    int getSelectedBand() const noexcept { return selectedBand; }

    // Zoom: resizes the editor (the AU / VST3 wrapper resizes the host's
    // window with it) and stores the choice as the user's preference. Only
    // the six choices, and only those that fit the screen (40 % always
    // does); returns false and changes nothing otherwise. The menu is the
    // one of the settings icon and of the context menu (item n + 1 =
    // zoomChoices[n]); choices that do not fit are disabled and say so.
    bool setZoom (float zoom);
    juce::PopupMenu createZoomMenu() const;
    void handleZoomMenuResult (int itemId);

    // UI-2b: the screen's view (tab index: 0 EQ, 1 SPECTRUM, 2 DYN since
    // UI-2c, 3 ALL) and the analyzer's source. Per instance, in memory:
    // never part of the plugin state.
    static constexpr int viewEq = 0, viewSpectrum = 1, viewDyn = 2, viewAll = 3;
    void setView (int tab);
    int getView() const noexcept { return view; }
    void setAnalyzerSource (AnalyzerSource newSource);
    juce::PopupMenu createAnalyzerMenu() const; // item n + 1 = AnalyzerSource n
    void handleAnalyzerMenuResult (int itemId);

    // Whether the editor at `zoom`, plus the host window allowance, fits in
    // `available` (a display's area without menu bar and Dock).
    static bool zoomFits (float zoom, juce::Rectangle<int> available);
    // Usable area of the display the editor is on (or, before it is shown,
    // of the display under the mouse, where the host opens it).
    juce::Rectangle<int> getAvailableScreenArea() const;
    // Tests and tools: pretend every display has this area (empty: real displays).
    static void setScreenAreaForTesting (juce::Rectangle<int> area);

    // For tests and snapshots.
    juce::Slider& getKnob (PanelControl control);
    juce::Component& getContent() noexcept { return *content; }
    juce::Component& getBandButton (int band);
    void setFilterTypeIndex (int index);   // as if chosen in the Filter Type menu
    void toggleDynamicSwitch();            // as if the switch were clicked
    juce::String getDisplayText (PanelControl control) const;
    bool isDynamicSwitchAvailable() const;   // false for non-Bell types
    bool isDynamicSwitchOn() const;          // stored b{n}_dyn_on of the shown band
    bool isBypassShown() const;              // indicator dimmed
    juce::Component& getBypassIndicator();   // colour indicator only, no clicks
    juce::Component& getDynamicSwitch();
    juce::Component& getSettingsButton();
    juce::Component& getScreen();
    LevelMeterView& getMeters() noexcept { return *meters; }
    SpectrumView& getSpectrum() noexcept { return *spectrum; }
    DynamicsOverlay& getDynamics() noexcept { return *dynamics; }
    std::array<BandView, 8> getScreenBandsForTesting() const; // what the static curve is drawn from
    juce::Component& getViewTab (int tab);
    juce::Component& getAnalyzerSelector();
    int getScreenPaintCount() const noexcept; // times the EQ curve was really painted
    juce::Component& getSpectrumScale (bool left);
    std::vector<juce::Rectangle<float>> getSpectrumScaleLabels (bool left) const; // 7 labels, then "dBFS"
    // The 30 Hz poll of the parameters (curve and nodes follow the host's
    // automation); called by the editor's timer, tests call it directly.
    void refreshFromParameters();
    bool isBandButtonOn (int band) const;
    juce::Point<float> getNodePosition (int band) const;               // reference space
    std::pair<juce::Point<float>, juce::Point<float>> getQHandleCentres() const; // selected band
    bool areQHandlesShown() const;
    void showQHandles (int band);            // as if its node were clicked
    float getScale() const noexcept;

private:
    void timerCallback() override;
    void attachPanel();
    void showZoomMenu (juce::Component& target);
    std::vector<float> parameterSnapshot() const;

    LabQ8AudioProcessor& audioProcessor;
    juce::AudioProcessorValueTreeState& state;

    std::unique_ptr<juce::Component> content;
    std::array<std::unique_ptr<KnobControl>, 7> knobs;
    std::array<std::unique_ptr<ValueDisplay>, 7> displays;
    std::array<std::unique_ptr<BandButton>, 8> bandButtons;
    std::unique_ptr<BandHeader> header;
    std::unique_ptr<FilterTypeSelector> filterType;
    std::unique_ptr<DynamicSwitch> dynamicSwitch;
    std::unique_ptr<BypassIndicator> bypassIndicator;
    std::unique_ptr<ResponseScreen> screen;
    std::unique_ptr<SettingsButton> settingsButton;
    std::unique_ptr<LevelMeterView> meters;
    std::unique_ptr<SpectrumView> spectrum;
    std::unique_ptr<DynamicsOverlay> dynamics;
    std::array<std::unique_ptr<ViewTab>, 4> viewTabs;
    std::unique_ptr<AnalyzerSelector> analyzerSelector;
    std::unique_ptr<SpectrumScale> leftScale, rightScale;
    int view = viewAll;
    std::vector<std::unique_ptr<juce::Component>> inactiveAreas;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 7> knobAttachments;
    juce::TooltipWindow tooltips { this, 600 };
    juce::Image scaledBackground;

    int selectedBand = 0;
    std::vector<float> lastSnapshot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabQ8Editor)
};
} // namespace labq8::ui
