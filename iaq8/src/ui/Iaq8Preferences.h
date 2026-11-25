#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <optional>

// Editor preferences of the user, global to the machine (not per session):
// IAQ8's own file, never LABQ8's. Never part of the plugin state, so they
// cannot change the audio or saved sessions.
//
// Stored in "VitaPlug/IAQ8/IAQ8.ui" under the user's application data folder
// (macOS: ~/Library/Application Support/VitaPlug/IAQ8/IAQ8.ui), next to the
// assistant settings (assist::Settings, IAQ8.settings). Message thread only.

namespace iaq8::ui::preferences
{
// The six editor scales (fractions of the 1672 x 941 reference), as LABQ8.
inline constexpr std::array<float, 6> zoomChoices { 0.4f, 0.6f, 0.8f, 1.0f, 1.2f, 1.4f };
inline constexpr float defaultZoom = 0.6f;

// The stored choice when it is one of zoomChoices; defaultZoom otherwise
// (nothing stored, unreadable, or not one of the six).
float loadZoom();
// Only one of zoomChoices is stored, at once; anything else is ignored.
void saveZoom (float zoom);

// The entry of zoomChoices equal to `zoom` (within 0.005), or -1.
int zoomChoiceIndex (double zoom);

// The user's file, and the one in use now (the user's unless a test moved it).
juce::File defaultStorageFile();
juce::File storageFile();

// Testing hook, like assist::Settings: keep IAQ8.ui in `folder` instead; with
// juce::File() keep the preferences in memory only (no file is read or
// written); std::nullopt goes back to the user's file.
void setStorageFolderForTesting (std::optional<juce::File> folder);
} // namespace iaq8::ui::preferences
