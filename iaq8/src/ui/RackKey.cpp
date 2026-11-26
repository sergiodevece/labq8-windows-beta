#include "ui/RackKey.h"

#include "ui/Iaq8LookAndFeel.h"

namespace iaq8::ui
{
namespace
{
// Sizes from the mockup's .key / .key.apply / .kl styles (reference px).
struct Look
{
    float fontPx, letterSpacing, lineHeight; // lineHeight in px
    float ledWidth, ledHeight, ledGap;       // gap between the LED and the text
    juce::Colour text;
};

Look lookOf (RackKey::Style style)
{
    switch (style)
    {
        case RackKey::Style::apply: return { 21.0f, 2.4f, 21.0f * 1.2f, 22.0f, 4.0f, 6.0f, colours::applyText };
        case RackKey::Style::slot:  return { 12.5f, 0.9f, 12.5f * 1.05f, 16.0f, 3.0f, 4.0f, colours::keySlotText };
        case RackKey::Style::dark:  break;
    }
    return { 17.0f, 1.4f, 17.0f * 1.2f, 22.0f, 4.0f, 6.0f, colours::keyText };
}

juce::Path rounded (juce::Rectangle<float> r, float radius)
{
    juce::Path p;
    p.addRoundedRectangle (r, radius);
    return p;
}

void glow (juce::Graphics& g, const juce::Path& shape, juce::Colour colour, int radius)
{
    juce::DropShadow (colour, radius, {}).drawForPath (g, shape);
}
} // namespace

RackKey::RackKey (const juce::String& name, Style s) : juce::Button (name), style (s)
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void RackKey::setLed (bool on)
{
    if (on == led)
        return;
    led = on;
    repaint();
}

int RackKey::margin() const noexcept
{
    // Room for the 4 px rings and the shadow (and APLICAR's 18 px glow).
    switch (style)
    {
        case Style::apply: return 20;
        case Style::slot:  return 4;
        case Style::dark:  break;
    }
    return 12;
}

void RackKey::setCapBounds (juce::Rectangle<int> cap)
{
    setBounds (cap.expanded (margin()));
}

juce::Rectangle<int> RackKey::getCapBounds() const
{
    return getBounds().reduced (margin());
}

juce::Rectangle<float> RackKey::capArea() const
{
    return getLocalBounds().reduced (margin()).toFloat();
}

bool RackKey::hitTest (int x, int y)
{
    return capArea().contains ((float) x, (float) y);
}

void RackKey::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        juce::Button::mouseDown (e);
}

void RackKey::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        juce::Button::mouseUp (e);
}

void RackKey::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto cap = capArea();
    if (style != Style::slot)
        paintCap (g, cap, highlighted, down);

    // Pressed: the print sinks by a pixel.
    paintLedAndText (g, down && isEnabled() ? cap.translated (0.0f, 1.0f) : cap);

    if (style == Style::slot && isEnabled() && (highlighted || down))
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.03f : 0.05f));
        g.fillRoundedRectangle (cap, 4.0f);
    }
}

void RackKey::paintCap (juce::Graphics& g, juce::Rectangle<float> cap, bool highlighted, bool down) const
{
    const auto apply = style == Style::apply;
    const auto radius = 7.0f;

    // Outside: glow (APLICAR), drop shadow 0 4px 8px, rings 4 px #0c0d0c and 3 px #2b2d28.
    if (apply && isEnabled())
        glow (g, rounded (cap, radius), juce::Colour (0xffff8232).withAlpha (0.35f), 18);
    juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 8, { 0, 4 }).drawForPath (g, rounded (cap, radius));
    g.setColour (juce::Colour (0xff0c0d0c));
    g.fillRoundedRectangle (cap.expanded (4.0f), radius + 4.0f);
    g.setColour (colours::keyRing);
    g.fillRoundedRectangle (cap.expanded (3.0f), radius + 3.0f);

    // The cap: 2 px black border, vertical gradient.
    g.setColour (colours::keyBorder);
    g.fillRoundedRectangle (cap, radius);
    const auto inner = cap.reduced (2.0f);
    juce::ColourGradient fill (apply ? colours::applyTop : colours::keyTop, 0.0f, inner.getY(),
                               apply ? colours::applyBottom : colours::keyBottom, 0.0f, inner.getBottom(), false);
    fill.addColour (apply ? 0.45 : 0.55, apply ? colours::applyMiddle : colours::keyMiddle);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (inner, radius - 2.0f);

    // Inset light on top, inset shade at the bottom.
    g.saveState();
    g.reduceClipRegion (rounded (inner, radius - 2.0f));
    g.setColour (apply ? juce::Colour (0xffffe1be).withAlpha (0.55f) : juce::Colours::white.withAlpha (0.12f));
    g.fillRect (inner.withHeight (1.0f));
    const auto shadeHeight = apply ? 6.0f : 4.0f;
    const auto shadeColour = apply ? juce::Colour (0xff5a1e05).withAlpha (0.45f) : juce::Colours::black.withAlpha (0.5f);
    const auto shade = inner.withTop (inner.getBottom() - shadeHeight * 1.5f);
    g.setGradientFill (juce::ColourGradient (shadeColour.withAlpha (0.0f), 0.0f, shade.getY(), shadeColour, 0.0f, shade.getBottom(), false));
    g.fillRect (shade);

    // Hover lightens the cap a little, a press darkens it; a disabled key is dimmed.
    if (! isEnabled())
        g.fillAll (juce::Colours::black.withAlpha (apply ? 0.38f : 0.25f));
    else if (down)
        g.fillAll (juce::Colours::black.withAlpha (0.18f));
    else if (highlighted)
        g.fillAll (juce::Colours::white.withAlpha (0.04f));
    g.restoreState();
}

void RackKey::paintLedAndText (juce::Graphics& g, juce::Rectangle<float> cap) const
{
    const auto look = lookOf (style);
    const auto lines = juce::StringArray::fromLines (getButtonText());
    const auto hasText = getButtonText().isNotEmpty();

    // The text shrinks if a line is wider than the cap.
    const auto withSpacing = [&] (float s)
    {
        const auto f = Iaq8LookAndFeel::keyFont (look.fontPx * s);
        return f.withExtraKerningFactor (look.letterSpacing / f.getHeight());
    };
    auto font = withSpacing (1.0f);
    auto scale = 1.0f;
    const auto room = cap.getWidth() - 8.0f;
    for (const auto& line : lines)
        scale = juce::jmin (scale, room / juce::jmax (1.0f, juce::GlyphArrangement::getStringWidth (font, line)));
    if (scale < 1.0f)
        font = withSpacing (scale);

    const auto lineHeight = look.lineHeight * scale;
    const auto textHeight = hasText ? lineHeight * (float) lines.size() : 0.0f;
    const auto groupHeight = look.ledHeight + (hasText ? look.ledGap : 0.0f) + textHeight;
    auto top = cap.getCentreY() - groupHeight * 0.5f;

    // The LED.
    const juce::Rectangle<float> ledArea { cap.getCentreX() - look.ledWidth * 0.5f, top, look.ledWidth, look.ledHeight };
    const auto ledRadius = juce::jmin (2.0f, look.ledHeight * 0.5f);
    const auto dim = isEnabled() ? 1.0f : 0.4f;
    if (style == Style::apply)
    {
        if (isEnabled())
            glow (g, rounded (ledArea, ledRadius), juce::Colour (0xffffb15c), 8);
        g.setColour (colours::applyLed.withMultipliedAlpha (isEnabled() ? 1.0f : 0.55f));
        g.fillRoundedRectangle (ledArea, ledRadius);
    }
    else if (led)
    {
        glow (g, rounded (ledArea, ledRadius), juce::Colour (0xff5dff40), style == Style::slot ? 6 : 7);
        g.setColour (colours::ledOn);
        g.fillRoundedRectangle (ledArea, ledRadius);
    }
    else
    {
        g.setColour (colours::ledOff);
        g.fillRoundedRectangle (ledArea, ledRadius);
        g.setColour (juce::Colours::black.withAlpha (0.8f)); // inset 0 1px 1px #000
        g.fillRect (ledArea.reduced (ledRadius, 0.0f).withHeight (1.0f));
    }

    if (! hasText)
        return;

    // The caption, centred, one row per line (CSS line box: half-leading).
    top += look.ledHeight + look.ledGap;
    g.setColour (look.text.withMultipliedAlpha (dim));
    for (int i = 0; i < lines.size(); ++i)
    {
        const auto width = juce::GlyphArrangement::getStringWidth (font, lines[i]);
        const auto baseline = top + (float) i * lineHeight + (lineHeight - font.getHeight()) * 0.5f + font.getAscent();
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, lines[i], cap.getCentreX() - width * 0.5f, baseline);
        glyphs.draw (g);
    }
}
} // namespace iaq8::ui
