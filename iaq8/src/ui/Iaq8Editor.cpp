#include "ui/Iaq8Editor.h"

#include "LabQ8Assets.h"
#include "assist/Session.h"
#include "assist/Settings.h"
#include "plugin/Iaq8Processor.h"
#include "ui/Iaq8Geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#ifndef IAQ8_PLUGIN_VERSION
 #error "IAQ8_PLUGIN_VERSION must be defined by CMake"
#endif

namespace iaq8::ui
{
namespace g = geometry;

namespace
{
juce::String utf8 (const char* text)
{
    return juce::String (juce::CharPointer_UTF8 (text));
}

// The ASSISTANT head (.head: flex, centred, gap 14): the 17 px orange lamp,
// then the title, whose 22 px line box is the taller of the two.
constexpr float headLampSize = 17.0f, headGap = 14.0f;

juce::Rectangle<float> assistantTitleArea()
{
    return g::assistantHead.withTrimmedLeft (headLampSize + headGap).withHeight (22.0f * 1.2f);
}

juce::Rectangle<float> assistantLampArea()
{
    const auto lineBox = 22.0f * 1.2f;
    return { g::assistantHead.getX(), g::assistantHead.getY() + (lineBox - headLampSize) * 0.5f, headLampSize, headLampSize };
}

// The worn silkscreen printed by the editor (the rest of the chassis print
// is in iaq8_chassis.png). Sizes, weights and letter spacing from the
// mockup's .lbl, .lampcap and .desc1-3 styles.
std::vector<SilkText> silkscreen()
{
    const auto label = Iaq8LookAndFeel::labelFont (16.0f, false);      // .lbl: 16 px, 500, 1.6 px
    const auto caption = Iaq8LookAndFeel::labelFont (11.0f, true);     // .lampcap: 11 px, 600, 1.4 px
    std::vector<SilkText> items {
        { "SOURCE", g::sourceLabel, label, 1.6f },
        { "COMPARE", g::compareLabel, label, 1.6f },
        { "STATUS", g::statusLabel, label, 1.6f },
        { "AI ASSISTED\nEQUALIZER", g::desc1, Iaq8LookAndFeel::labelFont (16.5f, false), 3.6f, 16.5f * 1.38f },
        { utf8 ("DESCRIBE \xc2\xb7 LISTEN \xc2\xb7 APPROVE"), g::desc2, Iaq8LookAndFeel::labelFont (11.5f, true), 1.0f },
        { utf8 ("INTELLIGENT \xc2\xb7 MUSICAL \xc2\xb7 ANALOG SOUL"), g::desc3, Iaq8LookAndFeel::labelFont (10.5f, true), 0.4f },
    };
    const char* captions[] = { "ONLINE", "LISTEN", "PROPOSAL" };
    for (size_t i = 0; i < g::lampCaps.size(); ++i)
        items.push_back ({ captions[i], g::lampCaps[i], caption, 1.4f, 0.0f, juce::Justification::horizontallyCentred });

    // Lower panel (.head .t, the note after it, .hint and .vlab; the hint
    // and the box labels are plain print in the mockup, not worn).
    items.push_back ({ "ASSISTANT", assistantTitleArea(), Iaq8LookAndFeel::labelFont (22.0f, true), 2.4f, 0.0f,
                       juce::Justification::left, colours::assistantTitle });
    items.push_back ({ utf8 ("P\xc3\xad" "delo con tus palabras"), g::assistantNote, Iaq8LookAndFeel::labelFont (16.0f, false), 0.0f, 0.0f,
                       juce::Justification::left, colours::assistantNote });
    items.push_back ({ utf8 ("El audio nunca sale de tu ordenador \xc2\xb7 solo se env\xc3\xad" "a un resumen del an\xc3\xa1lisis"),
                       g::hint, Iaq8LookAndFeel::labelFont (13.0f, false), 0.4f, 0.0f, juce::Justification::left, colours::hint, false });
    const char* boxLabels[] = { "SOURCE", "LISTENED", "CHANGES", "LEVEL MATCH" };
    for (size_t i = 0; i < g::valueLabels.size(); ++i)
        items.push_back ({ boxLabels[i], g::valueLabels[i], Iaq8LookAndFeel::labelFont (13.0f, false), 1.2f, 0.0f,
                           juce::Justification::horizontallyCentred, colours::valueLabel, false });
    return items;
}

//==============================================================================
// The reference space (1672 x 941), scaled by the editor. Paints the worn
// silkscreen from images rendered once per device scale.
class Content final : public juce::Component
{
public:
    Content() : items (silkscreen())
    {
        setSize (g::referenceWidth, g::referenceHeight);
        setOpaque (false);
    }

    void paint (juce::Graphics& gr) override
    {
        // Device pixels per reference pixel: the zoom times the display density.
        const auto scale = juce::jmax (0.1f, gr.getInternalContext().getPhysicalPixelScaleFactor());
        if (std::abs (scale - cachedScale) > 0.001f)
        {
            worn.clear();
            for (const auto& item : items)
                worn.push_back (Iaq8LookAndFeel::renderWornText (item, scale));
            cachedScale = scale;
        }

        for (const auto& w : worn)
            gr.drawImage (w.image, w.area);

        // The ASSISTANT head's lamp (with its glow) and rule.
        const auto lamp = assistantLampArea();
        lampLayer.draw (gr, lamp.expanded (24.0f), [lamp] (juce::Graphics& ig)
                        { Iaq8LookAndFeel::drawLamp (ig, lamp, true, Iaq8LookAndFeel::LampColour::orange); });
        gr.setColour (colours::rule);
        gr.fillRect (g::assistantRule);
    }

private:
    CachedLayer lampLayer;
    std::vector<SilkText> items;
    std::vector<Iaq8LookAndFeel::WornText> worn;
    float cachedScale = 0.0f;
};

// An invisible button over something printed on the chassis.
class HotSpot final : public juce::Button
{
public:
    explicit HotSpot (const juce::String& name) : juce::Button (name)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (false);
        setTooltip (name);
    }

    void paintButton (juce::Graphics&, bool, bool) override {}
};

juce::Rectangle<int> screenAreaOverride;
} // namespace

//==============================================================================
class Iaq8Editor::PopupMenuForwarder final : public juce::MouseListener
{
public:
    explicit PopupMenuForwarder (Iaq8Editor& e) : editor (e) {}

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            return; // left clicks stay with the component that got them
        for (auto* c = e.eventComponent; c != nullptr && c != editor.content.get(); c = c->getParentComponent())
            if (c->getProperties()[ownsPopupMenuProperty])
                return;
        editor.showSettingsMenu (*editor.content);
    }

private:
    Iaq8Editor& editor;
};

//==============================================================================
// The command line (.inbox): a recessed phosphor field, "> " before the text
// and "[ENTER]" at the right, VT323 29 px. The text is a borderless
// TextEditor on a cached background; scan lines and the reflection are a
// cached overlay above it that takes no clicks. The TextEditor keeps its own
// right-click menu (copy, paste).
class Iaq8Editor::CommandLine final : public juce::Component
{
public:
    static constexpr float padding = 22.0f, fontPx = 29.0f, enterFontPx = 20.0f;

    CommandLine()
    {

        field.setMultiLine (false);
        field.setReturnKeyStartsNewLine (false);
        field.setScrollbarsShown (false);
        field.setInputRestrictions (assist::Session::maxTextChars);
        field.setBorder ({});
        field.setIndents (0, 0);
        field.setJustification (juce::Justification::centredLeft);
        field.setFont (font());
        field.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        field.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        field.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        field.setColour (juce::TextEditor::textColourId, colours::phosphor);
        field.setColour (juce::CaretComponent::caretColourId, colours::cursor);
        field.getProperties().set (ownsPopupMenuProperty, true);
        field.setTitle ("Mensaje al asistente");
        addAndMakeVisible (field);

        overlay.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (overlay);
    }

    static juce::Font font() { return Iaq8LookAndFeel::screenFont (fontPx); }
    static juce::Font enterFont()
    {
        const auto f = Iaq8LookAndFeel::screenFont (enterFontPx);
        return f.withExtraKerningFactor (1.0f / f.getHeight());
    }

    // The box itself (the component is 1 px taller for the light under it).
    juce::Rectangle<float> box() const { return getLocalBounds().toFloat().withTrimmedBottom (1.0f); }

    void resized() override
    {
        const auto b = box();
        const auto prefix = juce::GlyphArrangement::getStringWidth (font(), "> ");
        const auto enter = juce::GlyphArrangement::getStringWidth (enterFont(), "[ENTER]");
        field.setBounds (b.withTrimmedLeft (padding + prefix).withTrimmedRight (padding + enter + 16.0f).toNearestInt());
        overlay.setBounds (getLocalBounds());
    }

    void paint (juce::Graphics& gr) override
    {
        background.draw (gr, getLocalBounds().toFloat(), [this] (juce::Graphics& ig) { drawBackground (ig); });
    }

    // A click anywhere in the box (the "> " included) puts the cursor in the line.
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            field.grabKeyboardFocus();
    }

    juce::TextEditor field;

private:
    void drawBackground (juce::Graphics& gr) const
    {
        const auto b = box();
        const auto radius = 8.0f;
        // 0 1px 0 rgba(255,255,255,.08) under the box.
        gr.setColour (juce::Colours::white.withAlpha (0.08f));
        gr.fillRoundedRectangle (b.translated (0.0f, 1.0f), radius);

        juce::Path shape;
        shape.addRoundedRectangle (b, radius);
        juce::ColourGradient glass (juce::Colour (0xff0b1f0e), b.getCentre(), juce::Colour (0xff020703),
                                    b.getCentre() + juce::Point<float> (b.getWidth() * 0.5f * 0.8f * 1.41f, 0.0f), true);
        gr.setGradientFill (glass);
        gr.fillPath (shape);

        // Inset shadows: 1 px black ring (.9) and 0 3px 9px (.95), mostly at the top.
        gr.saveState();
        gr.reduceClipRegion (shape);
        juce::Path outside;
        outside.addRectangle (b.expanded (20.0f));
        outside.addRoundedRectangle (b.translated (0.0f, 3.0f), radius);
        outside.setUsingNonZeroWinding (false);
        juce::DropShadow (juce::Colours::black.withAlpha (0.95f), 9, {}).drawForPath (gr, outside);
        gr.restoreState();
        gr.setColour (juce::Colours::black.withAlpha (0.9f));
        gr.drawRoundedRectangle (b.reduced (0.5f), radius, 1.0f);

        // "> " and "[ENTER]".
        const auto f = font();
        gr.setColour (colours::phosphor);
        juce::GlyphArrangement prefix;
        prefix.addLineOfText (f, "> ", b.getX() + padding, b.getCentreY() - f.getHeight() * 0.5f + f.getAscent());
        prefix.draw (gr);
        const auto ef = enterFont();
        const auto enterWidth = juce::GlyphArrangement::getStringWidth (ef, "[ENTER]");
        gr.setColour (colours::phosphorPlaceholder);
        juce::GlyphArrangement enter;
        enter.addLineOfText (ef, "[ENTER]", b.getRight() - padding - enterWidth, b.getCentreY() - ef.getHeight() * 0.5f + ef.getAscent());
        enter.draw (gr);
    }

    // Scan lines (1 px black .35 of every 3) and the 172deg reflection.
    class Overlay final : public juce::Component
    {
    public:
        void paint (juce::Graphics& gr) override
        {
            layer.draw (gr, getLocalBounds().toFloat(), [this] (juce::Graphics& ig)
            {
                const auto b = getLocalBounds().toFloat().withTrimmedBottom (1.0f);
                juce::Path shape;
                shape.addRoundedRectangle (b, 8.0f);
                ig.reduceClipRegion (shape);
                ig.setColour (juce::Colours::black.withAlpha (0.35f));
                for (auto y = b.getY(); y < b.getBottom(); y += 3.0f)
                    ig.fillRect (b.withY (y).withHeight (1.0f));
                ig.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), b.getX(), b.getY(),
                                                          juce::Colours::transparentWhite, b.getX() + b.getWidth() * 0.4f * 0.14f,
                                                          b.getY() + b.getHeight() * 0.4f, false));
                ig.fillRect (b);
            });
        }

    private:
        CachedLayer layer;
    };

    CachedLayer background;
    Overlay overlay;
};

//==============================================================================
// A value box of the lower panel (.vbox): recessed, 17 px Barlow SemiBold in
// green with a soft glow. Repaints only when its text changes.
class Iaq8Editor::ValueBox final : public juce::Component
{
public:
    ValueBox() { setInterceptsMouseClicks (false, false); }

    void setText (const juce::String& newText)
    {
        if (newText == text)
            return;
        text = newText;
        repaint();
    }

    const juce::String& getText() const noexcept { return text; }

    void paint (juce::Graphics& gr) override
    {
        const auto b = getLocalBounds().toFloat().withTrimmedBottom (1.0f);
        background.draw (gr, getLocalBounds().toFloat(), [b] (juce::Graphics& ig)
        {
            ig.setColour (juce::Colours::white.withAlpha (0.07f)); // 0 1px 0 below
            ig.fillRoundedRectangle (b.translated (0.0f, 1.0f), 5.0f);
            ig.setColour (colours::valueBox);
            ig.fillRoundedRectangle (b, 5.0f);
            juce::Path shape, outside;
            shape.addRoundedRectangle (b, 5.0f);
            ig.reduceClipRegion (shape);
            outside.addRectangle (b.expanded (12.0f));
            outside.addRoundedRectangle (b.translated (0.0f, 2.0f), 5.0f);
            outside.setUsingNonZeroWinding (false);
            juce::DropShadow (juce::Colours::black.withAlpha (0.95f), 5, {}).drawForPath (ig, outside);
        });

        if (text.isEmpty())
            return;
        auto font = Iaq8LookAndFeel::labelFont (17.0f, true);
        font = font.withExtraKerningFactor (0.8f / font.getHeight());
        const auto width = juce::GlyphArrangement::getStringWidth (font, text);
        if (width > b.getWidth() - 12.0f)
            font = font.withHeight (font.getHeight() * (b.getWidth() - 12.0f) / width);
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, text, b.getCentreX() - juce::GlyphArrangement::getStringWidth (font, text) * 0.5f,
                              b.getCentreY() - font.getHeight() * 0.5f + font.getAscent());
        juce::Path path;
        glyphs.createPath (path);
        juce::DropShadow (juce::Colour (0xff78ff64).withAlpha (0.35f), 6, {}).drawForPath (gr, path);
        gr.setColour (colours::valueText);
        gr.fillPath (path);
    }

private:
    juce::String text;
    CachedLayer background;
};

juce::MouseListener& Iaq8Editor::getPopupMenuListener() noexcept
{
    return *popupForwarder;
}

//==============================================================================
Iaq8Editor::Iaq8Editor (Iaq8AudioProcessor& p)
    : AudioProcessorEditor (p), iaq8Processor (p)
{
    setLookAndFeel (&lookAndFeel);

    content = std::make_unique<Content>();
    addAndMakeVisible (*content);

    // A right-click anywhere on the chassis opens the settings menu: one
    // listener on the content and every component inside it (present or
    // added later), so children such as the screen, which take the clicks
    // for themselves, need no wiring. Children with a menu of their own set
    // ownsPopupMenuProperty.
    popupForwarder = std::make_unique<PopupMenuForwarder> (*this);
    content->addMouseListener (popupForwarder.get(), true);

    const g::Rect spots[] = { g::settingsScrew, g::logo };
    for (size_t i = 0; i < hotSpots.size(); ++i)
    {
        auto spot = std::make_unique<HotSpot> ("Ajustes");
        spot->getProperties().set (ownsPopupMenuProperty, true); // any click opens the menu already
        spot->setBounds (spots[i].toNearestInt());
        auto* target = spot.get();
        spot->onClick = [this, target] { showSettingsMenu (*target); };
        content->addAndMakeVisible (*spot);
        hotSpots[i] = std::move (spot);
    }

    // The phosphor screen covers the CRT. Bound to the session in Task 7;
    // until then it shows only the system name.
    screen = std::make_unique<PhosphorScreen>();
    screen->setBounds (g::crt.toNearestInt());
    screen->setStatus ("IAQ8 1.0", {}, {});
    content->addAndMakeVisible (*screen);

    // The IN / OUT meters show the processor's real sample peaks; the view
    // places itself in reference coordinates (LABQ8's meter window).
    meters = std::make_unique<labq8::ui::LevelMeterView> (iaq8Processor.getInputMeter(), iaq8Processor.getOutputMeter());
    content->addAndMakeVisible (*meters);

    createAssistantControls();
    createTopRow();

    // No resize corner and no resizing by the host: the size only changes
    // through the zoom menu. Opens at the stored zoom, or at the largest
    // smaller choice that fits this screen (the preference is kept).
    setResizable (false, false);
    auto zoom = preferences::loadZoom();
    for (auto i = (size_t) juce::jmax (0, preferences::zoomChoiceIndex (zoom)); i > 0 && ! zoomFits (zoom, getAvailableScreenArea()); --i)
        zoom = zoomChoices[i - 1];
    setSize ((int) std::lround ((float) g::referenceWidth * zoom), (int) std::lround ((float) g::referenceHeight * zoom));

    refreshFromSession();
}

Iaq8Editor::~Iaq8Editor()
{
    content->removeMouseListener (popupForwarder.get());
    // An open dialog closes with the editor (its callback then finds no
    // editor and saves nothing).
    dialog.reset();
    onDialogClosed = nullptr;
    setLookAndFeel (nullptr);
}

void Iaq8Editor::paint (juce::Graphics& gr)
{
    // The chassis is resampled once per size and display density, then only
    // copied: repaints never rescale the 1672 x 941 picture.
    const auto density = juce::jmax (1.0f, (float) gr.getInternalContext().getPhysicalPixelScaleFactor());
    const auto w = juce::roundToInt ((float) getWidth() * density), h = juce::roundToInt ((float) getHeight() * density);
    if (scaledBackground.getWidth() != w || scaledBackground.getHeight() != h)
    {
        const auto source = juce::ImageCache::getFromMemory (LabQ8Assets::iaq8_chassis_png, LabQ8Assets::iaq8_chassis_pngSize);
        scaledBackground = source.rescaled (w, h, juce::Graphics::highResamplingQuality);
    }
    gr.drawImage (scaledBackground, getLocalBounds().toFloat());
}

void Iaq8Editor::resized()
{
    content->setTransform (juce::AffineTransform::scale (getScale()));
    if (dialog != nullptr)
        dialog->setCentrePosition (getLocalBounds().getCentre());
}

float Iaq8Editor::getScale() const noexcept
{
    return (float) getWidth() / (float) g::referenceWidth;
}

juce::Component& Iaq8Editor::getSettingsHotSpot (int index)
{
    return *hotSpots[(size_t) juce::jlimit (0, (int) hotSpots.size() - 1, index)];
}

//==============================================================================
bool Iaq8Editor::zoomFits (float zoom, juce::Rectangle<int> available)
{
    if (preferences::zoomChoiceIndex (zoom) == 0)
        return true; // the smallest size is always offered
    return std::lround ((float) g::referenceWidth * zoom) + hostWindowAllowanceWidth <= available.getWidth()
        && std::lround ((float) g::referenceHeight * zoom) + hostWindowAllowanceHeight <= available.getHeight();
}

juce::Rectangle<int> Iaq8Editor::getAvailableScreenArea() const
{
    if (! screenAreaOverride.isEmpty())
        return screenAreaOverride;
    const auto& screens = juce::Desktop::getInstance().getDisplays();
    const auto* display = isShowing() ? screens.getDisplayForRect (getScreenBounds())
                                      : screens.getDisplayForPoint (juce::Desktop::getMousePosition().toFloat());
    if (display == nullptr)
        display = screens.getPrimaryDisplay();
    if (display == nullptr)
        return { std::numeric_limits<int>::max(), std::numeric_limits<int>::max() }; // no display: nothing to limit
    return display->userBounds.getSmallestIntegerContainer();
}

void Iaq8Editor::setScreenAreaForTesting (juce::Rectangle<int> area)
{
    screenAreaOverride = area;
}

bool Iaq8Editor::setZoom (float zoom)
{
    const auto index = preferences::zoomChoiceIndex (zoom);
    if (index < 0 || ! zoomFits (zoom, getAvailableScreenArea()))
        return false;
    zoom = zoomChoices[(size_t) index];
    setSize ((int) std::lround ((float) g::referenceWidth * zoom), (int) std::lround ((float) g::referenceHeight * zoom));
    preferences::saveZoom (zoom);
    return true;
}

//==============================================================================
juce::PopupMenu Iaq8Editor::createZoomMenu() const
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Zoom");
    const auto available = getAvailableScreenArea();
    for (size_t i = 0; i < zoomChoices.size(); ++i)
    {
        const auto fits = zoomFits (zoomChoices[i], available);
        menu.addItem ((int) i + 1,
                      juce::String (juce::roundToInt (zoomChoices[i] * 100.0f)) + " %" + (fits ? "" : "  (no cabe en esta pantalla)"),
                      fits,
                      std::abs (getScale() - zoomChoices[i]) < 0.002f);
    }
    return menu;
}

juce::PopupMenu Iaq8Editor::createSettingsMenu() const
{
    auto menu = createZoomMenu();
    menu.addSeparator();
    menu.addItem (accessCodeItem, utf8 ("C\xc3\xb3" "digo de acceso\xe2\x80\xa6"));
    menu.addSeparator();
    menu.addItem (aboutItem, "Acerca de IAQ8");
    return menu;
}

void Iaq8Editor::handleSettingsMenuResult (int itemId)
{
    if (itemId >= 1 && itemId <= (int) zoomChoices.size())
        setZoom (zoomChoices[(size_t) itemId - 1]);
    else if (itemId == accessCodeItem)
        showAccessCodeDialog();
    else if (itemId == aboutItem)
        showAboutDialog();
}

void Iaq8Editor::showSettingsMenu (juce::Component& target)
{
    ++settingsMenuRequests;
    juce::Component::SafePointer<Iaq8Editor> self (this);
    createSettingsMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                                        [self] (int result)
                                        {
                                            if (self != nullptr)
                                                self->handleSettingsMenuResult (result);
                                        });
}

//==============================================================================
// Dialogs are AlertWindows inside the editor (so they stay with the plug-in
// window), modal but asynchronous: enterModalState with a callback, never a
// modal loop. One at a time; the editor owns it.
void Iaq8Editor::openDialog (std::unique_ptr<juce::AlertWindow> window, DialogHandler onClose)
{
    dialog.reset(); // a previous one closes (its callback is then ignored)
    dialog = std::move (window);
    onDialogClosed = std::move (onClose);

    addChildComponent (*dialog); // out of the desktop, into the editor
    dialog->setCentrePosition (getLocalBounds().getCentre());

    juce::Component::SafePointer<Iaq8Editor> self (this);
    const juce::AlertWindow* identity = dialog.get(); // compared only, never dereferenced
    dialog->enterModalState (isShowing(), // takes the focus (only possible on screen)
                             juce::ModalCallbackFunction::create ([self, identity] (int result)
                                                                  {
                                                                      if (self != nullptr)
                                                                          self->dialogFinished (identity, result);
                                                                  }),
                             false);
}

void Iaq8Editor::dialogFinished (const juce::AlertWindow* window, int result)
{
    if (window == nullptr || window != dialog.get())
        return; // replaced or closed with the editor
    const auto finished = std::move (dialog);
    const auto handler = std::move (onDialogClosed);
    onDialogClosed = nullptr;
    if (handler)
        handler (result, *finished);
}

void Iaq8Editor::showAccessCodeDialog()
{
    auto window = std::make_unique<juce::AlertWindow> (utf8 ("C\xc3\xb3" "digo de acceso"),
                                                       utf8 ("Escribe el c\xc3\xb3" "digo de acceso del asistente. "
                                                             "Se guarda solo en este ordenador, nunca en el proyecto."),
                                                       juce::MessageBoxIconType::NoIcon,
                                                       this);
    window->addTextEditor ("code", {}, {}, true); // masked
    window->addButton ("Guardar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    openDialog (std::move (window), [] (int result, juce::AlertWindow& w)
    {
        // The code goes straight to Settings: never kept, shown or logged.
        if (result == 1)
            assist::Settings::setAccessCode (w.getTextEditorContents ("code"));
    });

    if (auto* field = dialog->getTextEditor ("code"); field != nullptr && field->isShowing())
        field->grabKeyboardFocus();
}

void Iaq8Editor::showAboutDialog()
{
    auto window = std::make_unique<juce::AlertWindow> ("Acerca de IAQ8",
                                                       utf8 ("IAQ8 " IAQ8_PLUGIN_VERSION "\n"
                                                             "Ecualizador asistido por IA \xc2\xb7 VitaPlug\n\n"
                                                             "El audio nunca sale de tu ordenador: "
                                                             "solo se env\xc3\xad" "a un resumen del an\xc3\xa1lisis."),
                                                       juce::MessageBoxIconType::NoIcon,
                                                       this);
    window->addButton ("OK", 0, juce::KeyPress (juce::KeyPress::returnKey), juce::KeyPress (juce::KeyPress::escapeKey));
    openDialog (std::move (window), {});
}
//==============================================================================
// Assistant: keys, command line and values (Task 5).
void Iaq8Editor::createAssistantControls()
{
    // The eight keys under the screen: seven quick replies, then VER DETALLE.
    for (size_t i = 0; i < quickKeys.size(); ++i)
    {
        auto key = std::make_unique<RackKey> ("Respuesta " + juce::String ((int) i + 1), RackKey::Style::slot);
        key->setCapBounds (g::quickKeys[i].toNearestInt());
        content->addAndMakeVisible (*key);
        quickKeys[i] = std::move (key);
    }
    for (size_t i = 0; i < quickReplies.size(); ++i)
        quickKeys[i]->onClick = [this, i]
        {
            if (quickReplies[i].isEmpty())
                return;
            endSourceNote();
            iaq8Processor.getSession().say (quickReplies[i].toStdString());
            refreshFromSession();
        };
    auto& detail = *quickKeys.back();
    detail.setName ("Ver detalle");
    detail.setButtonText ("VER\nDETALLE");
    detail.onClick = [this]
    {
        screen->setDetailVisible (! screen->isDetailVisible());
        refreshFromSession();
    };

    // The lower panel's keys; APLICAR is the only orange one.
    const struct
    {
        PanelKey which;
        const char* text;
        g::Rect area;
    } keys[] = { { PanelKey::listen, "ESCUCHAR", g::keyListen },
                 { PanelKey::send, "ENVIAR", g::keySend },
                 { PanelKey::undo, "DESHACER", g::keyUndo },
                 { PanelKey::apply, "APLICAR", g::keyApply } };
    for (const auto& k : keys)
    {
        auto key = std::make_unique<RackKey> (k.text, k.which == PanelKey::apply ? RackKey::Style::apply : RackKey::Style::dark);
        key->setButtonText (k.text);
        key->setCapBounds (k.area.toNearestInt());
        content->addAndMakeVisible (*key);
        panelKeys[(size_t) k.which] = std::move (key);
    }
    // Any other action on the conversation leaves the "Otro" note mode.
    getPanelKey (PanelKey::listen).onClick = [this]
    {
        endSourceNote();
        iaq8Processor.getSession().listenAgain();
        refreshFromSession();
    };
    getPanelKey (PanelKey::send).onClick = [this] { sendCommandLine(); };
    getPanelKey (PanelKey::undo).onClick = [this]
    {
        endSourceNote();
        iaq8Processor.getSession().undo();
        refreshFromSession();
    };
    getPanelKey (PanelKey::apply).onClick = [this]
    {
        endSourceNote();
        iaq8Processor.getSession().apply();
        refreshFromSession();
    };

    // The command line: Enter sends.
    commandLine = std::make_unique<CommandLine>();
    commandLine->setBounds (g::inbox.withHeight (g::inbox.getHeight() + 1.0f).toNearestInt());
    commandLine->field.onReturnKey = [this] { sendCommandLine(); };
    // Escape leaves the "Otro" note mode (nothing is chosen; the typed text stays).
    commandLine->field.onEscapeKey = [this]
    {
        if (! awaitingSourceNote)
            return;
        endSourceNote();
        refreshFromSession();
    };
    content->addAndMakeVisible (*commandLine);

    for (size_t i = 0; i < valueBoxes.size(); ++i)
    {
        valueBoxes[i] = std::make_unique<ValueBox>();
        valueBoxes[i]->setBounds (g::valueBoxes[i].withHeight (g::valueBoxes[i].getHeight() + 1.0f).toNearestInt());
        content->addAndMakeVisible (*valueBoxes[i]);
    }
}

RackKey& Iaq8Editor::getQuickKey (int index)
{
    return *quickKeys[(size_t) juce::jlimit (0, (int) quickKeys.size() - 1, index)];
}

RackKey& Iaq8Editor::getPanelKey (PanelKey which)
{
    return *panelKeys[(size_t) which];
}

juce::TextEditor& Iaq8Editor::getCommandLine() noexcept
{
    return commandLine->field;
}

juce::String Iaq8Editor::getValueText (int box) const
{
    return valueBoxes[(size_t) juce::jlimit (0, (int) valueBoxes.size() - 1, box)]->getText();
}

juce::String Iaq8Editor::keyLabel (const juce::String& reply)
{
    const auto words = juce::StringArray::fromTokens (Iaq8LookAndFeel::toCapitals (reply).trim(), " ", {});
    if (words.size() < 2)
        return words.joinIntoString (" ");

    // The split whose longer line is the shortest (the first such split).
    int best = 1, bestLength = std::numeric_limits<int>::max();
    for (int split = 1; split < words.size(); ++split)
    {
        const auto first = words.joinIntoString (" ", 0, split).length();
        const auto second = words.joinIntoString (" ", split).length();
        if (juce::jmax (first, second) < bestLength)
        {
            bestLength = juce::jmax (first, second);
            best = split;
        }
    }
    return words.joinIntoString (" ", 0, best) + "\n" + words.joinIntoString (" ", best);
}

juce::String Iaq8Editor::formatLevelMatch (double db)
{
    const auto tenths = std::round (db * 10.0) / 10.0;
    if (std::abs (tenths) < 0.05)
        return "0.0 dB";
    return (tenths > 0.0 ? juce::String ("+") : utf8 ("\xe2\x88\x92")) + juce::String (std::abs (tenths), 1) + " dB";
}

void Iaq8Editor::sendCommandLine()
{
    auto& field = commandLine->field;
    const auto text = field.getText().trim();
    if (text.isEmpty())
        return;

    if (commandTextTarget != nullptr)
    {
        const auto target = commandTextTarget; // the target may replace itself
        target (text);
    }
    else
    {
        auto& session = iaq8Processor.getSession();
        if (session.getStage() == assist::Session::Stage::waiting)
            return; // one request at a time: the text waits in the line
        session.say (text.toStdString());
    }
    field.clear();
    refreshFromSession();
}

void Iaq8Editor::refreshFromSession()
{
    auto& session = iaq8Processor.getSession();
    const auto stage = session.getStage();

    // Quick replies: the first seven, each on its key; no text, no key.
    const auto replies = session.getQuickReplies();
    for (size_t i = 0; i < quickReplies.size(); ++i)
    {
        quickReplies[i] = i < replies.size() ? utf8 (replies[i].c_str()) : juce::String();
        auto& key = *quickKeys[i];
        key.setButtonText (keyLabel (quickReplies[i]));
        key.setEnabled (quickReplies[i].isNotEmpty());
        key.setTooltip (quickReplies[i]);
    }

    // VER DETALLE: with a proposal or something applied.
    const auto proposal = iaq8Processor.getProposal();
    const auto countActive = [] (const dsp::BandSet& set)
    {
        return (int) std::count_if (set.begin(), set.end(), [] (const auto& b) { return PhosphorScreen::isActiveBand (b); });
    };
    const auto appliedBands = countActive (iaq8Processor.getAppliedBands());
    const auto detailAvailable = proposal.has_value() || appliedBands > 0;
    if (! detailAvailable)
        screen->setDetailVisible (false);
    auto& detail = *quickKeys.back();
    detail.setEnabled (detailAvailable);
    detail.setLed (screen->isDetailVisible());

    // Panel keys.
    auto& listen = getPanelKey (PanelKey::listen);
    listen.setEnabled (session.getSource().has_value());
    listen.setLed (stage == assist::Session::Stage::listening);
    getPanelKey (PanelKey::send).setEnabled (commandTextTarget != nullptr || stage != assist::Session::Stage::waiting);
    getPanelKey (PanelKey::undo).setEnabled (session.canUndo());
    getPanelKey (PanelKey::apply).setEnabled (session.canApply());

    // Values.
    const auto dash = utf8 ("\xe2\x80\x94");
    const auto source = session.getSource();
    valueBoxes[0]->setText (source.has_value() ? Iaq8LookAndFeel::toCapitals (utf8 (assist::Session::sourceLabel (*source))) : dash);
    const auto listened = session.getListenedSeconds();
    valueBoxes[1]->setText (listened.has_value() ? juce::String (juce::roundToInt (*listened)) + " s" : dash);
    const auto changes = proposal.has_value() ? countActive (*proposal) : appliedBands;
    valueBoxes[2]->setText (juce::String (changes) + (changes == 1 ? " BANDA" : " BANDAS"));
    valueBoxes[3]->setText (formatLevelMatch (iaq8Processor.getCompensationDb()));

    // Top row. A pending "Otro" note ends when the conversation moves on
    // without it: the source, the stage or the messages changed (a reply,
    // the end of a listening, a restore). Until then the combo shows Otro.
    if (awaitingSourceNote
        && (source != sourceWhenNoteBegan || stage != stageWhenNoteBegan || session.getMessages().size() != messagesWhenNoteBegan))
        endSourceNote();
    TopRow::State top;
    top.source = awaitingSourceNote ? std::optional<assist::Source> (assist::Source::other) : source;
    top.listen = session.getListen();
    top.canPreview = proposal.has_value();
    top.online = session.lastRequestAnswered();
    top.listening = stage == assist::Session::Stage::listening;
    top.proposal = proposal.has_value();
    topRow->setState (top);

    updateScreenLines();
}

//==============================================================================
// Top row (Task 6).
void Iaq8Editor::createTopRow()
{
    topRow = std::make_unique<TopRow>();
    topRow->onSourceChosen = [this] (assist::Source s) { sourceChosen (s); };
    topRow->onListenChosen = [this] (dsp::Listen listen)
    {
        iaq8Processor.getSession().setListen (listen);
        refreshFromSession();
    };
    content->addAndMakeVisible (*topRow);
}

void Iaq8Editor::sourceChosen (assist::Source s)
{
    if (s == assist::Source::other)
    {
        beginSourceNote();
    }
    else
    {
        endSourceNote();
        iaq8Processor.getSession().chooseSource (s);
    }
    refreshFromSession();
}

void Iaq8Editor::beginSourceNote()
{
    awaitingSourceNote = true;
    sourceWhenNoteBegan = iaq8Processor.getSession().getSource();
    stageWhenNoteBegan = iaq8Processor.getSession().getStage();
    messagesWhenNoteBegan = iaq8Processor.getSession().getMessages().size();
    commandTextTarget = [this] (const juce::String& note)
    {
        endSourceNote();
        iaq8Processor.getSession().chooseSource (assist::Source::other, note.toStdString());
    };
    auto& field = commandLine->field;
    field.setTextToShowWhenEmpty (utf8 ("Describe la fuente\xe2\x80\xa6"), colours::phosphorPlaceholder);
    if (field.isShowing())
        field.grabKeyboardFocus();
}

void Iaq8Editor::endSourceNote()
{
    if (! awaitingSourceNote)
        return;
    awaitingSourceNote = false;
    sourceWhenNoteBegan.reset();
    commandTextTarget = nullptr;
    commandLine->field.setTextToShowWhenEmpty ({}, colours::phosphorPlaceholder);
}

void Iaq8Editor::updateScreenLines()
{
    auto& session = iaq8Processor.getSession();
    const auto& messages = session.getMessages();

    if (messages.size() < messagesSeen) // the conversation was reloaded or started over
        shownNotices.clear();
    messagesSeen = messages.size();

    const auto notice = utf8 (session.getNotice().c_str());
    if (notice != lastNotice)
    {
        if (notice.isNotEmpty())
            shownNotices.push_back ({ messages.size(), notice });
        lastNotice = notice;
    }

    using Who = PhosphorScreen::Line::Who;
    std::vector<PhosphorScreen::Line> lines;
    lines.reserve (messages.size() + shownNotices.size());
    auto next = shownNotices.begin();
    for (size_t i = 0; i <= messages.size(); ++i)
    {
        for (; next != shownNotices.end() && next->afterMessages <= i; ++next)
            lines.push_back ({ Who::system, next->text });
        if (i < messages.size())
            lines.push_back ({ messages[i].role == assist::Message::Role::user ? Who::user : Who::assistant,
                               utf8 (messages[i].text.c_str()) });
    }
    screen->setLines (std::move (lines));
}
} // namespace iaq8::ui
