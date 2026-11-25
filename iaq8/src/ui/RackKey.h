#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace iaq8::ui
{
// A rack key with a LED, after the approved mockup (iaq8-mockup-v2, .key and
// .kl): the LED bar above the text, both centred.
//
//   dark   a dark key cap (ESCUCHAR, ENVIAR, DESHACER): gradient, 2 px black
//          border, a 3 px ring and a drop shadow around it; 17 px text.
//   apply  the orange key (APLICAR, the only one): 21 px dark text, a cream
//          LED and an orange glow.
//   slot   a key printed on the chassis (the eight keys under the screen):
//          only the text (12.5 px, up to two lines) and a small LED are
//          painted, the key body is the chassis's.
//
// The button's text is the caption, with '\n' between lines; text that does
// not fit the cap is drawn smaller. A disabled key is dimmed and takes no
// clicks. The component is the cap plus a margin for the ring, shadow and
// glow (setCapBounds places it); only the cap takes the mouse.
class RackKey final : public juce::Button
{
public:
    enum class Style { dark, apply, slot };

    RackKey (const juce::String& name, Style);

    Style getStyle() const noexcept { return style; }

    void setLed (bool on);
    bool isLedOn() const noexcept { return led; }

    // The cap in the parent's coordinates (the mockup's rectangle).
    void setCapBounds (juce::Rectangle<int> cap);
    juce::Rectangle<int> getCapBounds() const;

    bool hitTest (int x, int y) override;
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    int margin() const noexcept;
    juce::Rectangle<float> capArea() const;
    void paintCap (juce::Graphics&, juce::Rectangle<float> cap, bool highlighted, bool down) const;
    void paintLedAndText (juce::Graphics&, juce::Rectangle<float> cap) const;

    Style style;
    bool led = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackKey)
};
} // namespace iaq8::ui
