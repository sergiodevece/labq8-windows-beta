#include "LabQ8Assets.h"
#include "TestHarness.h"
#include "assist/Settings.h"
#include "plugin/Iaq8Processor.h"
#include "ui/Iaq8Editor.h"
#include "ui/Iaq8Geometry.h"
#include "ui/Iaq8LookAndFeel.h"
#include "ui/Iaq8Preferences.h"

#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include <cmath>
#include <limits>
#include <memory>

namespace
{
juce::Image decode (const void* data, int size)
{
    return juce::ImageFileFormat::loadFrom (data, static_cast<size_t> (size));
}
}

LQ_TEST ("ui assets: chassis and LABQ8 sprites load at reference size")
{
    const auto chassis = decode (LabQ8Assets::iaq8_chassis_png, LabQ8Assets::iaq8_chassis_pngSize);
    LQ_CHECK (chassis.isValid(), "chassis valid");
    LQ_CHECK (chassis.getWidth() == 1672, "chassis width 1672");
    LQ_CHECK (chassis.getHeight() == 941, "chassis height 941");

    LQ_CHECK (decode (LabQ8Assets::meter_lit_png, LabQ8Assets::meter_lit_pngSize).isValid(), "meter_lit valid");
    LQ_CHECK (decode (LabQ8Assets::mode_face_lit_png, LabQ8Assets::mode_face_lit_pngSize).isValid(), "mode_face_lit valid");
    LQ_CHECK (decode (LabQ8Assets::mode_face_unlit_png, LabQ8Assets::mode_face_unlit_pngSize).isValid(), "mode_face_unlit valid");
}

LQ_TEST ("ui assets: fonts load")
{
    const struct { const char* data; int size; } fonts[] = {
        { LabQ8Assets::VT323Regular_ttf, LabQ8Assets::VT323Regular_ttfSize },
        { LabQ8Assets::BarlowMedium_ttf, LabQ8Assets::BarlowMedium_ttfSize },
        { LabQ8Assets::BarlowSemiBold_ttf, LabQ8Assets::BarlowSemiBold_ttfSize },
        { LabQ8Assets::BarlowSemiCondensedSemiBold_ttf, LabQ8Assets::BarlowSemiCondensedSemiBold_ttfSize } };

    for (const auto& f : fonts)
    {
        const auto tf = juce::Typeface::createSystemTypefaceFor (f.data, static_cast<size_t> (f.size));
        LQ_CHECK (tf != nullptr, "font loads");
    }
}

//==============================================================================
// Plan 3, Task 2: editor shell, zoom, geometry and look and feel.

namespace
{
namespace geo = iaq8::ui::geometry;
namespace prefs = iaq8::ui::preferences;
using iaq8::ui::Iaq8Editor;
using labq8::test::str;

// The tests never read or write the user's real IAQ8.ui and do not depend on
// the screens of this machine (every zoom fits unless a test says otherwise).
const juce::Rectangle<int> largeScreen { 0, 0, 4000, 3000 };
const bool uiPreferencesInMemory = []
{
    prefs::setStorageFolderForTesting (juce::File());
    Iaq8Editor::setScreenAreaForTesting (largeScreen);
    return true;
}();

template <typename Condition>
bool pumpUntil (Condition condition, int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (! condition() && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    return condition();
}

void pump (int ms) { pumpUntil ([] { return false; }, ms); }

bool near (juce::Rectangle<float> a, juce::Rectangle<float> b, float tolerance)
{
    return std::abs (a.getX() - b.getX()) <= tolerance && std::abs (a.getY() - b.getY()) <= tolerance
        && std::abs (a.getRight() - b.getRight()) <= tolerance && std::abs (a.getBottom() - b.getBottom()) <= tolerance;
}

std::string text (juce::Rectangle<float> r)
{
    return r.toString().toStdString();
}

struct MenuEntry
{
    int id;
    juce::String text;
    bool enabled, ticked, header;
};

std::vector<MenuEntry> entries (const juce::PopupMenu& menu)
{
    std::vector<MenuEntry> result;
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
    {
        const auto& item = it.getItem();
        if (! item.isSeparator)
            result.push_back ({ item.itemID, item.text, item.isEnabled, item.isTicked, item.isSectionHeader });
    }
    return result;
}

// Temporary Settings file (never the user's) for the access code dialog.
struct TemporarySettings
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getNonexistentChildFile ("iaq8-ui-settings", "", false);
    TemporarySettings()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "IAQ8";
        o.filenameSuffix = "settings";
        o.osxLibrarySubFolder = "Application Support";
        o.folderName = dir.getFullPathName();
        iaq8::assist::Settings::setFileOptionsForTesting (o);
    }
    ~TemporarySettings()
    {
        iaq8::assist::Settings::setFileOptionsForTesting (std::nullopt);
        dir.deleteRecursively();
    }
};
} // namespace

LQ_TEST ("editor: constructs at every zoom and maps reference rects to scaled bounds")
{
    // The geometry is the approved mockup's (iaq8-mockup-v2/index.html).
    LQ_CHECK (geo::referenceWidth == 1672 && geo::referenceHeight == 941, "reference space 1672 x 941");
    LQ_CHECK (geo::crt == juce::Rectangle<float> (346.0f, 193.0f, 980.0f, 314.0f), text (geo::crt));
    const float keyX[] = { 365, 486, 606, 727, 847, 968, 1088, 1208 };
    for (size_t i = 0; i < geo::quickKeys.size(); ++i)
        LQ_CHECK (geo::quickKeys[i] == juce::Rectangle<float> (keyX[i], 540.0f, 99.0f, 40.0f), str ("quick key ", i, ": ", text (geo::quickKeys[i])));
    LQ_CHECK (geo::keyApply == juce::Rectangle<float> (1352.0f, 796.0f, 186.0f, 72.0f), text (geo::keyApply));

    prefs::setStorageFolderForTesting (juce::File());
    Iaq8Editor::setScreenAreaForTesting (largeScreen);
    iaq8::Iaq8AudioProcessor processor;

    {
        std::unique_ptr<juce::AudioProcessorEditor> created (processor.createEditor());
        LQ_CHECK (dynamic_cast<Iaq8Editor*> (created.get()) != nullptr, "createEditor gives the IAQ8 editor");
    }

    for (const auto zoom : prefs::zoomChoices)
    {
        LQ_CHECK (Iaq8Editor::zoomFits (zoom, largeScreen), str (zoom, " fits a large screen"));

        // Opens at the stored zoom.
        prefs::saveZoom (zoom);
        auto editor = std::make_unique<Iaq8Editor> (processor);
        LQ_CHECK (std::abs (editor->getScale() - zoom) < 0.001f, str ("opens at ", zoom, ": scale ", editor->getScale()));
        LQ_CHECK (editor->getWidth() == (int) std::lround (1672.0f * zoom) && editor->getHeight() == (int) std::lround (941.0f * zoom),
                  str (zoom, ": ", editor->getWidth(), "x", editor->getHeight()));

        const auto scaled = editor->getLocalArea (&editor->getContent(), geo::quickKeys[0]);
        const auto expected = geo::quickKeys[0] * zoom;
        LQ_CHECK (near (scaled, expected, 1.0f), str (zoom, ": quick key 1 at ", text (scaled), " expected ", text (expected)));

        // The settings hot spots sit on the panel's top-left screw and on the logo.
        LQ_CHECK (near (editor->getLocalArea (&editor->getContent(), editor->getSettingsHotSpot (0).getBounds().toFloat()), geo::settingsScrew * zoom, 1.0f),
                  str (zoom, ": screw hot spot"));
        LQ_CHECK (near (editor->getLocalArea (&editor->getContent(), editor->getSettingsHotSpot (1).getBounds().toFloat()), geo::logo * zoom, 1.0f),
                  str (zoom, ": logo hot spot"));

        // setZoom resizes the editor the same way.
        LQ_CHECK (editor->setZoom (0.4f) && editor->getWidth() == 669, "back to 40 %");
        LQ_CHECK (editor->setZoom (zoom) && std::abs (editor->getScale() - zoom) < 0.001f, str ("setZoom ", zoom));
    }

    // 40 % always fits; larger choices only when the screen has room.
    const juce::Rectangle<int> tiny { 0, 0, 200, 100 }, laptop { 0, 0, 1710, 1072 };
    LQ_CHECK (Iaq8Editor::zoomFits (0.4f, tiny), "40 % always fits");
    LQ_CHECK (Iaq8Editor::zoomFits (1.0f, laptop) && ! Iaq8Editor::zoomFits (1.2f, laptop), "laptop: up to 100 %");

    Iaq8Editor::setScreenAreaForTesting (laptop);
    prefs::saveZoom (1.4f);
    {
        Iaq8Editor editor (processor);
        LQ_CHECK (editor.getWidth() == 1672, str ("opens at the largest that fits: ", editor.getWidth()));
        LQ_CHECK (! editor.setZoom (1.2f) && editor.getWidth() == 1672, "a zoom that does not fit is refused");
        LQ_CHECK (! editor.setZoom (0.5f) && ! editor.setZoom (std::numeric_limits<float>::quiet_NaN()), "only the six choices");
    }
    LQ_CHECK (juce::exactlyEqual (prefs::loadZoom(), 1.4f), "the stored preference is kept");
    Iaq8Editor::setScreenAreaForTesting (largeScreen);
    prefs::setStorageFolderForTesting (juce::File());
}

LQ_TEST ("editor: zoom preference is IAQ8's own")
{
    const auto labq8File = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                               .getChildFile ("Application Support/VitaPlug/LABQ8/LABQ8.settings");
    const auto labq8Existed = labq8File.existsAsFile();
    const auto labq8Modified = labq8File.getLastModificationTime();
    const auto labq8Size = labq8File.getSize();

    const auto userFile = prefs::defaultStorageFile();
    LQ_CHECK (userFile.getFullPathName().endsWith ("VitaPlug/IAQ8/IAQ8.ui"), userFile.getFullPathName().toStdString());
    LQ_CHECK (userFile != labq8File, "not LABQ8's file");

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("iaq8-ui-prefs", "", false);
    prefs::setStorageFolderForTesting (dir);
    LQ_CHECK (prefs::storageFile() == dir.getChildFile ("IAQ8.ui"), prefs::storageFile().getFullPathName().toStdString());
    LQ_CHECK (juce::exactlyEqual (prefs::loadZoom(), prefs::defaultZoom) && juce::exactlyEqual (prefs::defaultZoom, 0.6f), "nothing stored: 60 %");

    prefs::saveZoom (1.2f);
    LQ_CHECK (dir.getChildFile ("IAQ8.ui").existsAsFile(), "written at once to IAQ8.ui");
    LQ_CHECK (juce::exactlyEqual (prefs::loadZoom(), 1.2f), str ("loads 1.2: ", prefs::loadZoom()));

    // Read back by a new editor, as after restarting the host.
    prefs::setStorageFolderForTesting (dir);
    {
        iaq8::Iaq8AudioProcessor processor;
        Iaq8Editor editor (processor);
        LQ_CHECK (editor.getWidth() == 2006, str ("restored from the file: ", editor.getWidth()));
        LQ_CHECK (editor.setZoom (0.8f) && juce::exactlyEqual (prefs::loadZoom(), 0.8f), "a menu choice is stored");
    }

    // Only the six choices are stored; garbage falls back to 60 %.
    for (const float other : { 0.75f, 0.0f, 3.0f, std::numeric_limits<float>::quiet_NaN() })
    {
        prefs::saveZoom (other);
        LQ_CHECK (juce::exactlyEqual (prefs::loadZoom(), 0.8f), str ("not stored: ", other));
    }
    dir.getChildFile ("IAQ8.ui").replaceWithText ("not xml");
    LQ_CHECK (juce::exactlyEqual (prefs::loadZoom(), 0.6f), "unreadable file: 60 %");

    LQ_CHECK (dir.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 1, "only IAQ8.ui in the folder");
    LQ_CHECK (labq8File.existsAsFile() == labq8Existed && labq8File.getLastModificationTime() == labq8Modified
                  && labq8File.getSize() == labq8Size, "LABQ8's preferences untouched");

    dir.deleteRecursively();
    prefs::setStorageFolderForTesting (juce::File());
}

LQ_TEST ("editor: settings menu has zoom, access code and about")
{
    prefs::setStorageFolderForTesting (juce::File());
    Iaq8Editor::setScreenAreaForTesting (largeScreen);
    const TemporarySettings settings;
    iaq8::Iaq8AudioProcessor processor;
    auto editor = std::make_unique<Iaq8Editor> (processor);

    const auto items = entries (editor->createSettingsMenu());
    juce::StringArray zoomNames;
    int ticked = 0, enabledZooms = 0;
    bool hasCode = false, hasAbout = false, hasZoomHeader = false;
    for (const auto& e : items)
    {
        if (e.header && e.text == "Zoom")
            hasZoomHeader = true;
        else if (e.id >= 1 && e.id <= 6)
        {
            zoomNames.add (e.text);
            enabledZooms += e.enabled ? 1 : 0;
            ticked = e.ticked ? e.id : ticked;
        }
        else if (e.id == Iaq8Editor::accessCodeItem)
            hasCode = e.enabled && e.text == juce::String (juce::CharPointer_UTF8 ("C\xc3\xb3" "digo de acceso\xe2\x80\xa6"));
        else if (e.id == Iaq8Editor::aboutItem)
            hasAbout = e.enabled && e.text == "Acerca de IAQ8";
    }
    LQ_CHECK (hasZoomHeader, "zoom section");
    LQ_CHECK (zoomNames.joinIntoString ("|") == "40 %|60 %|80 %|100 %|120 %|140 %", zoomNames.joinIntoString ("|").toStdString());
    LQ_CHECK (enabledZooms == 6 && ticked == 2, str ("all available, 60 % ticked: ", enabledZooms, " ", ticked));
    LQ_CHECK (hasCode, "access code item");
    LQ_CHECK (hasAbout, "about item");

    editor->handleSettingsMenuResult (4);
    LQ_CHECK (editor->getWidth() == 1672 && juce::exactlyEqual (prefs::loadZoom(), 1.0f), "zoom item resizes and stores");
    editor->handleSettingsMenuResult (0);
    editor->handleSettingsMenuResult (99);
    LQ_CHECK (editor->getWidth() == 1672 && editor->getOpenDialog() == nullptr, "dismissed or unknown item does nothing");

    // Access code: a masked field, saved only through Settings.
    editor->handleSettingsMenuResult (Iaq8Editor::accessCodeItem);
    auto* dialog = editor->getOpenDialog();
    LQ_CHECK (dialog != nullptr && dialog->isCurrentlyModal(), "access code dialog open (async modal)");
    auto* field = dialog != nullptr ? dialog->getTextEditor ("code") : nullptr;
    LQ_CHECK (field != nullptr && field->getPasswordCharacter() != 0, "masked field");
    if (dialog != nullptr && field != nullptr)
    {
        field->setText ("dummy-code-5K1");
        dialog->exitModalState (1); // Guardar
        pumpUntil ([&] { return editor->getOpenDialog() == nullptr; }, 1000);
        LQ_CHECK (editor->getOpenDialog() == nullptr, "dialog closed");
        LQ_CHECK (iaq8::assist::Settings::accessCode() == "dummy-code-5K1", "saved through Settings"); // value never printed
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        const std::string bytes (static_cast<const char*> (state.getData()), state.getSize());
        LQ_CHECK (bytes.find ("dummy-code-5K1") == std::string::npos, "never in the DAW state");
    }

    // Cancel keeps the stored code.
    editor->handleSettingsMenuResult (Iaq8Editor::accessCodeItem);
    dialog = editor->getOpenDialog();
    if (dialog != nullptr && dialog->getTextEditor ("code") != nullptr)
    {
        dialog->getTextEditor ("code")->setText ("other-code");
        dialog->exitModalState (0); // Cancelar
        pumpUntil ([&] { return editor->getOpenDialog() == nullptr; }, 1000);
    }
    LQ_CHECK (editor->getOpenDialog() == nullptr && iaq8::assist::Settings::accessCode() == "dummy-code-5K1", "cancel keeps the code");

    // About: name and version.
    editor->handleSettingsMenuResult (Iaq8Editor::aboutItem);
    dialog = editor->getOpenDialog();
    LQ_CHECK (dialog != nullptr && dialog->getName().contains ("IAQ8"), "about dialog");
    if (dialog != nullptr)
    {
        dialog->exitModalState (0);
        pumpUntil ([&] { return editor->getOpenDialog() == nullptr; }, 1000);
    }
    LQ_CHECK (editor->getOpenDialog() == nullptr, "about closed");

    // Closing the editor with a dialog open neither waits nor saves.
    editor->handleSettingsMenuResult (Iaq8Editor::accessCodeItem);
    if (auto* d = editor->getOpenDialog(); d != nullptr && d->getTextEditor ("code") != nullptr)
        d->getTextEditor ("code")->setText ("never-saved");
    editor.reset();
    pump (100);
    LQ_CHECK (iaq8::assist::Settings::accessCode() == "dummy-code-5K1", "editor closed with the dialog open: nothing saved");
}

LQ_TEST ("ui look and feel: embedded fonts and a deterministic worn silkscreen")
{
    using LnF = iaq8::ui::Iaq8LookAndFeel;
    LQ_CHECK (LnF::screenFont (24.0f).getTypefaceName() == "VT323", LnF::screenFont (24.0f).getTypefaceName().toStdString());
    LQ_CHECK (LnF::labelFont (16.0f, false).getTypefaceName() == "Barlow", LnF::labelFont (16.0f, false).getTypefaceName().toStdString());
    LQ_CHECK (LnF::keyFont (17.0f).getTypefaceName() == "Barlow Semi Condensed", LnF::keyFont (17.0f).getTypefaceName().toStdString());

    const iaq8::ui::SilkText item { "SOURCE", geo::sourceLabel, LnF::labelFont (16.0f, false), 1.6f };
    const auto wa = LnF::renderWornText (item, 2.0f), wb = LnF::renderWornText (item, 2.0f);
    const auto& a = wa.image;
    const auto& b = wb.image;
    LQ_CHECK (a.isValid() && a.getWidth() >= (int) (geo::sourceLabel.getWidth() * 2.0f), str ("rendered ", a.getWidth(), "x", a.getHeight()));
    LQ_CHECK (wa.area.contains (geo::sourceLabel.getTopLeft()) && std::abs (wa.area.getWidth() * 2.0f - (float) a.getWidth()) < 0.01f,
              str ("image area ", text (wa.area)));

    int inked = 0, differ = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
        {
            inked += a.getPixelAt (x, y).getAlpha() > 0 ? 1 : 0;
            differ += a.getPixelAt (x, y) != b.getPixelAt (x, y) ? 1 : 0;
        }
    LQ_CHECK (inked > 200 && differ == 0, str ("same wear every time: ", inked, " inked, ", differ, " differ"));

    // The mask is fixed in reference space (as the mockup's: about half the
    // print intact, the rest thinned, a few spots worn through).
    int worn = 0, intact = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 200; ++x)
        {
            const auto m = LnF::wearAt ({ (float) x * 0.7f, (float) y * 0.7f });
            worn += m < 0.5f ? 1 : 0;
            intact += m > 0.99f ? 1 : 0;
        }
    LQ_CHECK (intact > 15000 && worn > 200 && worn < 4000, str ("mask: ", intact, " intact, ", worn, " worn of 40000"));
    LQ_CHECK (juce::exactlyEqual (LnF::wearAt ({ 10.3f, 20.7f }), LnF::wearAt ({ 10.3f, 20.7f })), "deterministic");
}
