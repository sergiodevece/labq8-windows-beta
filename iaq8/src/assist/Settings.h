#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <optional>

namespace iaq8::assist
{
// Per-user settings in a PropertiesFile under the user's application data
// folder, "VitaPlug/IAQ8" (macOS: ~/Library/Application Support/VitaPlug/IAQ8/
// IAQ8.settings). Never part of the DAW session state: the access code lives
// only here. Each call reads or writes the file; any thread (serialised).
struct Settings
{
    // Key server_url if set and non-empty, else IAQ8_DEFAULT_SERVER_URL (CMake).
    static juce::URL serverUrl();
    static juce::String accessCode();           // "" when not set
    static void setAccessCode (const juce::String& code);

    static juce::PropertiesFile::Options fileOptions();

    // Testing hook: use other file options (e.g. a temporary folder) until
    // reset with std::nullopt. Tests never touch the real user settings.
    static void setFileOptionsForTesting (std::optional<juce::PropertiesFile::Options> options);
};
} // namespace iaq8::assist
