// labq8_ui_assetgen: derives the editor's implementation assets from the
// approved design LABQ8_UI_DESIGN_APPROVED_01 (docs/design/, never modified).
//
// The master is a flat picture of the plug-in in one particular state. The
// editor needs the same picture WITHOUT the parts that change with the
// parameters, so it can draw those parts itself:
//
//   1. knob pointers   erased on the caps by angular interpolation;
//   2. knob value arcs erased by mirroring the (dark) right half of the ring;
//   3. display digits  erased by row-wise interpolation inside each box;
//   4. "o BAND 4"      erased (row-wise interpolation over the panel);
//   5. "Bell" + icon   erased inside the Filter Type box (arrow kept);
//   6. band buttons    button 4 (selected) replaced by button 5's pixels,
//                      dots and numbers erased on all eight;
//   7. screen          curves, nodes, labels and the illustrative spectrum
//                      removed: the plot background is re-synthesised from
//                      its clean rows, with the grid lines restored;
//   8. meters          illustrative levels removed (lit segments replaced by
//                      the unlit segment pattern of the same column);
//   9. scale labels    the five knobs whose printed scale differs from the
//                      public range are re-labelled (approved 29-10-2026):
//                      old numbers erased, new ones drawn centred on the old
//                      ink, in its sampled colour and measured height; the
//                      screen's dB axis likewise for +-24 dB (right-aligned);
//  10. Dynamic EQ pill erased (the editor draws its on / off / disabled
//                      state); the sprites are cut to the pill's shape.
//
// Everything else (chassis, logo, labels, scales, inactive controls) is the
// master's pixels, untouched. Output (PNG):
//   background.png         the cleaned 1672 x 941 background
//   dynamic_switch_on.png  the Dynamic EQ pill as drawn in the master (on),
//                          cut to the pill shape (alpha)
//   dynamic_switch_off.png the same pill mirrored and desaturated (off,
//                          approved 29-10-2026)
//
// Usage: labq8_ui_assetgen <master.png> <output_dir>

#include "ui/UiGeometry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace g = labq8::ui::geometry;

namespace
{
struct Pixel
{
    float r = 0, gr = 0, b = 0;
};

class Canvas
{
public:
    explicit Canvas (const juce::Image& image) : width (image.getWidth()), height (image.getHeight()), data ((size_t) (width * height))
    {
        const juce::Image::BitmapData bitmap (image, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const auto c = bitmap.getPixelColour (x, y);
                data[(size_t) (y * width + x)] = { (float) c.getRed(), (float) c.getGreen(), (float) c.getBlue() };
            }
    }

    Pixel& at (int x, int y) { return data[(size_t) (y * width + x)]; }
    Pixel get (int x, int y) const { return data[(size_t) (std::clamp (y, 0, height - 1) * width + std::clamp (x, 0, width - 1))]; }

    Pixel bilinear (float x, float y) const
    {
        const auto x0 = (int) std::floor (x), y0 = (int) std::floor (y);
        const auto fx = x - (float) x0, fy = y - (float) y0;
        const auto p00 = get (x0, y0), p10 = get (x0 + 1, y0), p01 = get (x0, y0 + 1), p11 = get (x0 + 1, y0 + 1);
        auto mix = [&] (float a, float b, float c, float d) { return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy; };
        return { mix (p00.r, p10.r, p01.r, p11.r), mix (p00.gr, p10.gr, p01.gr, p11.gr), mix (p00.b, p10.b, p01.b, p11.b) };
    }

    juce::Image toImage() const
    {
        juce::Image image (juce::Image::RGB, width, height, false);
        juce::Image::BitmapData bitmap (image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const auto& p = data[(size_t) (y * width + x)];
                auto q = [] (float v) { return (juce::uint8) std::clamp ((int) std::lround (v), 0, 255); };
                bitmap.setPixelColour (x, y, juce::Colour (q (p.r), q (p.gr), q (p.b)));
            }
        return image;
    }

    const int width, height;

private:
    std::vector<Pixel> data;
};

float lum (const Pixel& p) { return 0.299f * p.r + 0.587f * p.gr + 0.114f * p.b; }
Pixel lerp (const Pixel& a, const Pixel& b, float t) { return { a.r + (b.r - a.r) * t, a.gr + (b.gr - a.gr) * t, a.b + (b.b - a.b) * t }; }

float angleOf (float dx, float dy) // clockwise from 12 o'clock, [0, 360)
{
    auto a = std::atan2 (dx, -dy) * 180.0f / juce::MathConstants<float>::pi;
    return a < 0 ? a + 360.0f : a;
}

float angularDistance (float a, float b) // signed, (-180, 180]
{
    auto d = std::fmod (a - b + 540.0f, 360.0f) - 180.0f;
    return d;
}

Pixel polarSample (const Canvas& c, float cx, float cy, float radius, float angleDeg)
{
    const auto a = angleDeg * juce::MathConstants<float>::pi / 180.0f;
    return c.bilinear (cx + radius * std::sin (a), cy - radius * std::cos (a));
}

// Row-wise linear interpolation between the columns just outside [x0, x1].
void fillRows (Canvas& out, const Canvas& src, int x0, int x1, int y0, int y1)
{
    for (int y = y0; y <= y1; ++y)
    {
        const auto a = src.get (x0 - 1, y), b = src.get (x1 + 1, y);
        for (int x = x0; x <= x1; ++x)
            out.at (x, y) = lerp (a, b, (float) (x - x0 + 1) / (float) (x1 - x0 + 2));
    }
}

//==============================================================================
void eraseKnobs (Canvas& out, const Canvas& src)
{
    for (int k = 0; k < g::numKnobs; ++k)
    {
        const auto cx = g::knobCentres[(size_t) k].x, cy = g::knobCentres[(size_t) k].y;

        for (int y = (int) (cy - 56); y <= (int) (cy + 56); ++y)
            for (int x = (int) (cx - 56); x <= (int) (cx + 56); ++x)
            {
                const auto dx = (float) x - cx, dy = (float) y - cy;
                const auto r = std::sqrt (dx * dx + dy * dy);
                const auto phi = angleOf (dx, dy);

                // 1. Pointer(s): a dark radial line (r 21..41, ~3 px wide) on the cap.
                for (const auto pointer : g::masterPointerAngles[(size_t) k])
                    if (pointer >= 0.0f && r >= 17.0f && r <= 42.5f)
                    {
                        const auto halfWidth = (2.8f / r) * 180.0f / juce::MathConstants<float>::pi;
                        const auto d = angularDistance (phi, pointer);
                        if (std::abs (d) <= halfWidth)
                        {
                            const auto edge = halfWidth + 1.5f;
                            const auto left = polarSample (src, cx, cy, r, pointer - edge);
                            const auto right = polarSample (src, cx, cy, r, pointer + edge);
                            out.at (x, y) = lerp (left, right, (d + edge) / (2.0f * edge));
                        }
                    }

                // 2. Value arc (and its glow) between the cap and the ticks,
                //    on the left half: mirror of the dark right half.
                if (r >= 44.5f && r <= 54.5f)
                {
                    if (phi >= 200.0f)
                        out.at (x, y) = src.get ((int) std::lround (2.0f * cx - (float) x), y);
                    else if (phi <= 3.0f)
                        out.at (x, y) = polarSample (src, cx, cy, r, phi + 15.0f);
                }
            }
    }
}

void eraseDisplays (Canvas& out, const Canvas& src)
{
    for (const auto& span : g::displaySpans)
        fillRows (out, src, (int) span.left + 4, (int) span.right - 4, (int) g::displayTop + 6, (int) g::displayBottom - 6);
}

void eraseHeader (Canvas& out, const Canvas& src)
{
    const auto& a = g::bandLabelEraseArea;
    fillRows (out, src, (int) a.x, (int) (a.x + a.w), (int) a.y, (int) (a.y + a.h));
    const auto& f = g::filterTypeEraseArea;
    fillRows (out, src, (int) f.x, (int) (f.x + f.w), (int) f.y, (int) (f.y + f.h));
}

void eraseBandButtons (Canvas& out, const Canvas& src)
{
    // Button 4 is drawn selected in the master: take button 5's pixels.
    const auto left4 = (int) std::lround (g::bandButtonLeft (3));
    for (int y = (int) g::bandButtonTop - 4; y <= (int) g::bandButtonBottom + 5; ++y)
        for (int x = left4 - 5; x <= left4 + (int) g::bandButtonWidth + 5; ++x)
            out.at (x, y) = src.get (x + 120, y);

    const Canvas withButton4 = out;
    for (int b = 0; b < g::numBands; ++b)
    {
        const auto left = (int) std::lround (g::bandButtonLeft (b));
        fillRows (out, withButton4, left + 17, left + 73, (int) g::bandButtonCentreY - 11, (int) g::bandButtonCentreY + 12);
    }
}

void eraseScreen (Canvas& out, const Canvas& src)
{
    // The plot is re-synthesised, not in-painted: in-painting left ghosts of
    // the illustrative curves and spectrum. Model, per colour channel:
    //   B(x, y) = H(x) + V(y) + vertical grid(x) + horizontal grid(y)
    //   H(x)    running median (+-10 px) of the clean top rows 250..270
    //           (above every curve, node and label);
    //   V(y)    10th percentile of each row (the gaps between spectrum
    //           bars), smoothed, relative to the top rows;
    //   grids   excess of the clean top rows over H (vertical lines) and of
    //           the +12 dB row over its neighbours where it is clean
    //           (x 1200..1260), applied to every grid row.
    const int x0 = 395, x1 = 1322, y0 = 249, y1 = 476;
    const int w = x1 - x0 + 1;
    const int gridRows[] = { 289, 325, 326, 362, 399, 400, 437, 474 };
    const float rowWeight[] = { 1.0f, 0.6f, 0.6f, 1.0f, 0.6f, 0.6f, 1.0f, 1.0f };
    auto isGridRow = [&] (int y) { for (const auto r : gridRows) if (std::abs (y - r) <= 1) return true; return false; };

    std::vector<Pixel> top ((size_t) w);
    for (int x = 0; x < w; ++x)
    {
        Pixel s;
        for (int y = 250; y <= 270; ++y)
        {
            const auto p = src.get (x0 + x, y);
            s.r += p.r; s.gr += p.gr; s.b += p.b;
        }
        top[(size_t) x] = { s.r / 21, s.gr / 21, s.b / 21 };
    }

    auto median = [] (std::vector<float> v) { std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end()); return v[v.size() / 2]; };
    std::vector<Pixel> base ((size_t) w), vertical ((size_t) w);
    for (int x = 0; x < w; ++x)
    {
        std::vector<float> r, gr, b;
        for (int k = std::max (0, x - 10); k <= std::min (w - 1, x + 10); ++k)
        {
            r.push_back (top[(size_t) k].r); gr.push_back (top[(size_t) k].gr); b.push_back (top[(size_t) k].b);
        }
        base[(size_t) x] = { median (r), median (gr), median (b) };
        const auto& t = top[(size_t) x];
        if (lum (t) - lum (base[(size_t) x]) > 1.2f)
            vertical[(size_t) x] = { t.r - base[(size_t) x].r, t.gr - base[(size_t) x].gr, t.b - base[(size_t) x].b };
    }

    Pixel topLevel;
    for (int x = 0; x < w; ++x)
    {
        topLevel.r += base[(size_t) x].r; topLevel.gr += base[(size_t) x].gr; topLevel.b += base[(size_t) x].b;
    }
    topLevel = { topLevel.r / (float) w, topLevel.gr / (float) w, topLevel.b / (float) w };

    std::vector<Pixel> rowLevel ((size_t) (y1 - y0 + 1));
    for (int y = y0; y <= y1; ++y)
    {
        std::vector<float> r, gr, b;
        for (int x = 409; x <= 1300; ++x)
        {
            const auto p = src.get (x, y);
            r.push_back (p.r); gr.push_back (p.gr); b.push_back (p.b);
        }
        auto p10 = [] (std::vector<float> v) { std::nth_element (v.begin(), v.begin() + (long) v.size() / 10, v.end()); return v[v.size() / 10]; };
        rowLevel[(size_t) (y - y0)] = { p10 (r), p10 (gr), p10 (b) };
    }
    std::vector<Pixel> verticalProfile ((size_t) (y1 - y0 + 1));
    for (int y = y0; y <= y1; ++y)
    {
        Pixel s;
        float n = 0;
        for (int k = y - 6; k <= y + 6; ++k)
            if (k >= y0 && k <= y1 && ! isGridRow (k))
            {
                const auto& p = rowLevel[(size_t) (k - y0)];
                s.r += p.r; s.gr += p.gr; s.b += p.b; ++n;
            }
        if (n > 0)
            verticalProfile[(size_t) (y - y0)] = { s.r / n - topLevel.r, s.gr / n - topLevel.gr, s.b / n - topLevel.b };
    }
    // Rows 249..270 are the reference: no vertical correction there.
    for (int y = y0; y <= 270; ++y)
        verticalProfile[(size_t) (y - y0)] = {};

    Pixel horizontal;
    for (int x = 1200; x <= 1260; ++x)
    {
        const auto line = src.get (x, 289), above = src.get (x, 286), below = src.get (x, 292);
        horizontal.r += line.r - 0.5f * (above.r + below.r);
        horizontal.gr += line.gr - 0.5f * (above.gr + below.gr);
        horizontal.b += line.b - 0.5f * (above.b + below.b);
    }
    horizontal = { horizontal.r / 61, horizontal.gr / 61, horizontal.b / 61 };

    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
        {
            const auto& h = base[(size_t) (x - x0)];
            const auto& v = verticalProfile[(size_t) (y - y0)];
            const auto& line = vertical[(size_t) (x - x0)];
            Pixel p { h.r + v.r + line.r, h.gr + v.gr + line.gr, h.b + v.b + line.b };
            for (int k = 0; k < 8; ++k)
                if (y == gridRows[k] && x >= 408)
                {
                    p.r += horizontal.r * rowWeight[k]; p.gr += horizontal.gr * rowWeight[k]; p.b += horizontal.b * rowWeight[k];
                }
            out.at (x, y) = p;
        }
}

void eraseMeters (Canvas& out, const Canvas& src)
{
    for (const auto& column : g::meterColumns)
    {
        const auto centre = (int) std::lround (0.5f * (column.left + column.right));
        for (int y = (int) g::meterLitTop; y <= (int) g::meterLitBottom; ++y)
        {
            const bool gap = lum (src.get (centre, y)) < 60.0f;
            for (int x = (int) column.left - 6; x <= (int) column.right + 6; ++x)
                out.at (x, y) = src.get (x, (int) (gap ? g::meterTemplateGapRow : g::meterTemplateCellRow));
        }
    }
}

// Ink bounding box of `text` drawn with `font` (pixels above half brightness).
juce::Rectangle<int> inkBounds (const juce::Font& font, const juce::String& text)
{
    juce::Image image (juce::Image::RGB, 200, 80, true);
    {
        juce::Graphics gr (image);
        gr.setColour (juce::Colours::white);
        gr.setFont (font);
        gr.drawSingleLineText (text, 20, 50);
    }
    int x0 = 999, y0 = 999, x1 = -1, y1 = -1;
    for (int y = 0; y < 80; ++y)
        for (int x = 0; x < 200; ++x)
            if (image.getPixelAt (x, y).getBrightness() > 0.5f)
            {
                x0 = std::min (x0, x); y0 = std::min (y0, y); x1 = std::max (x1, x); y1 = std::max (y1, y);
            }
    return { x0 - 20, y0 - 50, x1 - x0 + 1, y1 - y0 + 1 }; // relative to the baseline origin
}

template <typename Labels>
void relabel (juce::Image& image, const Canvas& src, const Labels& labels, const juce::Font& font)
{
    for (const auto& label : labels)
    {
        const auto& r = label.oldInk;
        // Colour: mean of the brightest quarter of the old ink.
        std::vector<Pixel> ink;
        for (int y = (int) r.y; y < (int) (r.y + r.h); ++y)
            for (int x = (int) r.x; x < (int) (r.x + r.w); ++x)
                ink.push_back (src.get (x, y));
        std::sort (ink.begin(), ink.end(), [] (const Pixel& a, const Pixel& b) { return lum (a) > lum (b); });
        Pixel c;
        const auto n = std::max<size_t> (1, ink.size() / 4);
        for (size_t i = 0; i < n; ++i)
        {
            c.r += ink[i].r; c.gr += ink[i].gr; c.b += ink[i].b;
        }

        // Erase the old text (row-wise interpolation from just outside it).
        const int ex0 = (int) r.x - 3, ex1 = (int) (r.x + r.w) + 2, ey0 = (int) r.y - 3, ey1 = (int) (r.y + r.h) + 2;
        {
            juce::Image::BitmapData bitmap (image, juce::Image::BitmapData::readWrite);
            for (int y = ey0; y <= ey1; ++y)
            {
                const auto a = bitmap.getPixelColour (ex0 - 1, y), b = bitmap.getPixelColour (ex1 + 1, y);
                for (int x = ex0; x <= ex1; ++x)
                    bitmap.setPixelColour (x, y, a.interpolatedWith (b, (float) (x - ex0 + 1) / (float) (ex1 - ex0 + 2)));
            }
        }

        // New text: ink centred on the old ink, or right edge on its right edge.
        const auto text = juce::String::fromUTF8 (label.text);
        const auto box = inkBounds (font, text);
        const auto originX = label.rightAligned ? r.x + r.w - (float) box.getRight()
                                                : r.x + r.w * 0.5f - ((float) box.getX() + (float) box.getWidth() * 0.5f);
        const auto originY = r.y + r.h * 0.5f - ((float) box.getY() + (float) box.getHeight() * 0.5f);
        juce::Graphics gr (image);
        gr.setColour (juce::Colour ((juce::uint8) (c.r / (float) n), (juce::uint8) (c.gr / (float) n), (juce::uint8) (c.b / (float) n)));
        gr.setFont (font);
        gr.drawSingleLineText (text, juce::roundToInt (originX), juce::roundToInt (originY));
    }
}

void relabelScales (juce::Image& image, const Canvas& src)
{
    // Font height whose digit ink matches the master's: 12 rows above the
    // master's detection level (lum 95 on a dark panel), i.e. 13 rows at the
    // white-on-black half-brightness level used by inkBounds.
    float height = 10.0f;
    auto fontFor = [] (float h) { return juce::Font (juce::FontOptions ("Helvetica Neue", h, 0).withStyle ("Medium")); };
    while (height < 30.0f && inkBounds (fontFor (height), "0123456789").getHeight() < (int) g::scaleLabelInkHeight + 1)
        height += 0.25f;
    const auto font = fontFor (height);

    relabel (image, src, g::scaleLabels, font);
    relabel (image, src, g::screenAxisLabels, font);
}

void eraseSwitch (Canvas& out, const Canvas& src)
{
    // The pill's faint halo reaches a few px beyond the sprite on the left;
    // on the right the "Dynamic EQ" text starts at x 797, so stop at 794.
    const auto& r = g::dynamicSwitchSprite;
    fillRows (out, src, (int) r.x - 6, 794, (int) r.y - 2, (int) (r.y + r.h) - 1);
}

// Alpha mask: the pill outline (rounded rectangle), 1.5 px soft edge.
juce::Image cutToPill (const juce::Image& sprite)
{
    juce::Image out (juce::Image::ARGB, sprite.getWidth(), sprite.getHeight(), true);
    const auto& t = g::dynamicSwitchTrack;
    const auto& s = g::dynamicSwitchSprite;
    const auto radius = g::dynamicSwitchTrackRadius;
    for (int y = 0; y < sprite.getHeight(); ++y)
        for (int x = 0; x < sprite.getWidth(); ++x)
        {
            // Signed distance to the rounded rectangle (pixel centres).
            const auto px = s.x + (float) x + 0.5f, py = s.y + (float) y + 0.5f;
            const auto cx = std::clamp (px, t.x + radius, t.x + t.w - radius);
            const auto cy = std::clamp (py, t.y + radius, t.y + t.h - radius);
            const auto d = std::hypot (px - cx, py - cy) - radius;
            const auto alpha = std::clamp (0.5f - d / 1.5f, 0.0f, 1.0f);
            out.setPixelAt (x, y, sprite.getPixelAt (x, y).withAlpha (alpha));
        }
    return out;
}

juce::Image crop (const juce::Image& image, const g::Rect& r)
{
    return image.getClippedImage ({ (int) r.x, (int) r.y, (int) r.w, (int) r.h }).createCopy();
}

juce::Image switchOff (const juce::Image& on)
{
    juce::Image off (juce::Image::ARGB, on.getWidth(), on.getHeight(), true);
    for (int y = 0; y < on.getHeight(); ++y)
        for (int x = 0; x < on.getWidth(); ++x)
        {
            const auto c = on.getPixelAt (on.getWidth() - 1 - x, y);
            const auto l = (juce::uint8) std::clamp ((int) std::lround (0.85f * (0.299f * c.getRed() + 0.587f * c.getGreen() + 0.114f * c.getBlue())), 0, 255);
            off.setPixelAt (x, y, juce::Colour (l, l, l).withAlpha (c.getAlpha()));
        }
    return off;
}

bool writePng (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat png;
    return stream.openedOk() && png.writeImageToStream (image, stream);
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init; // fonts
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: labq8_ui_assetgen <master.png> <output_dir>\n");
        return 2;
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto masterFile = cwd.getChildFile (juce::String (argv[1]));
    const auto outDir = cwd.getChildFile (juce::String (argv[2]));
    const auto master = juce::ImageFileFormat::loadFrom (masterFile);
    if (! master.isValid() || master.getWidth() != g::referenceWidth || master.getHeight() != g::referenceHeight)
    {
        std::fprintf (stderr, "not the 1672 x 941 master: %s\n", argv[1]);
        return 1;
    }
    outDir.createDirectory();

    const Canvas src (master);
    Canvas out = src;
    eraseKnobs (out, src);
    eraseDisplays (out, src);
    eraseHeader (out, src);
    eraseBandButtons (out, src);
    eraseScreen (out, src);
    eraseMeters (out, src);
    eraseSwitch (out, src);

    const auto on = cutToPill (crop (master.convertedToFormat (juce::Image::ARGB), g::dynamicSwitchSprite));
    auto background = out.toImage();
    relabelScales (background, src);

    const bool ok = writePng (background, outDir.getChildFile ("background.png"))
                 && writePng (on, outDir.getChildFile ("dynamic_switch_on.png"))
                 && writePng (switchOff (on), outDir.getChildFile ("dynamic_switch_off.png"));
    std::printf ("%s\n", ok ? "assets written" : "FAILED to write assets");
    return ok ? 0 : 1;
}
