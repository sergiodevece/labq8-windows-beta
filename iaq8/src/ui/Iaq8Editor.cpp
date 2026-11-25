#include "ui/Iaq8Editor.h"

#include "LabQ8Assets.h"
#include "assist/Settings.h"
#include "plugin/Iaq8Processor.h"
#include "ui/Iaq8Geometry.h"

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

    std::function<void()> onPopupMenu;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onPopupMenu)
            onPopupMenu();
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
    }

private:
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
Iaq8Editor::Iaq8Editor (Iaq8AudioProcessor& p)
    : AudioProcessorEditor (p), iaq8Processor (p)
{
    setLookAndFeel (&lookAndFeel);

    auto chassis = std::make_unique<Content>();
    chassis->onPopupMenu = [this] { showSettingsMenu (*content); };
    content = std::move (chassis);
    addAndMakeVisible (*content);

    const g::Rect spots[] = { g::settingsScrew, g::logo };
    for (size_t i = 0; i < hotSpots.size(); ++i)
    {
        auto spot = std::make_unique<HotSpot> ("Ajustes");
        spot->setBounds (spots[i].toNearestInt());
        auto* target = spot.get();
        spot->onClick = [this, target] { showSettingsMenu (*target); };
        content->addAndMakeVisible (*spot);
        hotSpots[i] = std::move (spot);
    }

    // No resize corner and no resizing by the host: the size only changes
    // through the zoom menu. Opens at the stored zoom, or at the largest
    // smaller choice that fits this screen (the preference is kept).
    setResizable (false, false);
    auto zoom = preferences::loadZoom();
    for (auto i = (size_t) juce::jmax (0, preferences::zoomChoiceIndex (zoom)); i > 0 && ! zoomFits (zoom, getAvailableScreenArea()); --i)
        zoom = zoomChoices[i - 1];
    setSize ((int) std::lround ((float) g::referenceWidth * zoom), (int) std::lround ((float) g::referenceHeight * zoom));
}

Iaq8Editor::~Iaq8Editor()
{
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
} // namespace iaq8::ui
