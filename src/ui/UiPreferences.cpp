#include "ui/UiPreferences.h"

#include <juce_data_structures/juce_data_structures.h>

#include <cmath>
#include <optional>

namespace labq8::ui::preferences
{
namespace
{
    constexpr const char* zoomKey = "editorZoom";

    struct Storage
    {
        bool overridden = false;
        juce::File file;             // used when overridden and not empty
        float memoryZoom = defaultZoom;
    };

    Storage& storage()
    {
        static Storage s;
        return s;
    }

    std::optional<juce::File> storageFile()
    {
        const auto& s = storage();
        if (! s.overridden)
            return defaultStorageFile();
        if (s.file == juce::File())
            return std::nullopt; // memory only
        return s.file;
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
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Application Support/VitaPlug/LABQ8/LABQ8.settings");
}

void setStorageFileForTesting (const juce::File& file)
{
    storage() = { true, file, defaultZoom };
}

float loadZoom()
{
    const auto file = storageFile();
    if (! file.has_value())
        return storage().memoryZoom;

    juce::PropertiesFile properties (*file, juce::PropertiesFile::Options());
    const auto index = properties.containsKey (zoomKey) ? zoomChoiceIndex (properties.getDoubleValue (zoomKey)) : -1;
    return index < 0 ? defaultZoom : zoomChoices[(size_t) index];
}

void saveZoom (float zoom)
{
    const auto index = zoomChoiceIndex (zoom);
    if (index < 0)
        return;
    const auto value = zoomChoices[(size_t) index];
    const auto file = storageFile();
    if (! file.has_value())
    {
        storage().memoryZoom = value;
        return;
    }

    // Written at once (no timer): the editor only saves on an explicit
    // choice and when it closes.
    file->getParentDirectory().createDirectory();
    juce::PropertiesFile::Options options;
    options.millisecondsBeforeSaving = -1;
    juce::PropertiesFile properties (*file, options);
    properties.setValue (zoomKey, (double) value);
    properties.saveIfNeeded();
}
} // namespace labq8::ui::preferences
