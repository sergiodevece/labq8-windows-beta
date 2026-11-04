#pragma once

#include <juce_core/juce_core.h>

#include <array>

// UI-1.1: editor preferences of the user, global to the machine (not per
// session). They are never part of the plugin state, so they cannot change
// the audio or the compatibility of saved sessions.
//
// Stored in ~/Library/Application Support/VitaPlug/LABQ8/LABQ8.settings.

namespace labq8::ui::preferences
{
// The six editor scales (fractions of the 1672 x 941 reference).
inline constexpr std::array<float, 6> zoomChoices { 0.4f, 0.6f, 0.8f, 1.0f, 1.2f, 1.4f };
inline constexpr float defaultZoom = 0.6f;

// The stored choice when it is one of zoomChoices; defaultZoom otherwise
// (nothing stored, unreadable, or a size of an earlier build).
float loadZoom();
// Only one of zoomChoices is stored; anything else is ignored.
void saveZoom (float zoom);

// The entry of zoomChoices equal to `zoom` (within 0.005), or -1.
int zoomChoiceIndex (double zoom);

juce::File defaultStorageFile();

// Tests and tools: keep the preferences in this file instead, or in memory
// only with juce::File() (the user's file is then never read or written).
void setStorageFileForTesting (const juce::File& file);
} // namespace labq8::ui::preferences
