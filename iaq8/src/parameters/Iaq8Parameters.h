#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace iaq8::params
{
// Bumped whenever the host-visible parameter set changes.
constexpr int stateVersion = 1;

// The 40 static parameters of LABQ8's STEREO bank (same IDs, names, ranges,
// defaults and version hints). No dynamics, no processing mode, no Mid/Side.
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace iaq8::params
