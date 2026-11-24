#pragma once

#include "plugin/Iaq8Processor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace iaq8
{
struct HttpProbeResult
{
    int statusCode = 0;   // 0: no HTTP response (see error)
    juce::String error;
};

// Blocking HTTPS GET with no body and no credentials. Never call it from the
// audio thread or the message thread. statusCode is the HTTP status that
// arrived (any status proves the HTTPS path works).
HttpProbeResult probeHttpStatus (const juce::String& url, int timeoutMs);

// Provisional editor for the network spike (replaced in Plan 3): one button
// that probes api.anthropic.com from a detached background thread. Closing the
// editor never waits for the network: the worker owns no editor state and
// reports back through a SafePointer on the message thread.
class DebugEditor final : public juce::AudioProcessorEditor
{
public:
    explicit DebugEditor (Iaq8AudioProcessor&);
    ~DebugEditor() override;

    void resized() override;

private:
    void startProbe();

    juce::TextButton probeButton { juce::String (juce::CharPointer_UTF8 ("Probar conexi\xc3\xb3n")) };
    juce::Label resultLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DebugEditor)
};
} // namespace iaq8
