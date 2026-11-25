#pragma once

#include "dsp/AssistEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <optional>
#include <vector>

namespace iaq8::ui
{
// The green phosphor CRT of the approved mockup (iaq8-mockup-v2/index.html,
// .crt): a status bar, the EQ curve (applied and proposed), the conversation
// and, on demand, a read-only table of the active bands. A child of the
// editor's content, laid out in reference pixels (980 x 314 over
// geometry::crt); the editor's zoom scales it.
//
// Drawing. Two layers are cached as images at the device pixel scale
// (getPhysicalPixelScaleFactor, which includes the zoom):
//   - effects, rebuilt only when the size or scale changes: the glass (radial
//     gradient), scan lines (1 px of every 3, black 38 %), the faint roll
//     band, fixed grain, the diagonal reflection and highlights, the vignette
//     and the rounded corners (radius 22);
//   - the screen image, rebuilt only when the content changes: the content
//     (text and curves) drawn on its own, blurred copies of it added on top
//     (the phosphor glow: text-shadow-like blurs of about 3 and 8 px, plus
//     2.2 px on the bright curve; Gaussians made of three box blurs, on a
//     grid of about one reference pixel), then the effects.
// A paint copies one image. The setters compare with what is shown and only
// a real change schedules a repaint (coalesced by a short timer), so at rest
// the screen costs nothing. The blinking cursor is the editor's, not this.
//
// Text is shown in capitals, as on the mockup. Message thread only.
class PhosphorScreen final : public juce::Component,
                             private juce::Timer
{
public:
    struct Line
    {
        enum class Who { assistant, user, system } who;
        juce::String text;

        bool operator== (const Line& other) const { return who == other.who && text == other.text; }
    };

    // Reference layout (CRT pixels), from the mockup: padding 14 / 30, the
    // 920 x 112 curve under the status bar, the conversation below.
    static constexpr float padLeft = 30.0f, padTop = 14.0f;
    static constexpr float textWidth = 920.0f;
    static constexpr float curveTop = 42.0f, curveHeight = 112.0f;
    static constexpr float chatTop = 158.0f, chatBottomMargin = 10.0f;
    static constexpr float prefixWidth = 74.0f;
    static constexpr float chatFontPx = 24.0f, chatLineHeight = 24.48f, chatParagraphGap = 5.0f;
    static constexpr float statusFontPx = 19.0f;
    static constexpr double curveRangeDb = 12.0, curveSampleRate = 48000.0;
    static constexpr int curvePoints = 200;

    PhosphorScreen();
    ~PhosphorScreen() override;

    void setStatus (juce::String left, juce::String middle, juce::String right);
    void setLines (std::vector<Line> lines);
    void setCurves (const dsp::BandSet& applied, std::optional<dsp::BandSet> preview);
    // Shows "ESCUCHANDO" and a bar of ten blocks under the conversation;
    // a negative value hides it (the default).
    void setListening (double progress0to1);
    // The detail view replaces the curve and the conversation with a table
    // of the active bands (the proposal's, or the applied ones without one).
    void setDetailVisible (bool shouldShow);
    void scrollToEnd();

    bool isDetailVisible() const noexcept { return detailVisible; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    // Flushes a pending change into one repaint (public for tests).
    void timerCallback() override;

    //==========================================================================
    // Layout, exposed for tests and snapshots (reference pixels, relative to
    // the text column at padLeft for x and to the curve's top for markers).
    struct Row
    {
        int message = 0;
        juce::String prefix; // "IAQ8>" / "TÚ>" on a message's first row
        juce::String text;
        float x = 0.0f, width = 0.0f;
        juce::Colour colour;
        bool lastOfMessage = false;
    };
    struct CurvePoint
    {
        double hz, db;
    };
    struct Marker
    {
        juce::Point<float> position; // in the 920 x 112 curve box
        juce::String label;          // "-4 DB", "+2.5 DB", "80 HZ"
    };
    struct DetailRow
    {
        int band = 0; // 1..8
        juce::String type, frequency, gain, width;
    };
    struct LayerBuilds
    {
        int effects = 0, content = 0;
        bool operator== (const LayerBuilds& o) const { return effects == o.effects && content == o.content; }
    };

    const std::vector<Row>& getAllRows() const noexcept { return rows; }
    int getRowCount() const noexcept { return (int) rows.size(); }
    std::vector<Row> getVisibleRows() const;
    const std::vector<Marker>& getMarkers() const noexcept { return markers; }
    const std::vector<CurvePoint>& getAppliedCurve() const noexcept { return appliedCurve; }
    const std::vector<CurvePoint>& getPreviewCurve() const noexcept { return previewCurve; }
    const std::vector<DetailRow>& getDetailRows() const noexcept { return detailRows; }
    const std::array<juce::String, 3>& getStatus() const noexcept { return status; }
    int getRepaintRequests() const noexcept { return repaintRequests; }
    LayerBuilds getLayerBuilds() const noexcept { return builds; }

    // Curve box mapping: 20 Hz..20 kHz (log) across 920 px, +/-12 dB.
    static float xForHz (double hz) noexcept;

    // A band that changes the sound (drawn, marked, listed): not bypassed and
    // not a 0 dB bell or shelf. The editor counts CHANGES with the same rule.
    static bool isActiveBand (const labq8::dsp::BandSettings&) noexcept;
    static float yForDb (double db) noexcept;

private:
    void contentChanged();
    void layoutRows();
    void clampScroll();
    void drawContent (juce::Graphics&) const;
    void drawStatus (juce::Graphics&) const;
    void drawCurve (juce::Graphics&) const;
    void drawBrightCurve (juce::Graphics&) const;
    void drawChat (juce::Graphics&) const;
    void drawDetail (juce::Graphics&) const;
    void drawListening (juce::Graphics&, float top) const;
    void buildEffects (int width, int height);
    void buildScreenImage();

    std::array<juce::String, 3> status;
    std::vector<Line> lines;
    std::vector<Row> rows;
    dsp::BandSet applied {};
    std::optional<dsp::BandSet> preview;
    std::vector<CurvePoint> appliedCurve, previewCurve;
    std::vector<Marker> markers;
    std::vector<DetailRow> detailRows;
    double listening = -1.0;
    bool detailVisible = false;
    int scrollRows = 0;        // rows scrolled back from the end
    float wheelRemainder = 0.0f;

    bool pendingRepaint = false;
    int repaintRequests = 0;

    // Cached layers at the device scale.
    juce::Image glass, backdrop, screenImage; // glass: opaque RGB; the others premultiplied ARGB
    std::vector<float> transmission;          // how much of the content shows through the effects
    bool contentDirty = true;
    LayerBuilds builds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhosphorScreen)
};
} // namespace iaq8::ui
