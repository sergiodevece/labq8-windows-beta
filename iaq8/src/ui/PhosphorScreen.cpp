#include "ui/PhosphorScreen.h"

#include "dsp/BiquadDesign.h"
#include "ui/Iaq8Geometry.h"
#include "ui/Iaq8LookAndFeel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace iaq8::ui
{
namespace
{
using labq8::dsp::BandSettings;
using labq8::dsp::FilterType;

juce::String utf8 (const char* text)
{
    return juce::String (juce::CharPointer_UTF8 (text));
}

// VT323 at a CSS size with CSS letter-spacing (JUCE's kerning factor is a
// fraction of the font height).
juce::Font screenFont (float px, float letterSpacingPx)
{
    auto font = Iaq8LookAndFeel::screenFont (px);
    return letterSpacingPx > 0.0f ? font.withExtraKerningFactor (letterSpacingPx / font.getHeight()) : font;
}

// Capitals, as on the mockup, including the accented Latin-1 letters
// (String::toUpperCase follows the C locale and leaves them).
juce::String upper (const juce::String& text)
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

juce::Font chatFont() { return screenFont (PhosphorScreen::chatFontPx, 0.5f); }
juce::Font statusFont() { return screenFont (PhosphorScreen::statusFontPx, 1.5f); }

float widthOf (const juce::Font& font, const juce::String& text)
{
    return text.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (font, text);
}

// Draws one line of text with its baseline at `baseline` (floats, so the
// layout is exact under the zoom).
void drawText (juce::Graphics& g, const juce::Font& font, const juce::String& text, float x, float baseline)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, x, baseline);
    glyphs.draw (g);
}

// Baseline of a CSS line box of `lineHeight` starting at `top` (half-leading).
float baselineIn (const juce::Font& font, float top, float lineHeight)
{
    return top + (lineHeight - font.getHeight()) * 0.5f + font.getAscent();
}

//==============================================================================
// Bands and response.
bool sameBand (const BandSettings& a, const BandSettings& b)
{
    return juce::exactlyEqual (a.frequencyHz, b.frequencyHz) && juce::exactlyEqual (a.gainDb, b.gainDb)
        && juce::exactlyEqual (a.q, b.q) && a.bypassed == b.bypassed && a.type == b.type;
}

bool sameSet (const dsp::BandSet& a, const dsp::BandSet& b)
{
    return std::equal (a.begin(), a.end(), b.begin(), sameBand);
}

// A band changes the sound: not bypassed, and not a 0 dB bell or shelf
// (an exact identity).
bool isActive (const BandSettings& b)
{
    return ! b.bypassed && (! labq8::dsp::usesGain (b.type) || std::abs (b.gainDb) >= 0.05);
}

labq8::dsp::BiquadCoefficients design (const BandSettings& b)
{
    const auto sr = PhosphorScreen::curveSampleRate;
    const auto q = labq8::dsp::effectiveQ (b.type, labq8::dsp::clampQ (b.q));
    return labq8::dsp::designFilter (b.type, sr, b.frequencyHz, b.gainDb, q);
}

double responseDb (const std::vector<labq8::dsp::BiquadCoefficients>& filters, double hz)
{
    double db = 0.0;
    for (const auto& c : filters)
        db += 20.0 * std::log10 (std::max (1.0e-9, labq8::dsp::magnitudeAt (c, hz, PhosphorScreen::curveSampleRate)));
    return db;
}

std::vector<labq8::dsp::BiquadCoefficients> filtersOf (const dsp::BandSet& set)
{
    std::vector<labq8::dsp::BiquadCoefficients> filters;
    for (const auto& b : set)
        if (isActive (b))
            filters.push_back (design (b));
    return filters;
}

std::vector<PhosphorScreen::CurvePoint> curveOf (const dsp::BandSet& set)
{
    const auto filters = filtersOf (set);
    std::vector<PhosphorScreen::CurvePoint> points;
    points.reserve ((size_t) PhosphorScreen::curvePoints);
    for (int i = 0; i < PhosphorScreen::curvePoints; ++i)
    {
        const auto hz = 20.0 * std::pow (1000.0, (double) i / (double) (PhosphorScreen::curvePoints - 1));
        points.push_back ({ hz, responseDb (filters, hz) });
    }
    return points;
}

juce::String number (double value, int maxDecimals)
{
    const auto scale = std::pow (10.0, maxDecimals);
    const auto rounded = std::round (value * scale) / scale;
    if (std::abs (rounded - std::round (rounded)) < 1.0e-9)
        return juce::String ((int) std::lround (rounded));
    return juce::String (rounded, maxDecimals);
}

juce::String gainText (double db)
{
    const auto rounded = std::round (db * 10.0) / 10.0;
    return (rounded > 0.0 ? "+" : "") + number (rounded, 1) + " DB";
}

juce::String hzText (double hz)
{
    if (hz < 1000.0)
        return juce::String ((int) std::lround (hz)) + " HZ";
    return number (hz / 1000.0, 1) + " KHZ";
}

juce::String typeText (const BandSettings& b)
{
    const auto boost = b.gainDb > 0.0;
    switch (b.type)
    {
        case FilterType::bell:      return "CAMPANA";
        case FilterType::lowShelf:  return boost ? "REALCE DE GRAVES" : "RECORTE DE GRAVES";
        case FilterType::highShelf: return boost ? "REALCE DE AGUDOS" : "RECORTE DE AGUDOS";
        case FilterType::lowCut:    return "CORTE DE GRAVES";
        case FilterType::highCut:   return "CORTE DE AGUDOS";
        case FilterType::notch:     return "MUESCA";
    }
    return {};
}

// Q in plain words: how wide a bell or notch is, how steep a shelf or cut.
juce::String widthText (const BandSettings& b)
{
    const auto q = labq8::dsp::effectiveQ (b.type, labq8::dsp::clampQ (b.q));
    juce::String words;
    if (b.type == FilterType::bell || b.type == FilterType::notch)
        words = q < 0.9 ? "ANCHA" : (q <= 2.5 ? "MEDIA" : "ESTRECHA");
    else
        words = q < 0.6 ? "SUAVE" : (q <= 1.0 ? "NORMAL" : "MARCADA");
    return words + " (Q " + juce::String (q, 1) + ")";
}

//==============================================================================
// Pixel helpers for the cached layers.
struct Rgb
{
    float r = 0.0f, g = 0.0f, b = 0.0f;
};

Rgb rgbOf (juce::Colour c)
{
    return { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() };
}

Rgb mix (Rgb a, Rgb b, float t)
{
    return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
}

// Premultiplied "over".
struct Premul
{
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;

    void over (Rgb colour, float alpha)
    {
        alpha = juce::jlimit (0.0f, 1.0f, alpha);
        r = colour.r * alpha + r * (1.0f - alpha);
        g = colour.g * alpha + g * (1.0f - alpha);
        b = colour.b * alpha + b * (1.0f - alpha);
        a = alpha + a * (1.0f - alpha);
    }
};

std::uint8_t toByte (float v)
{
    return (std::uint8_t) juce::jlimit (0, 255, (int) std::lround (v * 255.0f));
}

// Direct pixel access (software images: ARGB is premultiplied PixelARGB, RGB is PixelRGB).
juce::PixelARGB& argbAt (const juce::Image::BitmapData& data, int x, int y)
{
    return *reinterpret_cast<juce::PixelARGB*> (data.getPixelPointer (x, y));
}

const juce::PixelRGB& rgbAt (const juce::Image::BitmapData& data, int x, int y)
{
    return *reinterpret_cast<const juce::PixelRGB*> (data.getPixelPointer (x, y));
}

// Fixed grain: one value in [0, 1) per reference pixel (integer hash).
float grainAt (int x, int y)
{
    auto h = (std::uint32_t) x * 374761393u + (std::uint32_t) y * 668265263u + 0x9e3779b9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float) (h & 0xffffu) / 65536.0f;
}

// Signed distance to a rounded rectangle centred at the origin (negative inside).
float roundedRectDistance (float px, float py, float halfW, float halfH, float radius)
{
    const auto qx = std::abs (px) - (halfW - radius), qy = std::abs (py) - (halfH - radius);
    const auto ox = std::max (qx, 0.0f), oy = std::max (qy, 0.0f);
    return std::sqrt (ox * ox + oy * oy) + std::min (std::max (qx, qy), 0.0f) - radius;
}

// CSS inset box-shadow (no offset, no spread) at `inside` px from the edge:
// the outside of the box blurred with sigma = blur / 2.
float insetShadow (float inside, float blur)
{
    const auto sigma = blur * 0.5f;
    return 0.5f * std::erfc (inside / (sigma * 1.41421356f));
}

// In-place box blur of radius r (zero outside), along rows or columns.
void boxBlur (std::vector<float>& data, std::vector<float>& scratch, int w, int h, int r, bool horizontal)
{
    if (r <= 0)
        return;
    const auto n = horizontal ? w : h, lines = horizontal ? h : w;
    const auto stride = horizontal ? 1 : w, lineStride = horizontal ? w : 1;
    const auto norm = 1.0f / (float) (2 * r + 1);
    scratch.resize ((size_t) n);
    for (int line = 0; line < lines; ++line)
    {
        auto* p = data.data() + (size_t) line * (size_t) lineStride;
        float sum = 0.0f;
        for (int i = 0; i <= std::min (r, n - 1); ++i)
            sum += p[(size_t) i * (size_t) stride];
        for (int i = 0; i < n; ++i)
        {
            scratch[(size_t) i] = sum * norm;
            const auto add = i + r + 1, remove = i - r;
            if (add < n)
                sum += p[(size_t) add * (size_t) stride];
            if (remove >= 0)
                sum -= p[(size_t) remove * (size_t) stride];
        }
        for (int i = 0; i < n; ++i)
            p[(size_t) i * (size_t) stride] = scratch[(size_t) i];
    }
}

// Gaussian blur approximated by three box blurs per direction.
void gaussianBlur (std::vector<float>& data, int w, int h, float sigma)
{
    std::vector<float> scratch;
    const auto ideal = std::sqrt (12.0f * sigma * sigma / 3.0f + 1.0f);
    const auto r = std::max (0, (int) std::lround ((ideal - 1.0f) * 0.5f));
    for (int pass = 0; pass < 3; ++pass)
    {
        boxBlur (data, scratch, w, h, r, true);
        boxBlur (data, scratch, w, h, r, false);
    }
}

float sampleBilinear (const std::vector<float>& data, int w, int h, float x, float y)
{
    x = juce::jlimit (0.0f, (float) (w - 1), x);
    y = juce::jlimit (0.0f, (float) (h - 1), y);
    const auto x0 = (int) x, y0 = (int) y;
    const auto x1 = std::min (x0 + 1, w - 1), y1 = std::min (y0 + 1, h - 1);
    const auto fx = x - (float) x0, fy = y - (float) y0;
    const auto at = [&] (int xx, int yy) { return data[(size_t) yy * (size_t) w + (size_t) xx]; };
    return (at (x0, y0) * (1.0f - fx) + at (x1, y0) * fx) * (1.0f - fy) + (at (x0, y1) * (1.0f - fx) + at (x1, y1) * fx) * fy;
}

// Mockup colours not in the shared palette.
const juce::Colour gridColour { 0xff3f8f3a };      // dotted grid, opacity .7
const juce::Colour zeroLineColour { 0xff4fa846 };  // 0 dB line, opacity .55
const juce::Colour appliedStroke { 0xff4c9e45 };
const juce::Colour appliedFill { juce::Colour (90, 200, 80).withAlpha (0.13f) };
const juce::Colour ruleColour { juce::Colour (110, 230, 100).withAlpha (0.28f) };
} // namespace

//==============================================================================
PhosphorScreen::PhosphorScreen()
{
    setOpaque (false);
    setInterceptsMouseClicks (true, false);
    for (auto& b : applied) // nothing applied yet: a flat line
        b.bypassed = true;
    appliedCurve = curveOf (applied);
}

PhosphorScreen::~PhosphorScreen()
{
    stopTimer();
}

float PhosphorScreen::xForHz (double hz) noexcept
{
    return textWidth * (float) (std::log10 (juce::jlimit (20.0, 20000.0, hz) / 20.0) / 3.0);
}

float PhosphorScreen::yForDb (double db) noexcept
{
    // 0 dB in the middle; +/-12 dB 54 px away (4.5 px per dB, as the mockup's -4 dB).
    const auto pxPerDb = (curveHeight * 0.5f - 2.0f) / (float) curveRangeDb;
    return curveHeight * 0.5f - pxPerDb * (float) juce::jlimit (-curveRangeDb, curveRangeDb, db);
}

//==============================================================================
void PhosphorScreen::contentChanged()
{
    contentDirty = true;
    pendingRepaint = true;
    if (! isTimerRunning())
        startTimerHz (60);
}

void PhosphorScreen::timerCallback()
{
    stopTimer();
    if (! pendingRepaint)
        return;
    pendingRepaint = false;
    ++repaintRequests;
    repaint();
}

void PhosphorScreen::setStatus (juce::String left, juce::String middle, juce::String right)
{
    const std::array<juce::String, 3> next { upper (left), upper (middle), upper (right) };
    if (next == status)
        return;
    status = next;
    contentChanged();
}

void PhosphorScreen::setLines (std::vector<Line> newLines)
{
    if (newLines == lines)
        return;
    const auto rowsBefore = (int) rows.size();
    lines = std::move (newLines);
    layoutRows();
    if (scrollRows > 0) // scrolled back: the view stays where it is
        scrollRows += (int) rows.size() - rowsBefore;
    clampScroll();
    contentChanged();
}

void PhosphorScreen::setCurves (const dsp::BandSet& newApplied, std::optional<dsp::BandSet> newPreview)
{
    const auto samePreview = newPreview.has_value() == preview.has_value() && (! preview.has_value() || sameSet (*newPreview, *preview));
    if (sameSet (newApplied, applied) && samePreview)
        return;
    applied = newApplied;
    preview = std::move (newPreview);

    appliedCurve = curveOf (applied);
    previewCurve = preview.has_value() ? curveOf (*preview) : std::vector<CurvePoint> {};

    // Markers and the detail table describe the proposal, or the applied
    // bands without one.
    const auto& shown = preview.has_value() ? *preview : applied;
    const auto filters = filtersOf (shown);
    markers.clear();
    detailRows.clear();
    for (size_t i = 0; i < shown.size(); ++i)
    {
        const auto& b = shown[i];
        if (! isActive (b))
            continue;
        const auto hz = juce::jlimit (20.0, 20000.0, b.frequencyHz);
        markers.push_back ({ { xForHz (hz), yForDb (responseDb (filters, hz)) },
                             labq8::dsp::usesGain (b.type) ? gainText (b.gainDb) : hzText (b.frequencyHz) });
        detailRows.push_back ({ (int) i + 1, typeText (b), hzText (b.frequencyHz),
                                labq8::dsp::usesGain (b.type) ? gainText (b.gainDb) : juce::String ("--"), widthText (b) });
    }
    contentChanged();
}

void PhosphorScreen::setListening (double progress)
{
    const auto blocks = [] (double p) { return p < 0.0 ? -1 : (int) std::lround (juce::jlimit (0.0, 1.0, p) * 10.0); };
    const auto changed = blocks (progress) != blocks (listening);
    listening = progress;
    if (changed)
    {
        clampScroll();
        contentChanged();
    }
}

void PhosphorScreen::setDetailVisible (bool shouldShow)
{
    if (shouldShow == detailVisible)
        return;
    detailVisible = shouldShow;
    contentChanged();
}

void PhosphorScreen::scrollToEnd()
{
    wheelRemainder = 0.0f;
    if (scrollRows == 0)
        return;
    scrollRows = 0;
    contentChanged();
}

void PhosphorScreen::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (detailVisible)
        return;
    // About two or three rows per notch of a wheel; like juce::Viewport,
    // deltaY already has the system's direction (positive: back in time).
    const auto amount = wheel.deltaY * 10.0f + wheelRemainder;
    const auto step = (int) amount;
    wheelRemainder = amount - (float) step;
    const auto before = scrollRows;
    scrollRows += step;
    clampScroll();
    if (scrollRows != before)
        contentChanged();
}

void PhosphorScreen::resized()
{
    clampScroll();
    contentDirty = true;
}

//==============================================================================
// Conversation layout: rows wrapped by words in the 920 px column; the first
// row of a message leaves the 74 px prefix column (IAQ8> / TÚ>), the next
// rows start at the left, as on the mockup.
void PhosphorScreen::layoutRows()
{
    rows.clear();
    const auto font = chatFont();
    const auto fits = [&] (const juce::String& s, float x) { return widthOf (font, s) <= textWidth - x; };

    for (size_t m = 0; m < lines.size(); ++m)
    {
        const auto& line = lines[m];
        const auto prefix = line.who == Line::Who::assistant ? juce::String ("IAQ8>")
                          : line.who == Line::Who::user      ? utf8 ("T\xc3\x9a>")
                                                             : juce::String();
        const auto colour = line.who == Line::Who::user ? colours::phosphorUser
                          : line.who == Line::Who::system ? colours::phosphorDim
                                                          : colours::phosphor;
        auto x = prefix.isEmpty() ? 0.0f : prefixWidth;
        const auto firstRow = rows.size();

        const auto push = [&] (const juce::String& text)
        {
            Row row;
            row.message = (int) m;
            row.prefix = rows.size() == firstRow ? prefix : juce::String();
            row.text = text;
            row.x = x;
            row.width = widthOf (font, text);
            row.colour = colour;
            rows.push_back (row);
            x = 0.0f;
        };

        for (const auto& paragraph : juce::StringArray::fromLines (upper (line.text)))
        {
            juce::String current;
            const auto paragraphStart = rows.size();
            for (auto word : juce::StringArray::fromTokens (paragraph, " \t", ""))
            {
                if (word.isEmpty())
                    continue;
                while (word.isNotEmpty())
                {
                    const auto candidate = current.isEmpty() ? word : current + " " + word;
                    if (fits (candidate, x))
                    {
                        current = candidate;
                        break;
                    }
                    if (current.isNotEmpty())
                    {
                        push (current);
                        current.clear();
                        continue;
                    }
                    // A word longer than the column is cut where it fills it.
                    int n = 1;
                    while (n < word.length() && fits (word.substring (0, n + 1), x))
                        ++n;
                    push (word.substring (0, n));
                    word = word.substring (n);
                }
            }
            if (current.isNotEmpty() || rows.size() == paragraphStart)
                push (current);
        }
        if (rows.size() == firstRow)
            push ({});
        rows.back().lastOfMessage = true;
    }
}

namespace
{
float rowHeight (const PhosphorScreen::Row& row)
{
    return PhosphorScreen::chatLineHeight + (row.lastOfMessage ? PhosphorScreen::chatParagraphGap : 0.0f);
}
} // namespace

// The rows shown: as many as fit, ending `scrollRows` before the last one;
// one row is kept for the listening bar while it shows.
std::vector<PhosphorScreen::Row> PhosphorScreen::getVisibleRows() const
{
    if (detailVisible || rows.empty())
        return {};
    auto room = (float) getHeight() - chatBottomMargin - chatTop;
    if (listening >= 0.0)
        room -= chatLineHeight;

    const auto end = (int) rows.size() - scrollRows;
    if (end <= 0)
        return {};
    auto start = end - 1;
    auto used = chatLineHeight;
    while (start > 0 && used + rowHeight (rows[(size_t) start - 1]) <= room)
        used += rowHeight (rows[(size_t) --start]);
    return { rows.begin() + start, rows.begin() + end };
}

void PhosphorScreen::clampScroll()
{
    // The furthest back: the first row at the top.
    auto room = (float) getHeight() - chatBottomMargin - chatTop;
    if (listening >= 0.0)
        room -= chatLineHeight;
    int fitting = rows.empty() ? 0 : 1;
    auto used = chatLineHeight;
    while (fitting < (int) rows.size() && used + rowHeight (rows[(size_t) fitting - 1]) <= room)
        used += rowHeight (rows[(size_t) fitting++ - 1]);
    scrollRows = juce::jlimit (0, std::max (0, (int) rows.size() - fitting), scrollRows);
}

//==============================================================================
// Content, in CRT reference pixels.
void PhosphorScreen::drawContent (juce::Graphics& g) const
{
    drawStatus (g);
    if (detailVisible)
        drawDetail (g);
    else
    {
        drawCurve (g);
        drawChat (g);
    }
}

void PhosphorScreen::drawStatus (juce::Graphics& g) const
{
    const auto font = statusFont();
    const auto baseline = padTop + font.getAscent();
    g.setColour (colours::phosphorDim);
    drawText (g, font, status[0], padLeft, baseline);
    drawText (g, font, status[1], padLeft + (textWidth - widthOf (font, status[1])) * 0.5f, baseline);
    drawText (g, font, status[2], padLeft + textWidth - widthOf (font, status[2]), baseline);

    // Dashed rule under the bar (CSS 1 px dashed: 3 px dashes, 3 px gaps).
    const auto ruleY = padTop + font.getHeight() + 3.0f;
    g.setColour (ruleColour);
    for (auto x = padLeft; x < padLeft + textWidth; x += 6.0f)
        g.fillRect (x, ruleY, std::min (3.0f, padLeft + textWidth - x), 1.0f);
}

namespace
{
juce::Path pathOf (const std::vector<PhosphorScreen::CurvePoint>& curve)
{
    juce::Path p;
    for (size_t i = 0; i < curve.size(); ++i)
    {
        const juce::Point<float> pt { PhosphorScreen::xForHz (curve[i].hz), PhosphorScreen::yForDb (curve[i].db) };
        if (i == 0)
            p.startNewSubPath (pt);
        else
            p.lineTo (pt);
    }
    return p;
}
} // namespace

void PhosphorScreen::drawCurve (juce::Graphics& g) const
{
    g.saveState();
    g.addTransform (juce::AffineTransform::translation (padLeft, curveTop));

    // Dotted grid (SVG stroke-dasharray 1 5): +/-8 dB and 100 Hz, 1 kHz, 10 kHz.
    g.setColour (gridColour.withMultipliedAlpha (0.7f));
    for (const auto db : { 8.0, -8.0 })
        for (auto x = 0.0f; x < textWidth; x += 6.0f)
            g.fillRect (x, yForDb (db) - 0.5f, 1.0f, 1.0f);
    const double decades[] = { 100.0, 1000.0, 10000.0 };
    for (const auto hz : decades)
        for (auto y = 0.0f; y < 100.0f; y += 6.0f)
            g.fillRect (xForHz (hz) - 0.5f, y, 1.0f, 1.0f);
    g.setColour (zeroLineColour.withMultipliedAlpha (0.55f));
    g.fillRect (0.0f, yForDb (0.0) - 0.5f, textWidth, 1.0f);

    // The applied curve is faint behind a proposal; on its own it is the
    // bright one.
    const auto hasPreview = ! previewCurve.empty();
    if (hasPreview)
    {
        // Faint fill between the applied curve and 0 dB.
        auto line = pathOf (appliedCurve);
        auto area = line;
        area.lineTo (textWidth, yForDb (0.0));
        area.lineTo (0.0f, yForDb (0.0));
        area.closeSubPath();
        g.setColour (appliedFill);
        g.fillPath (area);
        g.setColour (appliedStroke);
        g.strokePath (line, juce::PathStrokeType (1.0f));
    }

    // Axis: 100, 1K, 10K under the vertical lines.
    const auto axisFont = screenFont (17.0f, 0.0f);
    g.setColour (colours::phosphorDim);
    const char* axis[] = { "100", "1K", "10K" };
    for (size_t i = 0; i < 3; ++i)
        drawText (g, axisFont, axis[i], xForHz (decades[i]) - widthOf (axisFont, axis[i]) * 0.5f, 111.0f);

    g.restoreState();
    drawBrightCurve (g);
}

// The bright curve with its dots and labels (also drawn alone for its glow).
void PhosphorScreen::drawBrightCurve (juce::Graphics& g) const
{
    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::translation (padLeft, curveTop));
    const auto hasPreview = ! previewCurve.empty();
    g.setColour (colours::phosphorBright);
    g.strokePath (pathOf (hasPreview ? previewCurve : appliedCurve),
                  juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // A dot and a label on each active band.
    const auto labelFont = screenFont (19.0f, 0.0f);
    for (const auto& m : markers)
    {
        g.fillEllipse (m.position.x - 4.0f, m.position.y - 4.0f, 8.0f, 8.0f);
        const auto w = widthOf (labelFont, m.label);
        const auto x = juce::jlimit (0.0f, textWidth - w, m.position.x - w * 0.5f);
        auto below = m.position.y >= curveHeight * 0.5f;
        if (below && m.position.y + 24.0f > curveHeight)
            below = false;
        else if (! below && m.position.y - 22.0f < 0.0f)
            below = true;
        const auto baseline = below ? m.position.y + 22.0f : m.position.y - 7.0f;
        drawText (g, labelFont, m.label, x, baseline);
    }
}

void PhosphorScreen::drawChat (juce::Graphics& g) const
{
    const auto font = chatFont();
    auto top = chatTop;
    const auto visible = getVisibleRows();
    for (const auto& row : visible)
    {
        const auto baseline = baselineIn (font, top, chatLineHeight);
        g.setColour (row.colour);
        if (row.prefix.isNotEmpty())
            drawText (g, font, row.prefix, padLeft, baseline);
        drawText (g, font, row.text, padLeft + row.x, baseline);
        top += rowHeight (row);
    }
    if (listening >= 0.0)
        drawListening (g, top);
}

void PhosphorScreen::drawListening (juce::Graphics& g, float top) const
{
    const auto font = chatFont();
    const auto label = utf8 ("ESCUCHANDO");
    g.setColour (colours::phosphor);
    drawText (g, font, label, padLeft, baselineIn (font, top, chatLineHeight));

    const auto filled = (int) std::lround (juce::jlimit (0.0, 1.0, listening) * 10.0);
    auto x = padLeft + widthOf (font, label) + 12.0f;
    const auto y = top + (chatLineHeight - 17.0f) * 0.5f + 1.0f;
    for (int i = 0; i < 10; ++i, x += 15.0f)
    {
        if (i < filled)
            g.fillRect (x, y, 10.0f, 17.0f);
        else
            g.drawRect (x, y, 10.0f, 17.0f, 1.5f);
    }
}

void PhosphorScreen::drawDetail (juce::Graphics& g) const
{
    const float columns[] = { 0.0f, 90.0f, 360.0f, 520.0f, 680.0f };
    const auto headFont = screenFont (19.0f, 1.5f);
    const auto font = chatFont();
    auto top = curveTop + 4.0f;

    g.setColour (colours::phosphorDim);
    const char* heads[] = { "BANDA", "TIPO", "FRECUENCIA", "GANANCIA", "ANCHURA" };
    for (size_t i = 0; i < 5; ++i)
        drawText (g, headFont, heads[i], padLeft + columns[i], baselineIn (headFont, top, 22.0f));
    top += 26.0f;

    g.setColour (colours::phosphor);
    if (detailRows.empty())
    {
        drawText (g, font, "NINGUNA BANDA ACTIVA: EL SONIDO PASA SIN CAMBIOS.", padLeft, baselineIn (font, top, 26.0f));
        top += 26.0f;
    }
    for (const auto& r : detailRows)
    {
        const juce::String cells[] = { juce::String (r.band), r.type, r.frequency, r.gain, r.width };
        for (size_t i = 0; i < 5; ++i)
            drawText (g, font, cells[i], padLeft + columns[i], baselineIn (font, top, 26.0f));
        top += 26.0f;
    }
    if (listening >= 0.0)
        drawListening (g, top + 4.0f);
}

//==============================================================================
// Effects layer (size or scale change only). The mockup's .crt layers:
//   glass (background) | content + glow | ::before (roll band, scan lines) |
//   ::after (grain, reflection, highlights, inset shadows) | rounded corners.
// Everything but the content is fixed, so it is folded here into
//   backdrop     = (overlay over glass) x coverage   (premultiplied ARGB)
//   transmission = (1 - overlay alpha) x coverage
// and a screen image is backdrop + transmission x (ink + glow - glass x ink alpha).
void PhosphorScreen::buildEffects (int w, int h)
{
    ++builds.effects;
    const auto refW = (float) getWidth(), refH = (float) getHeight();
    const auto sx = (float) w / refW, sy = (float) h / refH;

    glass = juce::Image (juce::Image::RGB, w, h, false, juce::SoftwareImageType());
    backdrop = juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    transmission.assign ((size_t) w * (size_t) h, 0.0f);

    const auto centre = rgbOf (colours::crtCentre), middle = rgbOf (colours::crtMiddle), edge = rgbOf (colours::crtEdge);
    const Rgb black {}, white { 1.0f, 1.0f, 1.0f }, grainColour { 0.6f, 1.0f, 0.6f }, band { 140.0f / 255.0f, 1.0f, 140.0f / 255.0f };

    // linear-gradient(168deg): direction and gradient line length.
    const auto angle = juce::degreesToRadians (168.0f);
    const auto dirX = std::sin (angle), dirY = -std::cos (angle);
    const auto lineLength = std::abs (refW * dirX) + std::abs (refH * dirY);

    const juce::Image::BitmapData glassPixels (glass, juce::Image::BitmapData::writeOnly);
    const juce::Image::BitmapData backdropPixels (backdrop, juce::Image::BitmapData::writeOnly);

    for (int py = 0; py < h; ++py)
    {
        const auto v = ((float) py + 0.5f) / sy;
        // Scan lines: the part of this device row in the 1 px of every 3.
        const auto rowTop = (float) py / sy, rowBottom = (float) (py + 1) / sy;
        float scan = 0.0f;
        for (auto k = std::floor (rowTop / 3.0f) * 3.0f; k < rowBottom; k += 3.0f)
            scan += std::max (0.0f, std::min (rowBottom, k + 1.0f) - std::max (rowTop, k));
        scan /= (rowBottom - rowTop);

        // Faint roll band: transparent 38 %, rgba(140,255,140,.045) 44 %, transparent 52 %.
        const auto t = v / refH;
        const auto roll = t < 0.38f || t > 0.52f ? 0.0f
                        : t < 0.44f              ? 0.045f * (t - 0.38f) / 0.06f
                                                 : 0.045f * (0.52f - t) / 0.08f;

        for (int px = 0; px < w; ++px)
        {
            const auto u = ((float) px + 0.5f) / sx;

            const auto d = roundedRectDistance (u - refW * 0.5f, v - refH * 0.5f, refW * 0.5f, refH * 0.5f, geometry::crtCornerRadius);
            const auto coverage = juce::jlimit (0.0f, 1.0f, 0.5f - d * sx);
            if (coverage <= 0.0f)
                continue; // outside the rounded screen: transparent

            // Glass: radial-gradient(ellipse 75% 85% at 50% 48%).
            const auto ex = (u - refW * 0.5f) / (refW * 0.75f), ey = (v - refH * 0.48f) / (refH * 0.85f);
            const auto r = std::sqrt (ex * ex + ey * ey);
            const auto c = r < 0.5f ? mix (centre, middle, r / 0.5f) : mix (middle, edge, std::min (1.0f, (r - 0.5f) / 0.5f));
            auto& glassPixel = *reinterpret_cast<juce::PixelRGB*> (glassPixels.getPixelPointer (px, py));
            glassPixel.setARGB (255, toByte (c.r), toByte (c.g), toByte (c.b));

            Premul o;
            // ::before: roll band, scan lines on top.
            o.over (band, roll);
            o.over (black, 0.38f * scan);
            // ::after backgrounds, bottom to top: grain, diagonal reflection, highlights.
            o.over (grainColour, 0.09f * grainAt ((int) u, (int) v));
            const auto along = ((u - refW * 0.5f) * dirX + (v - refH * 0.5f) * dirY) / lineLength + 0.5f;
            if (along < 0.28f)
                o.over (white, 0.075f * (1.0f - along / 0.28f));
            const auto highlight = [&] (float cx, float cy, float rx, float ry, float alpha)
            {
                const auto hx = (u - refW * cx) / (refW * rx), hy = (v - refH * cy) / (refH * ry);
                const auto dist = std::sqrt (hx * hx + hy * hy);
                if (dist < 0.7f)
                    o.over (white, alpha * (1.0f - dist / 0.7f));
            };
            highlight (0.80f, 0.72f, 0.22f, 0.30f, 0.035f);
            highlight (0.18f, 0.20f, 0.30f, 0.22f, 0.07f);

            // Inset shadows (vignette), bottom to top: 2 px rim, 16 px, 70 px.
            const auto inside = -d;
            o.over (black, 0.8f * juce::jlimit (0.0f, 1.0f, (2.0f - inside) * sx + 0.5f));
            o.over (black, 0.9f * insetShadow (inside, 16.0f));
            o.over (black, 0.95f * insetShadow (inside, 70.0f));

            const auto through = 1.0f - o.a;
            argbAt (backdropPixels, px, py).setARGB (toByte (coverage),
                                                     toByte ((o.r + c.r * through) * coverage),
                                                     toByte ((o.g + c.g * through) * coverage),
                                                     toByte ((o.b + c.b * through) * coverage));
            transmission[(size_t) py * (size_t) w + (size_t) px] = through * coverage;
        }
    }
}

// Screen image (content change only): the content drawn alone, its glow, and
// both put behind the fixed effects.
void PhosphorScreen::buildScreenImage()
{
    ++builds.content;
    const auto w = glass.getWidth(), h = glass.getHeight();
    const auto sx = (float) w / (float) getWidth(), sy = (float) h / (float) getHeight();

    juce::Image content (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    {
        juce::Graphics g (content);
        g.addTransform (juce::AffineTransform::scale (sx, sy));
        drawContent (g);
    }

    // Glow source: the content's brightness on a grid of about one reference
    // pixel (k x k device pixels), so the blur cost does not grow with the zoom.
    const auto k = std::max (1, (int) std::floor (sx));
    const auto gw = (w + k - 1) / k, gh = (h + k - 1) / k;
    std::vector<float> source ((size_t) gw * (size_t) gh, 0.0f);
    const juce::Image::BitmapData contentPixels (content, juce::Image::BitmapData::readOnly);
    const auto cell = 1.0f / (float) (k * k * 255);
    for (int y = 0; y < h; ++y)
    {
        auto* sourceRow = source.data() + (size_t) (y / k) * (size_t) gw;
        for (int x = 0; x < w; ++x)
        {
            const auto& p = argbAt (contentPixels, x, y); // premultiplied
            if (p.getAlpha() != 0)
                sourceRow[x / k] += (float) std::max ({ p.getRed(), p.getGreen(), p.getBlue() }) * cell;
        }
    }

    // CSS text-shadow: 0 0 6px rgba(110,255,95,.75), 0 0 16px rgba(70,255,70,.3)
    // (sigma = blur / 2), which also stands in for the SVG curve's blur.
    auto nearGlow = source, farGlow = std::move (source);
    gaussianBlur (nearGlow, gw, gh, 3.0f * sx / (float) k);
    gaussianBlur (farGlow, gw, gh, 8.0f * sx / (float) k);
    constexpr float nearGain = 1.6f * 0.75f, farGain = 1.6f * 0.3f;

    // The bright curve's own glow (SVG feGaussianBlur 2.2, merged twice):
    // the curve, dots and labels drawn alone on the glow grid and blurred.
    std::vector<float> curveGlow;
    if (! detailVisible)
    {
        juce::Image alone (juce::Image::SingleChannel, gw, gh, true, juce::SoftwareImageType());
        {
            juce::Graphics g (alone);
            g.addTransform (juce::AffineTransform::scale (sx / (float) k, sy / (float) k));
            drawBrightCurve (g);
        }
        curveGlow.assign ((size_t) gw * (size_t) gh, 0.0f);
        const juce::Image::BitmapData alonePixels (alone, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < gh; ++y)
            for (int x = 0; x < gw; ++x)
                curveGlow[(size_t) y * (size_t) gw + (size_t) x] = (float) *alonePixels.getPixelPointer (x, y) / 255.0f;
        gaussianBlur (curveGlow, gw, gh, 2.2f * sx / (float) k);
    }
    const auto curveColour = rgbOf (colours::phosphorBright);
    const Rgb nearColour { 110.0f / 255.0f, 1.0f, 95.0f / 255.0f }, farColour { 70.0f / 255.0f, 1.0f, 70.0f / 255.0f };

    screenImage = backdrop.createCopy();
    const juce::Image::BitmapData glassPixels (glass, juce::Image::BitmapData::readOnly);
    const juce::Image::BitmapData out (screenImage, juce::Image::BitmapData::readWrite);

    for (int y = 0; y < h; ++y)
    {
        const auto gy = ((float) y + 0.5f) / (float) k - 0.5f;
        for (int x = 0; x < w; ++x)
        {
            const auto through = transmission[(size_t) y * (size_t) w + (size_t) x];
            if (through <= 0.0f)
                continue;

            const auto gx = ((float) x + 0.5f) / (float) k - 0.5f;
            const auto a = k == 1 ? nearGlow[(size_t) y * (size_t) gw + (size_t) x] : sampleBilinear (nearGlow, gw, gh, gx, gy);
            const auto b = k == 1 ? farGlow[(size_t) y * (size_t) gw + (size_t) x] : sampleBilinear (farGlow, gw, gh, gx, gy);
            auto glow = 0.0f;
            if (! curveGlow.empty())
            {
                const auto blurred = k == 1 ? curveGlow[(size_t) y * (size_t) gw + (size_t) x] : sampleBilinear (curveGlow, gw, gh, gx, gy);
                glow = 1.0f - (1.0f - blurred) * (1.0f - blurred); // two copies merged
            }
            const auto& ink = argbAt (contentPixels, x, y);
            if (ink.getAlpha() == 0 && a < 0.002f && b < 0.002f && glow < 0.002f)
                continue; // nothing on top of the backdrop here

            const auto near = std::min (1.0f, nearGain * a), far = std::min (1.0f, farGain * b);
            Rgb add { nearColour.r * near + farColour.r * far + curveColour.r * glow,
                      nearColour.g * near + farColour.g * far + curveColour.g * glow,
                      nearColour.b * near + farColour.b * far + curveColour.b * glow };
            if (ink.getAlpha() != 0)
            {
                const auto& base = rgbAt (glassPixels, x, y);
                const auto inkAlpha = (float) ink.getAlpha() / 255.0f;
                add.r += ((float) ink.getRed() - (float) base.getRed() * inkAlpha) / 255.0f;
                add.g += ((float) ink.getGreen() - (float) base.getGreen() * inkAlpha) / 255.0f;
                add.b += ((float) ink.getBlue() - (float) base.getBlue() * inkAlpha) / 255.0f;
            }

            auto& pixel = argbAt (out, x, y);
            const auto limit = (float) pixel.getAlpha() / 255.0f; // the corner coverage
            const auto channel = [&] (std::uint8_t current, float extra)
            {
                return toByte (juce::jlimit (0.0f, limit, (float) current / 255.0f + through * extra));
            };
            pixel.setARGB (pixel.getAlpha(), channel (pixel.getRed(), add.r), channel (pixel.getGreen(), add.g), channel (pixel.getBlue(), add.b));
        }
    }
    contentDirty = false;
}

void PhosphorScreen::paint (juce::Graphics& g)
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;
    // Device pixels per reference pixel: the zoom times the display density.
    const auto scale = juce::jmax (0.1f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto w = juce::roundToInt ((float) getWidth() * scale), h = juce::roundToInt ((float) getHeight() * scale);
    if (glass.getWidth() != w || glass.getHeight() != h)
    {
        buildEffects (w, h);
        contentDirty = true;
    }
    if (contentDirty || ! screenImage.isValid())
        buildScreenImage();
    g.drawImage (screenImage, getLocalBounds().toFloat());
}
} // namespace iaq8::ui
