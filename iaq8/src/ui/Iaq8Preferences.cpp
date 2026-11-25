#include "ui/Iaq8Preferences.h"

#include <juce_data_structures/juce_data_structures.h>

#include <cmath>

namespace iaq8::ui::preferences
{
namespace
{
constexpr const char* zoomKey = "editorZoom";
constexpr const char* fileName = "IAQ8.ui";

struct Storage
{
    std::optional<juce::File> folder;   // set by a test; juce::File(): memory only
    float memoryZoom = defaultZoom;
};

Storage& storage()
{
    static Storage s;
    return s;
}

bool inMemoryOnly()
{
    return storage().folder.has_value() && *storage().folder == juce::File();
}

juce::PropertiesFile::Options userOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "IAQ8";
    o.filenameSuffix = "ui";
    o.folderName = "VitaPlug/IAQ8";
    o.osxLibrarySubFolder = "Application Support";
    o.commonToAllUsers = false;
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    return o;
}

juce::PropertiesFile::Options fileOptions()
{
    auto o = userOptions();
    o.millisecondsBeforeSaving = -1; // saved explicitly, at once: no timer
    return o;
}
} // namespace

int zoomChoiceIndex (double zoom)
{
    for (size_t i = 0; i < zoomChoices.size(); ++i)
        if (std::abs (zoom - (double) zoomChoices[i]) < 0.005) // NaN never matches
            return (int) i;
    return -1;
}

juce::File defaultStorageFile()
{
    return userOptions().getDefaultFile();
}

juce::File storageFile()
{
    const auto& s = storage();
    if (! s.folder.has_value())
        return defaultStorageFile();
    if (*s.folder == juce::File())
        return {};
    return s.folder->getChildFile (fileName);
}

void setStorageFolderForTesting (std::optional<juce::File> folder)
{
    storage() = { std::move (folder), defaultZoom };
}

float loadZoom()
{
    if (inMemoryOnly())
        return storage().memoryZoom;

    const juce::PropertiesFile properties (storageFile(), fileOptions());
    const auto index = properties.containsKey (zoomKey) ? zoomChoiceIndex (properties.getDoubleValue (zoomKey)) : -1;
    return index < 0 ? defaultZoom : zoomChoices[(size_t) index];
}

void saveZoom (float zoom)
{
    const auto index = zoomChoiceIndex (zoom);
    if (index < 0)
        return;

    const auto value = zoomChoices[(size_t) index];
    if (inMemoryOnly())
    {
        storage().memoryZoom = value;
        return;
    }

    const auto file = storageFile();
    file.getParentDirectory().createDirectory();
    juce::PropertiesFile properties (file, fileOptions());
    properties.setValue (zoomKey, (double) value);
    properties.saveIfNeeded();
}
} // namespace iaq8::ui::preferences
