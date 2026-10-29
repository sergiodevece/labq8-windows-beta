#include "ui/LabQ8Editor.h"

#include "LabQ8Assets.h"
#include "dsp/FilterTypes.h"
#include "parameters/ParameterIDs.h"
#include "plugin/PluginProcessor.h"
#include "ui/UiGeometry.h"

#include <cmath>

namespace labq8::ui
{
namespace g = geometry;

namespace
{
    juce::Colour colourOf (const g::Rgb& c, float alpha = 1.0f) { return juce::Colour (c.r, c.g, c.b).withAlpha (alpha); }
    juce::Colour bandColour (int band, float alpha = 1.0f) { return colourOf (g::bandColours[(size_t) band], alpha); }

    juce::Rectangle<float> rectOf (const g::Rect& r) { return { r.x, r.y, r.w, r.h }; }

    // Dynamic text of the design. Helvetica Neue (a macOS system font),
    // heights and tracking measured so the ink matches the master: "820 Hz"
    // 54 x 13 px, "BAND 4" 76 x 15, "Bell" 23 x 12 (labq8_ui_snapshot --fonts).
    juce::Font designFont (float height, const char* style, float tracking = 0.0f)
    {
        return juce::Font (juce::FontOptions ("Helvetica Neue", height, 0).withStyle (style)).withExtraKerningFactor (tracking);
    }

    constexpr float displayFontHeight = 17.25f;     // Regular, cap 12
    constexpr float displayTracking = 0.081f;
    constexpr float headerFontHeight = 21.0f;       // Medium, cap 14
    constexpr float headerTracking = 0.124f;
    constexpr float filterTypeFontHeight = 15.75f;  // Regular, cap 11
    constexpr float bandNumberFontHeight = 17.25f;  // Regular, cap 12
    constexpr float nodeFontHeight = 11.75f;        // Bold, cap 8

    juce::Point<float> polar (juce::Point<float> centre, float radius, float degrees)
    {
        const auto a = juce::degreesToRadians (degrees);
        return { centre.x + radius * std::sin (a), centre.y - radius * std::cos (a) };
    }

    const char* parameterId (int band, PanelControl control)
    {
        const auto& ids = params::bandIDs[(size_t) band];
        const auto& dyn = params::dynamicIDs[(size_t) band];
        switch (control)
        {
            case PanelControl::frequency: return ids.frequency;
            case PanelControl::gain:      return ids.gain;
            case PanelControl::q:         return ids.q;
            case PanelControl::threshold: return dyn.threshold;
            case PanelControl::range:     return dyn.range;
            case PanelControl::attack:    return dyn.attack;
            case PanelControl::release:   return dyn.release;
        }
        return ids.frequency;
    }

    constexpr PanelControl controls[] = { PanelControl::frequency, PanelControl::gain, PanelControl::q, PanelControl::threshold,
                                          PanelControl::range, PanelControl::attack, PanelControl::release };
} // namespace

//==============================================================================
// Reference-space content: hosts every control (the editor paints the
// background behind it, pre-scaled).
class Content final : public juce::Component
{
public:
    Content()
    {
        setSize (g::referenceWidth, g::referenceHeight);
        setOpaque (false);
    }
};

//==============================================================================
// A metal knob of the design: cap and tick ring come from the background;
// this draws the green value arc and the pointer.
class KnobControl final : public juce::Slider
{
public:
    explicit KnobControl (int index) : knobIndex (index)
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setMouseDragSensitivity (240);
        setScrollWheelEnabled (true);
        const auto c = g::knobCentres[(size_t) index];
        setBounds (juce::Rectangle<float> (2 * g::knobHitRadius, 2 * g::knobHitRadius).withCentre ({ c.x, c.y }).toNearestInt());
    }

    bool hitTest (int x, int y) override
    {
        return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) x, (float) y }) <= g::knobHitRadius;
    }

    void paint (juce::Graphics& gr) override
    {
        const auto c = g::knobCentres[(size_t) knobIndex];
        const juce::Point<float> centre { c.x - (float) getX(), c.y - (float) getY() };
        const auto proportion = (float) juce::jlimit (0.0, 1.0, valueToProportionOfLength (getValue()));
        const auto start = juce::degreesToRadians (g::knobStartAngle);
        const auto end = juce::degreesToRadians (g::knobStartAngle + g::knobSweep * proportion);

        if (proportion > 0.001f)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, g::knobArcRadius, g::knobArcRadius, 0.0f, start, end, true);
            gr.setColour (juce::Colour (60, 200, 70).withAlpha (0.22f));
            gr.strokePath (arc, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            gr.setColour (juce::Colour (95, 215, 95).withAlpha (0.85f));
            gr.strokePath (arc, juce::PathStrokeType (3.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            gr.setColour (juce::Colour (195, 248, 182));
            gr.strokePath (arc, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const auto angle = g::knobStartAngle + g::knobSweep * proportion;
        juce::Line<float> pointer (polar (centre, g::pointerInnerRadius, angle), polar (centre, g::pointerOuterRadius, angle));
        gr.setColour (juce::Colour (6, 6, 6).withAlpha (0.92f));
        gr.drawLine (pointer, 2.8f);
    }

private:
    const int knobIndex;
};

//==============================================================================
// Numeric display under a knob: the real parameter value, design colours.
class ValueDisplay final : public juce::Component
{
public:
    ValueDisplay (int index, PanelControl c, juce::Slider& source) : control (c), knob (source)
    {
        const auto& span = g::displaySpans[(size_t) index];
        setBounds (juce::Rectangle<float> (span.left + 3, g::displayTop + 3, span.right - span.left - 6, g::displayBottom - g::displayTop - 6).toNearestInt());
        setInterceptsMouseClicks (false, false);
    }

    juce::String text() const { return formatValue (control, knob.getValue()); }

    void paint (juce::Graphics& gr) override
    {
        gr.setFont (designFont (displayFontHeight, "Regular", displayTracking));
        const auto area = getLocalBounds().toFloat().withCentre ({ (float) getWidth() * 0.5f, g::displayTextCentreY - (float) getY() - 0.5f });
        gr.setColour (colourOf (g::displayTextColour));
        gr.drawText (text(), area, juce::Justification::centred, false);
    }

private:
    const PanelControl control;
    juce::Slider& knob;
};

//==============================================================================
// One of the eight band selector buttons (the button body is background).
class BandButton final : public juce::Component
{
public:
    BandButton (int b, std::function<void (int)> onSelect) : band (b), select (std::move (onSelect))
    {
        const auto left = g::bandButtonLeft (b);
        setBounds (juce::Rectangle<float> (left, g::bandButtonTop, g::bandButtonWidth, g::bandButtonBottom - g::bandButtonTop + 1).toNearestInt());
        setTitle ("Band " + juce::String (b + 1));
    }

    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()))
            select (band);
    }

    void paint (juce::Graphics& gr) override
    {
        const auto left = g::bandButtonLeft (band) - (float) getX();
        const auto cy = g::bandButtonCentreY - (float) getY();
        const auto dot = juce::Rectangle<float> (2 * g::bandButtonDotRadius, 2 * g::bandButtonDotRadius).withCentre ({ left + g::bandButtonDotOffset, cy });
        gr.setColour (bandColour (band, 0.25f));
        gr.fillEllipse (dot.expanded (1.5f));
        gr.setColour (bandColour (band));
        gr.fillEllipse (dot);

        gr.setColour (colourOf (g::lightTextColour));
        gr.setFont (designFont (bandNumberFontHeight, "Regular"));
        gr.drawText (juce::String (band + 1), juce::Rectangle<float> (30, 20).withCentre ({ left + g::bandButtonNumberOffset, cy + 0.5f }),
                     juce::Justification::centred, false);

        if (selected)
        {
            // Selected: the design's border, in the band's colour.
            const auto box = getLocalBounds().toFloat().reduced (2.5f, 2.5f);
            gr.setColour (bandColour (band, 0.25f));
            gr.drawRoundedRectangle (box.expanded (1.0f), 7.0f, 3.0f);
            gr.setColour (bandColour (band).brighter (0.25f));
            gr.drawRoundedRectangle (box, 6.0f, 1.6f);
        }
    }

private:
    const int band;
    std::function<void (int)> select;
    bool selected = false;
};

//==============================================================================
// "BAND n" at the top left of the shared panel (its colour dot is the
// BypassIndicator).
class BandHeader final : public juce::Component
{
public:
    BandHeader()
    {
        setBounds (rectOf (g::bandLabelEraseArea).toNearestInt());
        setInterceptsMouseClicks (false, false);
    }

    void setBand (int b) { band = b; repaint(); }

    void paint (juce::Graphics& gr) override
    {
        const juce::Point<float> origin { (float) getX(), (float) getY() };
        gr.setColour (bandColour (band));
        gr.setFont (designFont (headerFontHeight, "Medium", headerTracking));
        const auto capCentre = 0.5f * (g::bandLabelCapTop + g::bandLabelCapBottom) - origin.y;
        gr.drawText ("BAND " + juce::String (band + 1), juce::Rectangle<float> (g::bandLabelLeft - origin.x, capCentre - 12, 120, 24),
                     juce::Justification::centredLeft, false);
    }

private:
    int band = 0;
};

//==============================================================================
// Filter Type selector: icon + name in the design's box, menu on click.
class FilterTypeSelector final : public juce::Component
{
public:
    FilterTypeSelector()
    {
        setBounds (rectOf (g::filterTypeBox).toNearestInt());
        setTitle ("Filter Type");
    }

    void attach (juce::RangedAudioParameter& parameter)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (parameter, [this] (float value)
        {
            index = (int) std::lround (value);
            repaint();
            if (onTypeChanged)
                onTypeChanged (index);
        });
        attachment->sendInitialUpdate();
    }

    std::function<void (int)> onTypeChanged;

    void choose (int newIndex)
    {
        if (attachment != nullptr)
            attachment->setValueAsCompleteGesture ((float) newIndex);
    }

    int getIndex() const noexcept { return index; }

    void mouseDown (const juce::MouseEvent&) override
    {
        juce::PopupMenu menu;
        for (int i = 0; i < (int) params::filterTypeNames.size(); ++i)
            menu.addItem (i + 1, params::filterTypeNames[(size_t) i], true, i == index);
        juce::Component::SafePointer<FilterTypeSelector> self (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [self] (int result) { if (self != nullptr && result > 0) self->choose (result - 1); });
    }

    void paint (juce::Graphics& gr) override
    {
        const juce::Point<float> origin { (float) getX(), (float) getY() };
        const auto icon = rectOf (g::filterTypeIcon) - origin;
        gr.setColour (colourOf (g::lightTextColour));
        gr.strokePath (iconPath (dsp::filterTypeFromIndex (index), icon), juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        gr.setFont (designFont (filterTypeFontHeight, "Regular"));
        const auto capCentre = 0.5f * (g::filterTypeCapTop + g::filterTypeCapBottom) - origin.y;
        gr.drawText (params::filterTypeNames[(size_t) index], juce::Rectangle<float> (g::filterTypeTextLeft - origin.x, capCentre - 11, 110, 22),
                     juce::Justification::centredLeft, false);
    }

private:
    // The master shows the Bell icon; the other five are drawn in the same
    // line style (not in the approved design: PENDING APPROVAL).
    static juce::Path iconPath (dsp::FilterType type, juce::Rectangle<float> r)
    {
        juce::Path p;
        const auto l = r.getX(), t = r.getY() + 1, w = r.getWidth(), h = r.getHeight() - 2, mid = t + h * 0.65f;
        switch (type)
        {
            case dsp::FilterType::bell:
                p.startNewSubPath (l, mid); p.cubicTo (l + w * 0.3f, mid, l + w * 0.35f, t, l + w * 0.5f, t);
                p.cubicTo (l + w * 0.65f, t, l + w * 0.7f, mid, l + w, mid); break;
            case dsp::FilterType::lowShelf:
                p.startNewSubPath (l, t); p.lineTo (l + w * 0.3f, t); p.cubicTo (l + w * 0.5f, t, l + w * 0.5f, mid, l + w * 0.7f, mid); p.lineTo (l + w, mid); break;
            case dsp::FilterType::highShelf:
                p.startNewSubPath (l, mid); p.lineTo (l + w * 0.3f, mid); p.cubicTo (l + w * 0.5f, mid, l + w * 0.5f, t, l + w * 0.7f, t); p.lineTo (l + w, t); break;
            case dsp::FilterType::lowCut:
                p.startNewSubPath (l + w * 0.1f, t + h); p.cubicTo (l + w * 0.3f, t, l + w * 0.4f, t + h * 0.3f, l + w, t + h * 0.3f); break;
            case dsp::FilterType::highCut:
                p.startNewSubPath (l, t + h * 0.3f); p.cubicTo (l + w * 0.6f, t + h * 0.3f, l + w * 0.7f, t, l + w * 0.9f, t + h); break;
            case dsp::FilterType::notch:
                p.startNewSubPath (l, t + h * 0.3f); p.lineTo (l + w * 0.4f, t + h * 0.3f); p.lineTo (l + w * 0.5f, t + h);
                p.lineTo (l + w * 0.6f, t + h * 0.3f); p.lineTo (l + w, t + h * 0.3f); break;
        }
        return p;
    }

    std::unique_ptr<juce::ParameterAttachment> attachment;
    int index = 0;
};

//==============================================================================
// Dynamic EQ switch: the pill of the master (on) or its off version. The
// dynamics only act on Bell bands (DSP, E2b): for the other types the switch
// is shown disabled (dimmed off pill, no clicks) and the stored b{n}_dyn_on
// is left untouched, so returning to Bell shows it again.
class DynamicSwitch final : public juce::Component, public juce::SettableTooltipClient
{
public:
    DynamicSwitch()
    {
        setBounds (rectOf (g::dynamicSwitchHit).toNearestInt());
        setTitle ("Dynamic EQ");
        onImage = juce::ImageCache::getFromMemory (LabQ8Assets::dynamic_switch_on_png, LabQ8Assets::dynamic_switch_on_pngSize);
        offImage = juce::ImageCache::getFromMemory (LabQ8Assets::dynamic_switch_off_png, LabQ8Assets::dynamic_switch_off_pngSize);
    }

    void attach (juce::RangedAudioParameter& parameter)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (parameter, [this] (float value) { on = value >= 0.5f; repaint(); });
        attachment->sendInitialUpdate();
    }

    void toggle()
    {
        if (attachment != nullptr && available)
            attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
    }

    void setAvailable (bool isAvailable)
    {
        if (isAvailable == available)
            return;
        available = isAvailable;
        setTooltip (available ? juce::String() : "Dynamic EQ: Bell bands only");
        repaint();
    }

    bool isOn() const noexcept { return on; }
    bool isAvailable() const noexcept { return available; }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()))
            toggle();
    }

    void paint (juce::Graphics& gr) override
    {
        const auto x = (int) (g::dynamicSwitchSprite.x - (float) getX()), y = (int) (g::dynamicSwitchSprite.y - (float) getY());
        if (! available)
        {
            gr.setOpacity (0.35f);
            gr.drawImageAt (offImage, x, y);
            return;
        }
        gr.drawImageAt (on ? onImage : offImage, x, y);
    }

private:
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::Image onImage, offImage;
    bool on = false, available = true;
};

//==============================================================================
// The colour dot next to "BAND n": lit when the band is active, dimmed when
// it is bypassed; a left click toggles b{n}_bypass of the shown band
// (approved 29-10-2026). Invisible click area of 34 x 34 px.
class BypassIndicator final : public juce::Component
{
public:
    BypassIndicator()
    {
        setBounds (juce::Rectangle<float> (g::bandDotHitSize, g::bandDotHitSize).withCentre ({ g::bandDotCentre.x, g::bandDotCentre.y }).toNearestInt());
        setTitle ("Band bypass");
    }

    void attach (juce::RangedAudioParameter& parameter, int newBand)
    {
        band = newBand;
        attachment = std::make_unique<juce::ParameterAttachment> (parameter, [this] (float value) { bypassed = value >= 0.5f; repaint(); });
        attachment->sendInitialUpdate();
    }

    void toggle()
    {
        if (attachment != nullptr)
            attachment->setValueAsCompleteGesture (bypassed ? 0.0f : 1.0f);
    }

    bool isBypassed() const noexcept { return bypassed; }

    void mouseUp (const juce::MouseEvent& e) override
    {
        // Left click only; the click area is the whole (invisible) 34 x 34.
        if (! e.mods.isPopupMenu() && getLocalBounds().contains (e.getPosition()))
            toggle();
    }

    void paint (juce::Graphics& gr) override
    {
        const auto dot = juce::Rectangle<float> (2 * g::bandDotRadius, 2 * g::bandDotRadius)
                             .withCentre ({ g::bandDotCentre.x - (float) getX(), g::bandDotCentre.y - (float) getY() });
        if (bypassed)
        {
            gr.setColour (bandColour (band, 0.18f));
            gr.fillEllipse (dot);
            gr.setColour (bandColour (band, 0.55f));
            gr.drawEllipse (dot.reduced (0.6f), 1.2f);
        }
        else
        {
            gr.setColour (bandColour (band));
            gr.fillEllipse (dot);
        }
    }

private:
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int band = 0;
    bool bypassed = false;
};

//==============================================================================
// Central screen: the static EQ response computed from the parameters with
// the DSP's own designs, and the eight band nodes. No spectrum, no dynamic
// curve, no meters: nothing that would pretend to be a measurement.
class ResponseScreen final : public juce::Component
{
public:
    ResponseScreen (juce::AudioProcessorValueTreeState& s, LabQ8AudioProcessor& p, std::function<void (int)> onSelect)
        : state (s), processor (p), select (std::move (onSelect))
    {
        setBounds (rectOf (g::plotArea).toNearestInt());
        for (float x = g::plotArea.x; x <= g::plotArea.x + g::plotArea.w; x += 1.5f)
            allFrequencies.push_back (xToFrequency (x));
    }

    void setSelectedBand (int b) { selectedBand = b; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto bands = readBands();
        const auto total = cascadeResponseDb (bands, sampleRate(), nodeFrequencies (bands));
        for (int b = 0; b < 8; ++b)
            if (nodePosition (bands[(size_t) b], total[(size_t) b]).getDistanceFrom (e.position + getPosition().toFloat()) <= 11.0f)
            {
                select (b);
                return;
            }
    }

    void paint (juce::Graphics& gr) override
    {
        gr.addTransform (juce::AffineTransform::translation (-(float) getX(), -(float) getY()));
        const auto plot = rectOf (g::plotArea);
        gr.reduceClipRegion (plot.toNearestInt());

        const auto bands = readBands();
        const auto fs = sampleRate();
        // The plot runs a little past 20 kHz; never draw at or above Nyquist.
        std::vector<double> frequencies;
        for (const auto f : allFrequencies)
            if (f < 0.497 * fs)
                frequencies.push_back (f);
        const auto total = cascadeResponseDb (bands, fs, frequencies);

        // Selected band's own contribution, filled against 0 dB.
        const auto own = bandResponseDb (bands[(size_t) selectedBand], fs, frequencies);
        juce::Path fill;
        fill.startNewSubPath (plot.getX(), decibelsToY (0));
        for (size_t i = 0; i < frequencies.size(); ++i)
            fill.lineTo (frequencyToX (frequencies[i]), clampY (decibelsToY (own[i])));
        fill.lineTo (frequencyToX (frequencies.back()), decibelsToY (0));
        fill.closeSubPath();
        gr.setColour (bandColour (selectedBand, 0.22f));
        gr.fillPath (fill);

        // Composite curve, each stretch in the colour of the nearest active band.
        auto colourAt = [&] (double f)
        {
            int best = -1;
            double distance = 1e9;
            for (int b = 0; b < 8; ++b)
                if (! bands[(size_t) b].bypassed)
                {
                    const auto d = std::abs (std::log (f / bands[(size_t) b].frequencyHz));
                    if (d < distance) { distance = d; best = b; }
                }
            return best < 0 ? juce::Colour (120, 225, 120) : bandColour (best);
        };
        for (const auto& [width, alpha] : { std::pair { 7.0f, 0.10f }, std::pair { 2.2f, 1.0f } })
            for (size_t i = 1; i < frequencies.size(); ++i)
            {
                gr.setColour (colourAt (frequencies[i]).withMultipliedAlpha (alpha));
                gr.drawLine (frequencyToX (frequencies[i - 1]), clampY (decibelsToY (total[i - 1])),
                             frequencyToX (frequencies[i]), clampY (decibelsToY (total[i])), width);
            }

        // Nodes on the curve.
        const auto atNodes = cascadeResponseDb (bands, fs, nodeFrequencies (bands));
        for (int b = 0; b < 8; ++b)
        {
            const auto centre = nodePosition (bands[(size_t) b], atNodes[(size_t) b]);
            const auto alpha = bands[(size_t) b].bypassed ? 0.35f : 1.0f; // bypass look (approved)
            const auto dot = juce::Rectangle<float> (15.0f, 15.0f).withCentre (centre);
            gr.setColour (bandColour (b, 0.25f * alpha));
            gr.fillEllipse (dot.expanded (2.5f));
            gr.setColour (bandColour (b, alpha));
            gr.fillEllipse (dot);
            gr.setColour (juce::Colour (12, 14, 12).withAlpha (0.8f * alpha));
            gr.drawEllipse (dot, 1.2f);
            gr.setFont (designFont (nodeFontHeight, "Bold"));
            gr.drawText (juce::String (b + 1), dot.translated (0, 0.5f), juce::Justification::centred, false);
        }
    }

    std::array<BandView, 8> readBands() const
    {
        std::array<BandView, 8> bands;
        for (int b = 0; b < 8; ++b)
        {
            const auto& ids = params::bandIDs[(size_t) b];
            auto value = [&] (const char* id) { return (double) state.getRawParameterValue (id)->load(); };
            bands[(size_t) b] = { value (ids.frequency), value (ids.gain), value (ids.q),
                                  dsp::filterTypeFromIndex ((int) std::lround (value (ids.type))), value (ids.bypass) >= 0.5 };
        }
        return bands;
    }

private:
    double sampleRate() const { return processor.getSampleRate() > 0 ? processor.getSampleRate() : 48000.0; }

    static std::vector<double> nodeFrequencies (const std::array<BandView, 8>& bands)
    {
        std::vector<double> f;
        for (const auto& b : bands)
            f.push_back (b.frequencyHz);
        return f;
    }

    static float clampY (float y) { return juce::jlimit (g::plotArea.y + 1.0f, g::plotArea.y + g::plotArea.h - 1.0f, y); }

    static juce::Point<float> nodePosition (const BandView& band, double db)
    {
        const auto x = juce::jlimit (g::plotArea.x + 8.0f, g::plotArea.x + g::plotArea.w - 8.0f, frequencyToX (band.frequencyHz));
        const auto y = juce::jlimit (g::plotArea.y + 9.0f, g::plotArea.y + g::plotArea.h - 9.0f, decibelsToY (db));
        return { x, y };
    }

    juce::AudioProcessorValueTreeState& state;
    LabQ8AudioProcessor& processor;
    std::function<void (int)> select;
    std::vector<double> allFrequencies;
    int selectedBand = 0;
};

//==============================================================================
// A control of the design without implementation: keeps its look, takes
// clicks without effect and explains itself in a tooltip.
class InactiveArea final : public juce::Component, public juce::SettableTooltipClient
{
public:
    InactiveArea (const juce::String& name, juce::Rectangle<int> area)
    {
        setBounds (area);
        setTitle (name);
        setTooltip (name + ": not available yet");
    }
};

//==============================================================================
LabQ8Editor::LabQ8Editor (LabQ8AudioProcessor& p)
    : AudioProcessorEditor (p), audioProcessor (p), state (p.getValueTreeState())
{
    content = std::make_unique<Content>();
    addAndMakeVisible (*content);

    screen = std::make_unique<ResponseScreen> (state, audioProcessor, [this] (int b) { selectBand (b); });
    content->addAndMakeVisible (*screen);

    for (int i = 0; i < 7; ++i)
    {
        knobs[(size_t) i] = std::make_unique<KnobControl> (i);
        content->addAndMakeVisible (*knobs[(size_t) i]);
        displays[(size_t) i] = std::make_unique<ValueDisplay> (i, controls[i], *knobs[(size_t) i]);
        content->addAndMakeVisible (*displays[(size_t) i]);
        knobs[(size_t) i]->onValueChange = [this, i] { displays[(size_t) i]->repaint(); };
    }

    for (int b = 0; b < 8; ++b)
    {
        bandButtons[(size_t) b] = std::make_unique<BandButton> (b, [this] (int band) { selectBand (band); });
        content->addAndMakeVisible (*bandButtons[(size_t) b]);
    }

    header = std::make_unique<BandHeader>();
    content->addAndMakeVisible (*header);
    bypassIndicator = std::make_unique<BypassIndicator>();
    content->addAndMakeVisible (*bypassIndicator);
    filterType = std::make_unique<FilterTypeSelector>();
    content->addAndMakeVisible (*filterType);
    dynamicSwitch = std::make_unique<DynamicSwitch>();
    content->addAndMakeVisible (*dynamicSwitch);
    filterType->onTypeChanged = [this] (int type) { dynamicSwitch->setAvailable (dsp::filterTypeFromIndex (type) == dsp::FilterType::bell); };

    // Present in the design, not implemented in UI-1.
    const std::pair<const char*, juce::Rectangle<int>> pending[] = {
        { "Stereo Mode", { 458, 80, 310, 58 } },     { "Polarity", { 816, 80, 98, 58 } },
        { "Intention", { 988, 80, 244, 58 } },       { "Display view", { 370, 198, 940, 36 } },
        { "Phase Behavior", { 1320, 619, 230, 43 } }, { "Level meters", { 1372, 176, 196, 338 } },
    };
    for (const auto& [name, area] : pending)
    {
        inactiveAreas.push_back (std::make_unique<InactiveArea> (name, area));
        content->addAndMakeVisible (*inactiveAreas.back());
    }

    selectBand (juce::jlimit (0, 7, audioProcessor.getUiSelectedBand()));

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) g::referenceWidth / g::referenceHeight);
    setResizeLimits ((int) (g::referenceWidth * minimumScale), (int) (g::referenceHeight * minimumScale), g::referenceWidth, g::referenceHeight);
    setSize ((int) std::lround (g::referenceWidth * defaultScale), (int) std::lround (g::referenceHeight * defaultScale));
    startTimerHz (30);
}

LabQ8Editor::~LabQ8Editor()
{
    stopTimer();
    for (auto& a : knobAttachments)
        a.reset();
}

void LabQ8Editor::paint (juce::Graphics& gr)
{
    // The background is resampled once per size and display density, then
    // only copied: repaints never rescale the 1672 x 941 picture.
    const auto density = juce::jmax (1.0f, (float) gr.getInternalContext().getPhysicalPixelScaleFactor());
    const auto w = juce::roundToInt ((float) getWidth() * density), h = juce::roundToInt ((float) getHeight() * density);
    if (scaledBackground.getWidth() != w || scaledBackground.getHeight() != h)
    {
        const auto source = juce::ImageCache::getFromMemory (LabQ8Assets::background_png, LabQ8Assets::background_pngSize);
        scaledBackground = source.rescaled (w, h, juce::Graphics::highResamplingQuality);
    }
    gr.drawImage (scaledBackground, getLocalBounds().toFloat());
}

void LabQ8Editor::resized()
{
    content->setTransform (juce::AffineTransform::scale (getScale()));
}

float LabQ8Editor::getScale() const noexcept
{
    return (float) getWidth() / (float) g::referenceWidth;
}

void LabQ8Editor::selectBand (int band)
{
    selectedBand = juce::jlimit (0, 7, band);
    audioProcessor.setUiSelectedBand (selectedBand);
    attachPanel();
    for (int b = 0; b < 8; ++b)
        bandButtons[(size_t) b]->setSelected (b == selectedBand);
    header->setBand (selectedBand);
    screen->setSelectedBand (selectedBand);
}

void LabQ8Editor::attachPanel()
{
    // Re-point the shared panel at the selected band. The attachments read
    // the parameters; creating or destroying them never writes one.
    for (int i = 0; i < 7; ++i)
    {
        knobAttachments[(size_t) i].reset();
        const auto* id = parameterId (selectedBand, controls[i]);
        knobAttachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, *knobs[(size_t) i]);
        if (auto* parameter = state.getParameter (id))
            knobs[(size_t) i]->setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        displays[(size_t) i]->repaint();
    }
    dynamicSwitch->attach (*state.getParameter (params::dynamicIDs[(size_t) selectedBand].enabled));
    filterType->attach (*state.getParameter (params::bandIDs[(size_t) selectedBand].type)); // also sets the switch availability
    bypassIndicator->attach (*state.getParameter (params::bandIDs[(size_t) selectedBand].bypass), selectedBand);
}

void LabQ8Editor::timerCallback()
{
    // Host automation of any band moves the curve and nodes.
    const auto snapshot = parameterSnapshot();
    if (snapshot != lastSnapshot)
    {
        lastSnapshot = snapshot;
        screen->repaint();
    }
}

std::vector<float> LabQ8Editor::parameterSnapshot() const
{
    // The 40 static parameters (the screen depends on nothing else).
    std::vector<float> values;
    values.reserve (40);
    for (const auto& ids : params::bandIDs)
        for (const auto* id : { ids.frequency, ids.gain, ids.q, ids.bypass, ids.type })
            values.push_back (state.getRawParameterValue (id)->load());
    return values;
}

juce::Slider& LabQ8Editor::getKnob (PanelControl control) { return *knobs[(size_t) control]; }
juce::Component& LabQ8Editor::getBandButton (int band) { return *bandButtons[(size_t) juce::jlimit (0, 7, band)]; }
void LabQ8Editor::setFilterTypeIndex (int index) { filterType->choose (index); }
void LabQ8Editor::toggleDynamicSwitch() { dynamicSwitch->toggle(); }
bool LabQ8Editor::isDynamicSwitchAvailable() const { return dynamicSwitch->isAvailable(); }
bool LabQ8Editor::isDynamicSwitchOn() const { return dynamicSwitch->isOn(); }
juce::Component& LabQ8Editor::getBypassIndicator() { return *bypassIndicator; }
juce::Component& LabQ8Editor::getDynamicSwitch() { return *dynamicSwitch; }
bool LabQ8Editor::isBypassShown() const { return bypassIndicator->isBypassed(); }
juce::String LabQ8Editor::getDisplayText (PanelControl control) const { return displays[(size_t) control]->text(); }
} // namespace labq8::ui
