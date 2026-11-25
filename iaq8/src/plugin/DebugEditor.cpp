#include "plugin/DebugEditor.h"

#include "assist/Settings.h"

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

namespace
{
juce::String ui (const char* utf8) { return juce::String (juce::CharPointer_UTF8 (utf8)); }
juce::String ui (const std::string& utf8) { return juce::String::fromUTF8 (utf8.data(), (int) utf8.size()); }

const char* stageText (assist::Session::Stage stage)
{
    using Stage = assist::Session::Stage;
    switch (stage)
    {
        case Stage::askSource: return "Fuente";
        case Stage::listening: return "Escuchando";
        case Stage::askGoal:   return "Objetivo";
        case Stage::waiting:   return "Esperando respuesta";
        case Stage::proposal:  return "Propuesta";
        case Stage::applied:   return "Aplicado";
        case Stage::offline:   return "Sin conexi\xc3\xb3n";
    }
    return "";
}
} // namespace

DebugEditor::DebugEditor (Iaq8AudioProcessor& p)
    : AudioProcessorEditor (p), iaq8Processor (p), session (p.getSession())
{
    log.setMultiLine (true, true);
    log.setReadOnly (true);
    log.setCaretVisible (false);
    log.setComponentID ("log");
    addAndMakeVisible (log);

    for (auto* label : { &notice, &status, &meters, &codeStatus, &resultLabel })
        addAndMakeVisible (*label);
    notice.setComponentID ("notice");
    notice.setColour (juce::Label::textColourId, juce::Colours::orange);
    status.setComponentID ("status");

    for (size_t i = 0; i < quickButtons.size(); ++i)
    {
        auto& b = quickButtons[i];
        b.setComponentID ("quick" + juce::String ((int) i));
        b.onClick = [this, &b] { session.say (b.getButtonText().toStdString()); };
        addChildComponent (b);
    }

    input.setComponentID ("input");
    input.setTextToShowWhenEmpty (ui ("Escribe aqu\xc3\xad..."), juce::Colours::grey);
    input.onReturnKey = [this] { sendInput(); };
    input.setInputRestrictions (assist::Session::maxTextChars); // the session caps it too
    addAndMakeVisible (input);

    const auto button = [this] (juce::TextButton& b, const char* id, const char* text, std::function<void()> onClick)
    {
        b.setComponentID (id);
        b.setButtonText (ui (text));
        b.onClick = std::move (onClick);
        addAndMakeVisible (b);
    };
    button (sendButton, "send", "Enviar", [this] { sendInput(); });
    button (listenButton, "listen", "Escuchar", [this] { session.listenAgain(); });
    button (compareButton, "compare", "Antes/Despu\xc3\xa9s", [this]
    {
        session.setListen (session.getListen() == dsp::Listen::preview ? dsp::Listen::applied : dsp::Listen::preview);
    });
    button (applyButton, "apply", "Aplicar", [this] { session.apply(); });
    button (undoButton, "undo", "Deshacer", [this] { session.undo(); });
    button (startOverButton, "startOver", "Empezar de nuevo", [this] { session.startOver(); });
    button (saveCodeButton, "saveCode", "Guardar c\xc3\xb3" "digo", [this] { saveAccessCode(); });
    button (probeButton, "probe", "Probar conexi\xc3\xb3n", [this] { startProbe(); });

    accessCode.setComponentID ("accessCode");
    accessCode.setPasswordCharacter ((juce::juce_wchar) 0x2022);
    accessCode.setTextToShowWhenEmpty (ui ("C\xc3\xb3" "digo de acceso"), juce::Colours::grey);
    accessCode.onReturnKey = [this] { saveAccessCode(); };
    addAndMakeVisible (accessCode);
    codeStatus.setText (assist::Settings::accessCode().isNotEmpty() ? ui ("C\xc3\xb3" "digo guardado") : ui ("Sin c\xc3\xb3" "digo"),
                        juce::dontSendNotification);

    resultLabel.setText ("Sin probar", juce::dontSendNotification);

    session.addListener (this);
    sessionChanged();
    startTimerHz (10);
    setSize (720, 640);
}

DebugEditor::~DebugEditor() // never waits for a probe in flight
{
    stopTimer();
    session.removeListener (this);
}

void DebugEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void DebugEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto top = area.removeFromTop (24);
    status.setBounds (top.removeFromLeft (top.getWidth() / 2));
    meters.setBounds (top);
    notice.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);

    auto bottom = area.removeFromBottom (28);
    probeButton.setBounds (bottom.removeFromLeft (150));
    bottom.removeFromLeft (8);
    resultLabel.setBounds (bottom);
    area.removeFromBottom (6);

    auto code = area.removeFromBottom (28);
    accessCode.setBounds (code.removeFromLeft (220));
    code.removeFromLeft (8);
    saveCodeButton.setBounds (code.removeFromLeft (130));
    code.removeFromLeft (8);
    codeStatus.setBounds (code);
    area.removeFromBottom (6);

    auto actions = area.removeFromBottom (28);
    const auto actionWidth = actions.getWidth() / 5;
    for (auto* b : { &listenButton, &compareButton, &applyButton, &undoButton, &startOverButton })
        b->setBounds (actions.removeFromLeft (actionWidth).reduced (2, 0));
    area.removeFromBottom (6);

    auto line = area.removeFromBottom (28);
    sendButton.setBounds (line.removeFromRight (90));
    line.removeFromRight (6);
    input.setBounds (line);
    area.removeFromBottom (6);

    auto quick = area.removeFromBottom (56);
    auto row1 = quick.removeFromTop (28), row2 = quick;
    const auto quickWidth = row1.getWidth() / 4;
    for (size_t i = 0; i < quickButtons.size(); ++i)
        quickButtons[i].setBounds ((i < 4 ? row1 : row2).removeFromLeft (quickWidth).reduced (2));
    area.removeFromBottom (6);

    log.setBounds (area);
}

void DebugEditor::sessionChanged()
{
    juce::String text;
    for (const auto& m : session.getMessages())
        text << (m.role == assist::Message::Role::user ? "> " : "IA: ") << ui (m.text) << "\n\n";
    for (const auto& line : session.getLimitAdjustments())
        text << "[" << ui ("l\xc3\xadmite") << "] " << ui (line) << "\n";
    if (text != log.getText())
    {
        log.setText (text, false);
        log.moveCaretToEnd();
    }

    notice.setText (ui (session.getNotice()), juce::dontSendNotification);

    const auto replies = session.getQuickReplies();
    for (size_t i = 0; i < quickButtons.size(); ++i)
    {
        quickButtons[i].setVisible (i < replies.size());
        quickButtons[i].setButtonText (i < replies.size() ? ui (replies[i]) : juce::String());
    }

    const auto stage = session.getStage();
    sendButton.setEnabled (stage != assist::Session::Stage::waiting);
    listenButton.setEnabled (session.getSource().has_value());
    compareButton.setEnabled (session.canApply());
    compareButton.setButtonText (session.getListen() == dsp::Listen::preview ? ui ("Antes/Despu\xc3\xa9s: despu\xc3\xa9s")
                                                                             : ui ("Antes/Despu\xc3\xa9s: antes"));
    applyButton.setEnabled (session.canApply());
    undoButton.setEnabled (session.canUndo());
    refreshMeters();
}

void DebugEditor::timerCallback()
{
    refreshMeters();
}

void DebugEditor::refreshMeters()
{
    const auto& capture = iaq8Processor.getCapture();
    status.setText (ui ("Estado: ") + ui (stageText (session.getStage()))
                        + (session.getStage() == assist::Session::Stage::listening
                               ? " (" + juce::String (juce::roundToInt (session.getListenProgress() * 100.0)) + " %)"
                               : juce::String()),
                    juce::dontSendNotification);
    meters.setText (ui ("Compensaci\xc3\xb3n: ") + juce::String (iaq8Processor.getCompensationDb(), 1) + " dB   "
                        + "Escuchado: " + juce::String (capture.getCapturedSeconds(), 1) + " s",
                    juce::dontSendNotification);
}

void DebugEditor::sendInput()
{
    const auto text = input.getText().trim();
    if (text.isEmpty() || session.getStage() == assist::Session::Stage::waiting)
        return;
    input.clear();
    session.say (text.toStdString());
}

void DebugEditor::saveAccessCode()
{
    const auto code = accessCode.getText().trim();
    accessCode.clear(); // never kept in the UI, never logged
    if (code.isEmpty())
        return;
    assist::Settings::setAccessCode (code);
    codeStatus.setText (ui ("C\xc3\xb3" "digo guardado"), juce::dontSendNotification);
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
