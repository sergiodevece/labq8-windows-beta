#include "assist/Settings.h"

#include <mutex>

#ifndef IAQ8_DEFAULT_SERVER_URL
 #error "IAQ8_DEFAULT_SERVER_URL must be defined by CMake"
#endif

namespace iaq8::assist
{
namespace
{
constexpr const char* kServerUrlKey = "server_url";
constexpr const char* kAccessCodeKey = "access_code";

std::mutex& settingsMutex()
{
    static std::mutex m;
    return m;
}

std::optional<juce::PropertiesFile::Options>& testingOptions()
{
    static std::optional<juce::PropertiesFile::Options> o;
    return o;
}

juce::PropertiesFile::Options defaultOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "IAQ8";
    o.filenameSuffix = "settings";
    o.folderName = "VitaPlug/IAQ8";
    o.osxLibrarySubFolder = "Application Support";
    o.commonToAllUsers = false;
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    return o;
}

juce::PropertiesFile::Options currentOptions()
{
    auto o = testingOptions().value_or (defaultOptions());
    o.millisecondsBeforeSaving = -1; // saved explicitly: no timer, so any thread may use it
    return o;
}
} // namespace

juce::PropertiesFile::Options Settings::fileOptions()
{
    const std::lock_guard<std::mutex> lock (settingsMutex());
    return currentOptions();
}

void Settings::setFileOptionsForTesting (std::optional<juce::PropertiesFile::Options> options)
{
    const std::lock_guard<std::mutex> lock (settingsMutex());
    testingOptions() = std::move (options);
}

juce::URL Settings::serverUrl()
{
    const std::lock_guard<std::mutex> lock (settingsMutex());
    juce::PropertiesFile file (currentOptions());
    const auto value = file.getValue (kServerUrlKey).trim();
    return juce::URL (value.isNotEmpty() ? value : juce::String (IAQ8_DEFAULT_SERVER_URL));
}

juce::String Settings::accessCode()
{
    const std::lock_guard<std::mutex> lock (settingsMutex());
    juce::PropertiesFile file (currentOptions());
    return file.getValue (kAccessCodeKey).trim();
}

void Settings::setAccessCode (const juce::String& code)
{
    const std::lock_guard<std::mutex> lock (settingsMutex());
    juce::PropertiesFile file (currentOptions());
    file.setValue (kAccessCodeKey, code.trim());
    file.saveIfNeeded();
}
} // namespace iaq8::assist
