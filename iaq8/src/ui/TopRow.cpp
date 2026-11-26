#include "ui/TopRow.h"

#include "LabQ8Assets.h"
#include "assist/Session.h"
#include "ui/Iaq8Geometry.h"

namespace iaq8::ui
{
namespace g = geometry;

namespace
{
juce::String utf8 (const char* text)
{
    return juce::String (juce::CharPointer_UTF8 (text));
}

// The sources in the Session's order (its quick replies and sourceLabel).
constexpr std::array<assist::Source, 7> kSources { assist::Source::voice, assist::Source::acousticGuitar,
                                                   assist::Source::electricGuitar, assist::Source::bass,
                                                   assist::Source::drums, assist::Source::mix, assist::Source::other };

// The row in reference coordinates: the two trays, the lamps and their glow.
const juce::Rectangle<float> rowArea { 440.0f, 66.0f, 920.0f, 84.0f };

// A recessed tray (.tray): radius 7, #121311 -> #1b1c19, inset 0 2px 5px
// black .95 and a 1 px black ring (.8), 0 1px 0 white .1 below.
void drawTray (juce::Graphics& gr, juce::Rectangle<float> b)
{
    constexpr float radius = 7.0f;
    gr.setColour (juce::Colours::white.withAlpha (0.1f));
    gr.fillRoundedRectangle (b.translated (0.0f, 1.0f), radius);
    juce::Path shape;
    shape.addRoundedRectangle (b, radius);
    gr.setGradientFill (juce::ColourGradient (colours::trayTop, 0.0f, b.getY(), colours::trayBottom, 0.0f, b.getBottom(), false));
    gr.fillPath (shape);

    gr.saveState();
    gr.reduceClipRegion (shape);
    juce::Path outside;
    outside.addRectangle (b.expanded (12.0f));
    outside.addRoundedRectangle (b.translated (0.0f, 2.0f), radius);
    outside.setUsingNonZeroWinding (false);
    juce::DropShadow (juce::Colours::black.withAlpha (0.95f), 5, {}).drawForPath (gr, outside);
    gr.restoreState();
    gr.setColour (juce::Colours::black.withAlpha (0.8f));
    gr.drawRoundedRectangle (b.reduced (0.5f), radius, 1.0f);
}
} // namespace

//==============================================================================
// A COMPARE face (.face): LABQ8's lit / unlit mode sprite stretched over the
// face, 17 px Barlow Semi Condensed SemiBold (letter spacing 0.6 px), dark
// green on the lit face, light grey on the unlit one. A disabled face is
// dimmed and takes no clicks; a right-click never switches.
class TopRow::Face final : public juce::Button
{
public:
    explicit Face (const juce::String& text) : juce::Button (text)
    {
        setButtonText (text);
        setWantsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setLit (bool shouldBeLit)
    {
        if (lit == shouldBeLit)
            return;
        lit = shouldBeLit;
        repaint();
    }

    bool isLit() const noexcept { return lit; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            juce::Button::mouseDown (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            juce::Button::mouseUp (e);
    }

    void paintButton (juce::Graphics& gr, bool highlighted, bool down) override
    {
        const auto b = getLocalBounds().toFloat();
        if (! isEnabled())
            gr.setOpacity (0.4f);
        const auto sprite = lit ? juce::ImageCache::getFromMemory (LabQ8Assets::mode_face_lit_png, LabQ8Assets::mode_face_lit_pngSize)
                                : juce::ImageCache::getFromMemory (LabQ8Assets::mode_face_unlit_png, LabQ8Assets::mode_face_unlit_pngSize);
        gr.drawImage (sprite, b, juce::RectanglePlacement::stretchToFit);
        if (isEnabled() && (highlighted || down) && ! lit)
        {
            gr.setColour (juce::Colours::white.withAlpha (down ? 0.02f : 0.05f));
            gr.fillRoundedRectangle (b.reduced (2.0f), 4.0f);
        }

        auto font = Iaq8LookAndFeel::keyFont (17.0f);
        font = font.withExtraKerningFactor (0.6f / font.getHeight());
        const auto text = getButtonText();
        const auto x = b.getCentreX() - juce::GlyphArrangement::getStringWidth (font, text) * 0.5f;
        const auto baseline = b.getCentreY() - font.getHeight() * 0.5f + font.getAscent();
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, text, x, baseline);
        if (lit)
        {
            // text-shadow: 0 1px 0 rgba(255,255,255,.35).
            gr.setColour (juce::Colours::white.withAlpha (isEnabled() ? 0.35f : 0.14f));
            juce::GlyphArrangement shadow (glyphs);
            shadow.moveRangeOfGlyphs (0, -1, 0.0f, 1.0f);
            shadow.draw (gr);
        }
        gr.setColour ((lit ? colours::faceLitText : colours::faceUnlitText).withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
        glyphs.draw (gr);
    }

private:
    bool lit = false;
};

//==============================================================================
// A STATUS lamp: the 17 px dot and room for its glow. Its two looks are
// cached; a change repaints only this small component.
class TopRow::StatusLamp final : public juce::Component
{
public:
    static constexpr float glowRoom = 24.0f;

    explicit StatusLamp (Iaq8LookAndFeel::LampColour c) : colour (c) { setInterceptsMouseClicks (false, false); }

    void setOn (bool shouldBeOn)
    {
        if (on == shouldBeOn)
            return;
        on = shouldBeOn;
        repaint();
    }

    bool isOn() const noexcept { return on; }

    void paint (juce::Graphics& gr) override
    {
        const auto area = getLocalBounds().toFloat();
        const auto dot = area.reduced (glowRoom);
        (on ? onLayer : offLayer).draw (gr, area, [this, dot] (juce::Graphics& ig)
                                        { Iaq8LookAndFeel::drawLamp (ig, dot, on, colour); });
    }

private:
    const Iaq8LookAndFeel::LampColour colour;
    bool on = false;
    CachedLayer onLayer, offLayer;
};

//==============================================================================
TopRow::TopRow()
{
    setBounds (rowArea.toNearestInt());
    setInterceptsMouseClicks (false, true);

    for (const auto s : kSources)
        combo.addItem (utf8 (assist::Session::sourceLabel (s)), comboIdFor (s));
    combo.setTextWhenNothingSelected (noSourceText());
    combo.setTextWhenNoChoicesAvailable (noSourceText());
    combo.setTitle ("Fuente");
    combo.setTooltip ("Fuente: lo que hay en esta pista");
    combo.setWantsKeyboardFocus (false);
    combo.setBounds (local (g::sourceDrop).toNearestInt());
    combo.onChange = [this]
    {
        if (const auto s = sourceForComboId (combo.getSelectedId()); s.has_value() && onSourceChosen)
            onSourceChosen (*s);
    };
    addAndMakeVisible (combo);

    const struct
    {
        dsp::Listen listen;
        juce::String text;
        g::Rect area;
        const char* tooltip;
    } faceSpecs[] = { { dsp::Listen::applied, "ANTES", g::faceAntes, "Escucha tu EQ aplicada" },
                      { dsp::Listen::preview, utf8 ("DESPU\xc3\x89S"), g::faceDespues, "Escucha la propuesta" } };
    for (const auto& spec : faceSpecs)
    {
        auto face = std::make_unique<Face> (spec.text);
        face->setBounds (local (spec.area).toNearestInt());
        face->setTooltip (spec.tooltip);
        const auto listen = spec.listen;
        face->onClick = [this, listen]
        {
            if (onListenChosen)
                onListenChosen (listen);
        };
        addAndMakeVisible (*face);
        faces[(size_t) spec.listen] = std::move (face);
    }

    const Iaq8LookAndFeel::LampColour lampColours[] = { Iaq8LookAndFeel::LampColour::green, Iaq8LookAndFeel::LampColour::green,
                                                        Iaq8LookAndFeel::LampColour::orange };
    for (size_t i = 0; i < lamps.size(); ++i)
    {
        lamps[i] = std::make_unique<StatusLamp> (lampColours[i]);
        lamps[i]->setBounds (local (g::lamps[i].expanded (StatusLamp::glowRoom)).toNearestInt());
        addAndMakeVisible (*lamps[i]);
    }

    // The controls start as the default state shows it.
    faces[(size_t) dsp::Listen::applied]->setLit (true);
    faces[(size_t) dsp::Listen::preview]->setEnabled (false);
}

TopRow::~TopRow() = default;

juce::Rectangle<float> TopRow::local (juce::Rectangle<float> reference) const
{
    return reference.translated (-rowArea.getX(), -rowArea.getY());
}

int TopRow::comboIdFor (assist::Source s)
{
    for (size_t i = 0; i < kSources.size(); ++i)
        if (kSources[i] == s)
            return (int) i + 1;
    return 0;
}

std::optional<assist::Source> TopRow::sourceForComboId (int id)
{
    if (id < 1 || id > (int) kSources.size())
        return std::nullopt;
    return kSources[(size_t) id - 1];
}

juce::String TopRow::noSourceText()
{
    return utf8 ("Elige la fuente\xe2\x80\xa6");
}

void TopRow::setState (const State& newState)
{
    if (newState == state)
        return;
    state = newState;

    // Shown, never chosen again: no notification.
    const auto id = state.source.has_value() ? comboIdFor (*state.source) : 0;
    if (combo.getSelectedId() != id)
        combo.setSelectedId (id, juce::dontSendNotification);

    auto& antes = *faces[(size_t) dsp::Listen::applied];
    auto& despues = *faces[(size_t) dsp::Listen::preview];
    despues.setEnabled (state.canPreview);
    const auto preview = state.listen == dsp::Listen::preview && state.canPreview;
    antes.setLit (! preview);
    despues.setLit (preview);

    lamps[(size_t) Lamp::online]->setOn (state.online);
    lamps[(size_t) Lamp::listen]->setOn (state.listening);
    lamps[(size_t) Lamp::proposal]->setOn (state.proposal);
}

void TopRow::paint (juce::Graphics& gr)
{
    trays.draw (gr, getLocalBounds().toFloat(), [this] (juce::Graphics& ig)
    {
        drawTray (ig, local (g::sourceTray));
        drawTray (ig, local (g::compareTray));
        // The drop-down's own shadow (0 1px 2px black .6) under it.
        juce::Path drop;
        drop.addRoundedRectangle (local (g::sourceDrop).translated (0.0f, 1.0f), 4.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 2, {}).drawForPath (ig, drop);
    });
}

juce::Button& TopRow::getFace (dsp::Listen listen)
{
    return *faces[(size_t) listen];
}

bool TopRow::isFaceLit (dsp::Listen listen) const
{
    return faces[(size_t) listen]->isLit();
}

bool TopRow::isLampOn (Lamp lamp) const
{
    return lamps[(size_t) lamp]->isOn();
}
} // namespace iaq8::ui
