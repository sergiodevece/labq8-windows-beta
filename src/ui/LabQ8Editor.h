#pragma once

#include "ui/UiModel.h"

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

// UI-1 editor: the approved design LABQ8_UI_DESIGN_APPROVED_01, laid out in
// its 1672 x 941 reference space and scaled as a whole (fixed aspect ratio).
//
// Layers (docs/UI_IMPLEMENTATION.md):
//   A. static graphics   assets/ui/background.png (derived from the master)
//   B. interactive       knobs, Filter Type, Dynamic EQ, band selector
//   C. values / states   displays, band header, selector highlight
//   D. visualisation     static EQ response and nodes (from the parameters)
//
// The editor only talks to the public JUCE parameters (attachments, with
// begin / end gestures). It never touches the DSP: selecting a band only
// re-points the shared panel at that band's parameters.
class LabQ8Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr float defaultScale = 0.75f;
    static constexpr float minimumScale = 0.5f;

    explicit LabQ8Editor (LabQ8AudioProcessor& processor);
    ~LabQ8Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Band shown by the shared panel (0..7). Not a parameter: no audio effect.
    void selectBand (int band);
    int getSelectedBand() const noexcept { return selectedBand; }

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
    juce::Component& getBypassIndicator();
    juce::Component& getDynamicSwitch();
    float getScale() const noexcept;

private:
    void timerCallback() override;
    void attachPanel();
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
    std::vector<std::unique_ptr<juce::Component>> inactiveAreas;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 7> knobAttachments;
    juce::TooltipWindow tooltips { this, 600 };
    juce::Image scaledBackground;

    int selectedBand = 0;
    std::vector<float> lastSnapshot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabQ8Editor)
};
} // namespace labq8::ui
