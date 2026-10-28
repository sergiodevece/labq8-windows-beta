#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace labq8::params
{
// Version history of the saved state (property on the root element):
//   1  E0   band 1 only (b1_*)
//   2  E1a  bands 1..8 (b1_* .. b8_*)
inline constexpr const char* stateVersionProperty = "labq8StateVersion";
inline constexpr int currentStateVersion = 2;

// Brings a restored state tree up to currentStateVersion, in place, before it
// is handed to AudioProcessorValueTreeState::replaceState().
//
// Every parameter of `state` (the processor's APVTS) that is absent from the
// tree gets an explicit PARAM child holding its default value. Without this,
// JUCE leaves absent parameters at whatever value the instance currently has,
// so loading an E0 session would depend on the instance's history.
// PARAM children with unknown IDs are left alone (replaceState ignores them).
//
// Returns the version found in the tree (0 if the property was missing).
int migrateState (juce::ValueTree& tree, const juce::AudioProcessorValueTreeState& state);
} // namespace labq8::params
