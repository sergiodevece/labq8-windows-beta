#include "plugin/DebugEditor.h"

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
        result.error = stream == nullptr ? "sin respuesta (red bloqueada o sin conexión)" : "sin código HTTP";

    return result;
}

class DebugEditor::ProbeThread final : public juce::Thread
{
public:
    explicit ProbeThread (DebugEditor& e) : juce::Thread ("IAQ8 network probe"), editor (&e) {}
    ~ProbeThread() override { stopThread (15000); }

    void run() override
    {
        const auto result = probeHttpStatus (probeUrl, 10000);

        const auto text = result.statusCode != 0 ? "HTTP " + juce::String (result.statusCode)
                                                  : "Error: " + result.error;

        juce::MessageManager::callAsync ([safe = editor, text]
        {
            if (safe != nullptr)
                safe->resultLabel.setText (text, juce::dontSendNotification);

            if (safe != nullptr)
                safe->probeButton.setEnabled (true);
        });
    }

private:
    juce::Component::SafePointer<DebugEditor> editor;
};

DebugEditor::DebugEditor (Iaq8AudioProcessor& p) : AudioProcessorEditor (p)
{
    addAndMakeVisible (probeButton);
    addAndMakeVisible (resultLabel);
    resultLabel.setJustificationType (juce::Justification::centred);
    resultLabel.setText ("Sin probar", juce::dontSendNotification);
    probeButton.onClick = [this] { startProbe(); };
    setSize (360, 140);
}

DebugEditor::~DebugEditor()
{
    probeThread.reset(); // joins the worker; its pending callAsync is guarded by the SafePointer
}

void DebugEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    probeButton.setBounds (area.removeFromTop (40));
    area.removeFromTop (10);
    resultLabel.setBounds (area);
}

void DebugEditor::startProbe()
{
    probeThread.reset();
    probeButton.setEnabled (false);
    resultLabel.setText ("Probando...", juce::dontSendNotification);
    probeThread = std::make_unique<ProbeThread> (*this);
    probeThread->startThread();
}
} // namespace iaq8
