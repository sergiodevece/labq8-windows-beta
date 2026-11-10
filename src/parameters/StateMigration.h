#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace labq8::params
{
// Version history of the saved state (property on the root element):
//   1  E0   band 1 only (b1_*)
//   2  E1a  bands 1..8 (b1_* .. b8_*)
//   3  E1b  + b1_type .. b8_type (older states load every band as Bell)
//   4  E2c.1 + b{n}_dyn_on, _threshold, _range, _attack, _release (older
//            states load with dynamics off and the default dynamic settings)
//   5  E4a  + proc_mode and the MID / SIDE banks (mid_b{n}_*, side_b{n}_*;
//            older states load in Stereo with both banks at their defaults,
//            i.e. exactly what v1..v4 played). Same serialisation: one
//            PARAM per parameter, 241 in all (docs/MIDSIDE_E4A.md).
inline constexpr const char* stateVersionProperty = "labq8StateVersion";
inline constexpr int currentStateVersion = 5;

// Brings a restored state tree up to currentStateVersion, in place, before it
// is handed to AudioProcessorValueTreeState::replaceState().
//
// Every parameter of `state` (the processor's APVTS) that is absent from the
// tree gets an explicit PARAM child holding its default value. Without this,
// JUCE 9.0.3 re-saves absent parameters with or without a value depending on
// the instance's history, so the same older session would not produce the
// same state (see docs/ARCHITECTURE.md).
// PARAM children with unknown IDs are left alone (replaceState ignores them,
// and the tree keeps them, so they are saved again unchanged).
//
// A state written by a newer version (foundVersion > currentStateVersion)
// gets the same treatment: known parameters are restored, unknown ones are
// kept, and it is saved back with this version's number (see the policy in
// docs/MIDSIDE_E4A.md).
//
// Returns the version found in the tree (0 if the property was missing).
int migrateState (juce::ValueTree& tree, const juce::AudioProcessorValueTreeState& state);
} // namespace labq8::params
