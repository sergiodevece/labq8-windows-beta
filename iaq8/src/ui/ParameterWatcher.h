#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace iaq8::ui
{
// Calls `onChange` on the message thread after any of a processor's
// parameters changed: the host's undo, automation in Read, its generic
// controls, or IAQ8 itself. Parameter callbacks may come from any thread
// (the audio thread included), so they only trigger an AsyncUpdater; many
// changes in a row give one call. The listeners go with the watcher (destroy
// it before what `onChange` touches).
class ParameterWatcher final : private juce::AudioProcessorParameter::Listener,
                               private juce::AsyncUpdater
{
public:
    ParameterWatcher (juce::AudioProcessor&, std::function<void()> onChange);
    ~ParameterWatcher() override;

private:
    void parameterValueChanged (int, float) override { triggerAsyncUpdate(); }
    void parameterGestureChanged (int, bool) override {}
    void handleAsyncUpdate() override;

    juce::AudioProcessor& processor;
    std::function<void()> onChange;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterWatcher)
};
} // namespace iaq8::ui
