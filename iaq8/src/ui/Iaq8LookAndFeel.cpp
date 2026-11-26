#include "ui/Iaq8LookAndFeel.h"

#include "LabQ8Assets.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace iaq8::ui
{
namespace
{
//==============================================================================
// Embedded typefaces, created once (thread-safe static) and shared.
struct Typefaces
{
    juce::Typeface::Ptr vt323, barlowMedium, barlowSemiBold, barlowSemiCondensed;
};

juce::Typeface::Ptr load (const char* data, int size)
{
    auto typeface = juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    jassert (typeface != nullptr);
    return typeface;
}

const Typefaces& typefaces()
{
    static const Typefaces t {
        load (LabQ8Assets::VT323Regular_ttf, LabQ8Assets::VT323Regular_ttfSize),
        load (LabQ8Assets::BarlowMedium_ttf, LabQ8Assets::BarlowMedium_ttfSize),
        load (LabQ8Assets::BarlowSemiBold_ttf, LabQ8Assets::BarlowSemiBold_ttfSize),
        load (LabQ8Assets::BarlowSemiCondensedSemiBold_ttf, LabQ8Assets::BarlowSemiCondensedSemiBold_ttfSize),
    };
    return t;
}

// CSS font-size is the em size: JUCE's point height.
juce::Font fontOf (const juce::Typeface::Ptr& typeface, float px)
{
    return juce::Font (juce::FontOptions (typeface).withPointHeight (px));
}

//==============================================================================
// Wear mask: fractal gradient noise over a 160 px tile of the reference space
// (as the mockup's 160 px feTurbulence mask: fractalNoise, base frequency
// 0.75 / px, 3 octaves, seed 4; alpha = 3.05 - 3.6 x noise). Periodic in the
// tile, sampled twice per reference pixel, built once.
class WearMask
{
public:
    static constexpr int tile = 160;            // reference px
    static constexpr int samplesPerPixel = 2;
    static constexpr int size = tile * samplesPerPixel;
    static constexpr std::uint32_t seed = 4;

    WearMask() : alpha ((size_t) (size * size))
    {
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const auto px = (float) x / (float) samplesPerPixel, py = (float) y / (float) samplesPerPixel;
                float sum = 0.0f, amplitude = 1.0f, frequency = 0.75f;
                for (int octave = 0; octave < 3; ++octave)
                {
                    const auto period = (int) std::lround ((float) tile * frequency); // 120, 240, 480 cells
                    sum += amplitude * gradientNoise (px * frequency, py * frequency, period, (std::uint32_t) octave);
                    amplitude *= 0.5f;
                    frequency *= 2.0f;
                }
                const auto noise = (sum + 1.0f) * 0.5f;
                alpha[(size_t) (y * size + x)] = juce::jlimit (0.0f, 1.0f, 3.05f - 3.6f * noise);
            }
    }

    float at (juce::Point<float> reference) const noexcept
    {
        const auto wrap = [] (float v)
        {
            const auto t = std::fmod (v * (float) samplesPerPixel, (float) size);
            return t < 0.0f ? t + (float) size : t;
        };
        const auto fx = wrap (reference.x), fy = wrap (reference.y);
        const auto x0 = (int) fx, y0 = (int) fy;
        const auto x1 = (x0 + 1) % size, y1 = (y0 + 1) % size;
        const auto tx = fx - (float) x0, ty = fy - (float) y0;
        const auto v = [this] (int x, int y) { return alpha[(size_t) (y * size + x)]; };
        const auto top = v (x0, y0) + (v (x1, y0) - v (x0, y0)) * tx;
        const auto bottom = v (x0, y1) + (v (x1, y1) - v (x0, y1)) * tx;
        return top + (bottom - top) * ty;
    }

private:
    static std::uint32_t hash (std::uint32_t x, std::uint32_t y, std::uint32_t octave) noexcept
    {
        auto h = x * 0x8da6b343u ^ y * 0xd8163841u ^ (octave + 1u) * 0xcb1ab31fu ^ seed * 0x165667b1u;
        h ^= h >> 15;
        h *= 0x2c1b3c6du;
        h ^= h >> 12;
        h *= 0x297a2d39u;
        h ^= h >> 15;
        return h;
    }

    // Classic 2D gradient noise (eight unit gradients, quintic fade), with
    // the lattice wrapped every `period` cells so the tile repeats seamlessly.
    static float gradientNoise (float x, float y, int period, std::uint32_t octave) noexcept
    {
        static constexpr float d = 0.70710678f;
        static constexpr std::array<std::array<float, 2>, 8> gradients { { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { d, d }, { -d, d }, { d, -d }, { -d, -d } } };

        const auto ix = (int) std::floor (x), iy = (int) std::floor (y);
        const auto fx = x - (float) ix, fy = y - (float) iy;
        const auto dot = [&] (int cx, int cy, float dx, float dy)
        {
            const auto wx = (std::uint32_t) (((cx % period) + period) % period);
            const auto wy = (std::uint32_t) (((cy % period) + period) % period);
            const auto& g = gradients[hash (wx, wy, octave) & 7u];
            return g[0] * dx + g[1] * dy;
        };
        const auto fade = [] (float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };

        const auto n00 = dot (ix, iy, fx, fy), n10 = dot (ix + 1, iy, fx - 1.0f, fy);
        const auto n01 = dot (ix, iy + 1, fx, fy - 1.0f), n11 = dot (ix + 1, iy + 1, fx - 1.0f, fy - 1.0f);
        const auto u = fade (fx), v = fade (fy);
        const auto top = n00 + (n10 - n00) * u, bottom = n01 + (n11 - n01) * u;
        return top + (bottom - top) * v;
    }

    std::vector<float> alpha;
};

const WearMask& wearMask()
{
    static const WearMask mask;
    return mask;
}

//==============================================================================
struct Layout
{
    juce::Font font { juce::FontOptions() }; // with the letter spacing as tracking
    juce::StringArray lines;
    std::vector<float> widths;
    float lineHeight = 0.0f;
    juce::Rectangle<float> bounds; // the whole text and its shadow, reference px
};

Layout layoutOf (const SilkText& item)
{
    Layout l;
    const auto height = item.font.getHeight();
    l.font = item.font.withExtraKerningFactor (height > 0.0f ? item.letterSpacing / height : 0.0f);
    l.lines = juce::StringArray::fromLines (item.text);
    l.lineHeight = item.lineHeight > 0.0f ? item.lineHeight : height;

    float widest = 0.0f;
    for (const auto& line : l.lines)
    {
        l.widths.push_back (juce::GlyphArrangement::getStringWidth (l.font, line));
        widest = juce::jmax (widest, l.widths.back());
    }

    auto box = item.area.withHeight (juce::jmax (item.area.getHeight(), l.lineHeight * (float) l.lines.size()));
    if (widest > box.getWidth())
    {
        if (item.justification.testFlags (juce::Justification::horizontallyCentred))
            box = box.withSizeKeepingCentre (widest, box.getHeight());
        else if (item.justification.testFlags (juce::Justification::right))
            box = box.withLeft (box.getRight() - widest);
        else
            box = box.withWidth (widest);
    }
    l.bounds = box.expanded (2.0f).withTrimmedBottom (-1.0f); // antialiasing, the 1 px shadow below
    return l;
}

float lineX (const SilkText& item, float width)
{
    if (item.justification.testFlags (juce::Justification::horizontallyCentred))
        return item.area.getCentreX() - width * 0.5f;
    if (item.justification.testFlags (juce::Justification::right))
        return item.area.getRight() - width;
    return item.area.getX();
}

void drawText (juce::Graphics& g, const SilkText& item, const Layout& l)
{
    const auto halfLeading = (l.lineHeight - l.font.getHeight()) * 0.5f;
    for (int i = 0; i < l.lines.size(); ++i)
    {
        const auto baseline = item.area.getY() + (float) i * l.lineHeight + halfLeading + l.font.getAscent();
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (l.font, l.lines[i], lineX (item, l.widths[(size_t) i]), baseline);

        if (item.worn)
        {
            g.setColour (colours::silkShadow);
            glyphs.draw (g, juce::AffineTransform::translation (0.0f, 1.0f));
        }
        g.setColour (item.colour);
        glyphs.draw (g);
    }
}

//==============================================================================
// Block cursor of the command line (the mockup's .cur: 12 x 20 px, #b4ff9f,
// centred on the text line). It blinks with its own timer, changing every
// 500 ms (2 Hz; JUCE's caret uses 380 ms), and only while the owner has the
// focus; each blink repaints only its own small rectangle.
class BlockCaret final : public juce::CaretComponent
{
public:
    static constexpr int width = 12, height = 20, blinkMs = 500;

    explicit BlockCaret (juce::Component* keyFocusOwner) : juce::CaretComponent (keyFocusOwner), owner (keyFocusOwner) {}

    void setCaretPosition (const juce::Rectangle<int>& characterArea) override
    {
        blink.startTimer (blinkMs);
        setVisible (shouldBeShown());
        setBounds (characterArea.getX() + 1, characterArea.getCentreY() - height / 2 + 1, width, height);
    }

private:
    bool shouldBeShown() const
    {
        return owner == nullptr || (owner->hasKeyboardFocus (false) && ! owner->isCurrentlyBlockedByAnotherModalComponent());
    }

    juce::Component* owner;
    juce::TimedCallback blink { [this] { setVisible (shouldBeShown() && ! isVisible()); } };
};
} // namespace

//==============================================================================
Iaq8LookAndFeel::Iaq8LookAndFeel()
    : juce::LookAndFeel_V4 ({ colours::trayBottom,   // window background
                              colours::dropTop,      // widget background
                              colours::dropBottom,   // menu background
                              colours::dropBorder,   // outline
                              colours::silk,         // default text
                              colours::keyMiddle,    // default fill
                              colours::dropText,     // highlighted text
                              colours::dropBorder,   // highlighted fill
                              colours::dropText })   // menu text
{
    setDefaultSansSerifTypeface (typefaces().barlowMedium);
    setColour (juce::TextEditor::backgroundColourId, colours::valueBox);
    setColour (juce::TextEditor::textColourId, colours::phosphor);
    setColour (juce::TextEditor::highlightColourId, colours::phosphorDim.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, colours::cursor);
    setColour (juce::AlertWindow::backgroundColourId, colours::trayBottom);
    setColour (juce::AlertWindow::textColourId, colours::silk);
    setColour (juce::AlertWindow::outlineColourId, colours::dropBorder);
    setColour (juce::ComboBox::textColourId, colours::dropText);
    setColour (juce::ComboBox::arrowColourId, colours::dropArrow);
}

juce::CaretComponent* Iaq8LookAndFeel::createCaretComponent (juce::Component* keyFocusOwner)
{
    return new BlockCaret (keyFocusOwner);
}

juce::String Iaq8LookAndFeel::toCapitals (const juce::String& text)
{
    juce::String result;
    result.preallocateBytes (text.getNumBytesAsUTF8());
    for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
    {
        auto c = *p;
        if (c >= 0xe0 && c <= 0xfe && c != 0xf7)
            c -= 0x20;
        else
            c = juce::CharacterFunctions::toUpperCase (c);
        result += c;
    }
    return result;
}

juce::Font Iaq8LookAndFeel::screenFont (float px)
{
    return fontOf (typefaces().vt323, px);
}

juce::Font Iaq8LookAndFeel::labelFont (float px, bool semiBold)
{
    return fontOf (semiBold ? typefaces().barlowSemiBold : typefaces().barlowMedium, px);
}

juce::Font Iaq8LookAndFeel::keyFont (float px)
{
    return fontOf (typefaces().barlowSemiCondensed, px);
}

float Iaq8LookAndFeel::wearAt (juce::Point<float> reference)
{
    return wearMask().at (reference);
}

Iaq8LookAndFeel::WornText Iaq8LookAndFeel::renderWornText (const SilkText& item, float pixelScale)
{
    jassert (pixelScale > 0.0f);
    const auto l = layoutOf (item);

    // Start and end on device pixels, so drawing the image is a plain copy.
    const auto left = std::floor (l.bounds.getX() * pixelScale), top = std::floor (l.bounds.getY() * pixelScale);
    const auto width = juce::jmax (1, (int) std::ceil (l.bounds.getRight() * pixelScale - left));
    const auto height = juce::jmax (1, (int) std::ceil (l.bounds.getBottom() * pixelScale - top));
    const juce::Rectangle<float> area { left / pixelScale, top / pixelScale, (float) width / pixelScale, (float) height / pixelScale };

    juce::Image image (juce::Image::ARGB, width, height, true, juce::SoftwareImageType());
    {
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::translation (-area.getX(), -area.getY()).scaled (pixelScale));
        drawText (g, item, l);
    }

    if (! item.worn)
        return { image, area };

    const auto& mask = wearMask();
    juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            auto* pixel = reinterpret_cast<juce::PixelARGB*> (pixels.getPixelPointer (x, y));
            if (pixel->getAlpha() == 0)
                continue;
            const juce::Point<float> reference { area.getX() + ((float) x + 0.5f) / pixelScale,
                                                 area.getY() + ((float) y + 0.5f) / pixelScale };
            pixel->multiplyAlpha (mask.at (reference));
        }

    return { image, area };
}

void Iaq8LookAndFeel::drawWornText (juce::Graphics& g, const SilkText& item)
{
    const auto scale = juce::jmax (0.1f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto worn = renderWornText (item, scale);
    g.drawImage (worn.image, worn.area);
}

//==============================================================================
void Iaq8LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown, int, int, int, int,
                                    juce::ComboBox& box)
{
    const auto b = juce::Rectangle<float> ((float) width, (float) height);
    const auto radius = 4.0f;
    const auto dim = box.isEnabled() ? 1.0f : 0.5f;

    g.setGradientFill (juce::ColourGradient (colours::dropTop.withMultipliedAlpha (dim), 0.0f, b.getY(),
                                             colours::dropBottom.withMultipliedAlpha (dim), 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, radius);
    if (isButtonDown)
    {
        g.setColour (juce::Colours::black.withAlpha (0.15f));
        g.fillRoundedRectangle (b, radius);
    }
    // inset 0 1px 0 rgba(255,255,255,.06), then the 1 px border.
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (b.reduced (radius, 0.0f).withY (1.0f).withHeight (1.0f));
    g.setColour (colours::dropBorder.withMultipliedAlpha (dim));
    g.drawRoundedRectangle (b.reduced (0.5f), radius, 1.0f);

    // The triangle (border-top 8 px, 7 px each side).
    const auto right = b.getRight() - comboPadding, top = b.getCentreY() - comboArrowHeight * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (right - comboArrowWidth, top, right, top, right - comboArrowWidth * 0.5f, top + comboArrowHeight);
    g.setColour (colours::dropArrow.withMultipliedAlpha (dim));
    g.fillPath (arrow);
}

juce::Font Iaq8LookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return labelFont (18.0f, false);
}

void Iaq8LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const auto left = (int) comboPadding;
    const auto right = (int) (comboPadding + comboArrowWidth + 8.0f); // the arrow and a gap
    label.setBounds (left, 0, juce::jmax (0, box.getWidth() - left - right), box.getHeight());
    label.setBorderSize ({});
    label.setFont (getComboBoxFont (box));
}

void Iaq8LookAndFeel::drawLamp (juce::Graphics& g, juce::Rectangle<float> lamp, bool on, LampColour colour)
{
    juce::Path dot;
    dot.addEllipse (lamp);
    // CSS "circle at 40% 35%": the gradient reaches the farthest corner.
    const auto centre = lamp.getRelativePoint (0.4f, 0.35f);
    const auto edge = centre + juce::Point<float> (lamp.getWidth() * 0.6f, lamp.getHeight() * 0.65f);

    if (! on)
    {
        // box-shadow: inset 0 1px 3px #000, 0 1px 0 rgba(255,255,255,.12).
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillEllipse (lamp.translated (0.0f, 1.0f));
        g.setGradientFill (juce::ColourGradient (colours::lampOffCentre, centre, colours::lampOffEdge, edge, true));
        g.fillPath (dot);
        g.saveState();
        g.reduceClipRegion (dot);
        juce::Path outside;
        outside.addRectangle (lamp.expanded (6.0f));
        outside.addEllipse (lamp.translated (0.0f, 1.0f));
        outside.setUsingNonZeroWinding (false);
        juce::DropShadow (juce::Colours::black, 3, {}).drawForPath (g, outside);
        g.restoreState();
        return;
    }

    const auto green = colour == LampColour::green;
    // box-shadow: 0 0 10px (glow), 0 0 22px (35 %).
    juce::DropShadow ((green ? juce::Colour (0xff50ff3c) : juce::Colour (0xffff8c28)).withAlpha (0.35f), 22, {}).drawForPath (g, dot);
    juce::DropShadow (green ? colours::lampGreenGlow : colours::lampOrangeGlow, 10, {}).drawForPath (g, dot);
    juce::ColourGradient fill (green ? juce::Colour (0xffe6ffd9) : juce::Colour (0xfffff0dc), centre,
                               green ? juce::Colour (0xff2c9a1f) : juce::Colour (0xffb9520f), edge, true);
    fill.addColour (0.4, green ? colours::lampGreen : colours::lampOrange);
    g.setGradientFill (fill);
    g.fillPath (dot);
}
} // namespace iaq8::ui
