#pragma once

#include "assist/Session.h"
#include "plugin/Iaq8Processor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

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

// Provisional editor (replaced in Plan 3), plain JUCE components:
//  - the conversation as a text log, the session notice and the stage;
//  - the quick replies on 8 buttons, a text field with "Enviar";
//  - Escuchar, Antes/Después, Aplicar, Deshacer and Empezar de nuevo;
//  - the loudness compensation and the seconds listened, as text;
//  - an access code field saved with Settings::setAccessCode (never in the
//    DAW state, never shown back or logged);
//  - the network probe of the spike: it probes api.anthropic.com from a
//    detached background thread. Closing the editor never waits for the
//    network: the worker owns no editor state and reports back through a
//    SafePointer on the message thread.
// Component IDs (for tests): quick0..quick7, input, send, listen, compare,
// apply, undo, startOver, accessCode, saveCode, log, notice, status.
class DebugEditor final : public juce::AudioProcessorEditor,
                          private assist::Session::Listener,
                          private juce::Timer
{
public:
    explicit DebugEditor (Iaq8AudioProcessor&);
    ~DebugEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void sessionChanged() override;
    void timerCallback() override;
    void refreshMeters();
    void sendInput();
    void saveAccessCode();
    void startProbe();

    Iaq8AudioProcessor& iaq8Processor;
    assist::Session& session;

    juce::TextEditor log, input, accessCode;
    juce::Label notice, status, meters, codeStatus;
    std::array<juce::TextButton, 8> quickButtons;
    juce::TextButton sendButton, listenButton, compareButton, applyButton, undoButton, startOverButton, saveCodeButton;

    juce::TextButton probeButton;
    juce::Label resultLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DebugEditor)
};
} // namespace iaq8
