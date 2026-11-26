#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

// IAQ8's fonts, colours and drawing helpers, after the approved mockup
// (docs/superpowers/specs/iaq8-mockup-v2/index.html).
//
// Fonts: VT323 (screen), Barlow Medium / SemiBold (silkscreen labels) and
// Barlow Semi Condensed SemiBold (keys), embedded from iaq8/assets/fonts
// (SIL OFL 1.1). Sizes are CSS px of the reference space (the em size).

namespace iaq8::ui
{
namespace colours
{
// Silkscreen print and its shadow (text-shadow 0 1px 0 rgba(0,0,0,.45)).
inline const juce::Colour silk { 0xffdcd8c8 };
inline const juce::Colour silkShadow { juce::Colours::black.withAlpha (0.45f) };
inline const juce::Colour assistantTitle { 0xfff08a3c };
inline const juce::Colour assistantNote { 0xffcfcbbd };
inline const juce::Colour rule { juce::Colour (0xffdcd8c8).withAlpha (0.22f) };
inline const juce::Colour hint { 0xff8f8d83 };

// Phosphor screen.
inline const juce::Colour phosphor { 0xffa9ff92 };
inline const juce::Colour phosphorBright { 0xffb8ffa3 };
inline const juce::Colour phosphorDim { 0xff5fbf55 };
inline const juce::Colour phosphorUser { 0xff6fd062 };
inline const juce::Colour phosphorPlaceholder { 0xff4f9a46 };
inline const juce::Colour cursor { 0xffb4ff9f };
inline const juce::Colour crtCentre { 0xff0d2611 };
inline const juce::Colour crtMiddle { 0xff061409 };
inline const juce::Colour crtEdge { 0xff010502 };

// Trays and the SOURCE drop-down.
inline const juce::Colour trayTop { 0xff121311 };
inline const juce::Colour trayBottom { 0xff1b1c19 };
inline const juce::Colour dropTop { 0xff232421 };
inline const juce::Colour dropBottom { 0xff171816 };
inline const juce::Colour dropBorder { 0xff3b3d38 };
inline const juce::Colour dropText { 0xffeceadf };
inline const juce::Colour dropArrow { 0xffe8e6db };

// COMPARE faces.
inline const juce::Colour faceLitText { 0xff0d2a0b };
inline const juce::Colour faceUnlitText { 0xffc8c6ba };

// Lamps.
inline const juce::Colour lampOffCentre { 0xff3a2d22 };
inline const juce::Colour lampOffEdge { 0xff120d09 };
inline const juce::Colour lampGreen { 0xff6dff52 };
inline const juce::Colour lampGreenGlow { 0xff4dff3a };
inline const juce::Colour lampOrange { 0xffff9440 };
inline const juce::Colour lampOrangeGlow { 0xffff8a2a };

// Rack keys (dark) and APLICAR, the only orange key.
inline const juce::Colour keyText { 0xffdddbd0 };
inline const juce::Colour keySlotText { 0xffd9d7cb };
inline const juce::Colour keyTop { 0xff2d2e2b };
inline const juce::Colour keyMiddle { 0xff1c1d1b };
inline const juce::Colour keyBottom { 0xff161715 };
inline const juce::Colour keyBorder { 0xff070707 };
inline const juce::Colour keyRing { 0xff2b2d28 };
inline const juce::Colour ledOff { 0xff2c2620 };
inline const juce::Colour ledOn { 0xff7dff63 };
inline const juce::Colour applyTop { 0xfff39350 };
inline const juce::Colour applyMiddle { 0xffdf6a2a };
inline const juce::Colour applyBottom { 0xffbf5219 };
inline const juce::Colour applyText { 0xff1b0a02 };
inline const juce::Colour applyLed { 0xfffff1d6 };

// Value boxes of the lower panel.
inline const juce::Colour valueLabel { 0xffb9b6aa };
inline const juce::Colour valueBox { 0xff0b0c0b };
inline const juce::Colour valueText { 0xff9ef08f };
} // namespace colours

// One piece of silkscreen text in reference coordinates. `area` is where the
// text box starts (top-left, as the mockup's left / top) and how wide it is
// for centred text; the rendered image grows to hold the whole text.
struct SilkText
{
    juce::String text;            // lines separated by '\n'
    juce::Rectangle<float> area;
    juce::Font font;
    float letterSpacing = 0.0f;   // CSS letter-spacing, px
    float lineHeight = 0.0f;      // CSS line-height, px; 0: normal (one font height)
    juce::Justification justification = juce::Justification::left;
    juce::Colour colour = colours::silk;
    bool worn = true;             // false: plain print (no wear mask, no shadow)
};

// A static picture rendered once per device scale (and area), then copied.
// `drawAt` draws in the coordinates of `where`.
struct CachedLayer
{
    juce::Image image;
    float scale = 0.0f;
    juce::Rectangle<float> area;

    template <typename Draw>
    void draw (juce::Graphics& gr, juce::Rectangle<float> where, Draw&& drawAt)
    {
        const auto s = juce::jmax (0.1f, gr.getInternalContext().getPhysicalPixelScaleFactor());
        if (image.isNull() || std::abs (s - scale) > 0.001f || where != area)
        {
            image = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt (where.getWidth() * s)),
                                 juce::jmax (1, juce::roundToInt (where.getHeight() * s)), true, juce::SoftwareImageType());
            juce::Graphics ig (image);
            ig.addTransform (juce::AffineTransform::translation (-where.getX(), -where.getY()).scaled (s));
            drawAt (ig);
            scale = s;
            area = where;
        }
        gr.drawImage (image, where);
    }
};

class Iaq8LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    Iaq8LookAndFeel();

    // The SOURCE drop-down (the mockup's .drop, LABQ8's style): rounded 4 px,
    // gradient #232421 -> #171816, 1 px #3b3d38 border, a faint top light,
    // 18 px Barlow Medium #eceadf after 16 px of padding and a 14 x 8
    // triangle 16 px from the right.
    static constexpr float comboPadding = 16.0f, comboArrowWidth = 14.0f, comboArrowHeight = 8.0f;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW,
                       int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    // "Elige la fuente…" in the label's place (JUCE draws it at the box's left edge).
    void drawComboBoxTextWhenNothingSelected (juce::Graphics&, juce::ComboBox&, juce::Label&) override;

    // A status lamp (the mockup's .lamp): off, a dark recessed dot; on, a
    // green (.lamp.g) or orange (.lamp.o) dot with two glows that spill
    // 22 px around `lamp`.
    enum class LampColour { green, orange };
    static void drawLamp (juce::Graphics&, juce::Rectangle<float> lamp, bool on, LampColour);

    // Text fields get a block cursor (12 x 20 px, colours::cursor) that
    // blinks at 2 Hz while the field has the keyboard focus.
    juce::CaretComponent* createCaretComponent (juce::Component* keyFocusOwner) override;

    // Capitals, including the accented Latin-1 letters (String::toUpperCase
    // follows the C locale and leaves them): the screen and the key captions.
    static juce::String toCapitals (const juce::String& text);

    // The embedded typefaces are loaded once and shared by every editor.
    static juce::Font screenFont (float px);                 // VT323
    static juce::Font labelFont (float px, bool semiBold);   // Barlow Medium / SemiBold
    static juce::Font keyFont (float px);                    // Barlow Semi Condensed SemiBold

    // Worn silkscreen. The text (with its 1 px shadow) is rendered into an
    // image at `pixelScale` device pixels per reference pixel, and its alpha
    // is multiplied by a fixed noise mask laid over the reference space, so
    // the same letters wear the same way at every zoom. The mask is built
    // once from a fixed seed (fractal gradient noise, after the mockup's
    // feTurbulence mask). `area` is where the image goes in reference
    // coordinates: it holds the whole text and starts on a device pixel.
    struct WornText
    {
        juce::Image image;
        juce::Rectangle<float> area;
    };
    static WornText renderWornText (const SilkText& item, float pixelScale);
    // Renders and draws at once (uncached): `g` is in reference coordinates.
    static void drawWornText (juce::Graphics& g, const SilkText& item);

    // The mask at a reference position: 1 intact print, 0 worn away.
    static float wearAt (juce::Point<float> reference);
};
} // namespace iaq8::ui
