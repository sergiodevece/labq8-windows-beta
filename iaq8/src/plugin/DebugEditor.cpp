#include "plugin/DebugEditor.h"

#include <thread>

namespace iaq8
{
namespace
{
constexpr const char* probeUrl = "https://api.anthropic.com/v1/models";
}

HttpProbeResult probeHttpStatus (const juce::String& url, int timeoutMs)
{
    HttpProbeResult result;
    int status = 0;

    auto stream = juce::URL (url).createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs (timeoutMs)
            .withStatusCode (&status));

    // A 4xx/5xx still yields a stream (or none) plus the status code.
    result.statusCode = status;

    if (status == 0)
        result.error = stream == nullptr ? juce::String (juce::CharPointer_UTF8 ("sin respuesta (red bloqueada o sin conexi\xc3\xb3n)")) : juce::String (juce::CharPointer_UTF8 ("sin c\xc3\xb3" "digo HTTP"));

    return result;
}

DebugEditor::DebugEditor (Iaq8AudioProcessor& p) : AudioProcessorEditor (p)
{
    addAndMakeVisible (probeButton);
    addAndMakeVisible (resultLabel);
    resultLabel.setJustificationType (juce::Justification::centred);
    resultLabel.setText ("Sin probar", juce::dontSendNotification);
    probeButton.onClick = [this] { startProbe(); };
    setSize (360, 140);
}

DebugEditor::~DebugEditor() = default; // never waits for a probe in flight

void DebugEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    probeButton.setBounds (area.removeFromTop (40));
    area.removeFromTop (10);
    resultLabel.setBounds (area);
}

void DebugEditor::startProbe()
{
    probeButton.setEnabled (false);
    resultLabel.setText ("Probando...", juce::dontSendNotification);

    // Detached on purpose: the editor may close while the request is pending
    // (up to the 10 s timeout). The worker touches only its own locals; the
    // result is delivered through a SafePointer, which is null once the
    // editor is gone.
    std::thread ([safe = juce::Component::SafePointer<DebugEditor> (this)]
    {
        const auto result = probeHttpStatus (probeUrl, 10000);
        const auto text = result.statusCode != 0 ? "HTTP " + juce::String (result.statusCode)
                                                  : "Error: " + result.error;

        juce::MessageManager::callAsync ([safe, text]
        {
            if (safe != nullptr)
            {
                safe->resultLabel.setText (text, juce::dontSendNotification);
                safe->probeButton.setEnabled (true);
            }
        });
    }).detach();
}
} // namespace iaq8
