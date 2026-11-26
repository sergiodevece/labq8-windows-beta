#pragma once

#include "assist/Session.h"
#include "ui/Iaq8LookAndFeel.h"
#include "ui/Iaq8Preferences.h"
#include "ui/LevelMeterView.h"
#include "ui/PhosphorScreen.h"
#include "ui/RackKey.h"
#include "ui/TopRow.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace iaq8
{
class Iaq8AudioProcessor;
}

namespace iaq8::ui
{
// The IAQ8 editor (Plan 3): the approved mockup laid out in its 1672 x 941
// reference space (ui/Iaq8Geometry.h) and scaled as a whole, as LABQ8's
// editor.
//
// Layers:
//   - background: iaq8_chassis.png, resampled once per size and density;
//   - content: a component in reference coordinates, scaled with an
//     AffineTransform; it paints the worn silkscreen (top row labels, lamp
//     captions and the text under the logo) from images cached per scale.
//     The phosphor screen (over geometry::crt, hiding LABQ8's screen in the
//     chassis), the meters and, in later tasks, the keys and top row
//     controls are its children.
//
// Assistant (Task 5): the eight keys under the screen carry the first seven
// quick replies of the Session (a key without one is off) and, on the
// eighth, VER DETALLE (the screen's band table, available with a proposal or
// something applied). The lower panel has the ASSISTANT head, the command
// line (Enter sends), the privacy hint, the SOURCE / LISTENED / CHANGES /
// LEVEL MATCH boxes and the ESCUCHAR, ENVIAR, DESHACER and APLICAR keys.
// refreshFromSession() pulls everything they show from the Session and the
// processor; it runs at construction, after every key or Enter and (Task 7)
// whenever the Session says it changed: the editor is its listener from
// construction to destruction, so an editor opened at any moment (in the
// middle of a request or a listening) shows the Session's state, and one
// closed in the middle loses nothing.
//
// Screen (Task 7): the status bar ("IAQ8 OS 1.0", "FUENTE: …", "ESCUCHADO:
// N S", "PROPUESTA n · DESPUÉS / ANTES" or "EQ APLICADA"), the applied curve
// and the proposal's, the conversation with the Session's notices, and the
// listening bar. A 15 Hz timer runs only while listening or while a request
// is in flight, for the progress; it stops at rest. It only feeds values that
// change what is shown (whole seconds, the bar's blocks), so a tick with
// nothing new rebuilds nothing. The command line's caret blinks on its own
// 500 ms callback.
//
// Top row (Task 6, ui/TopRow): the SOURCE drop-down (Session::chooseSource;
// "Otro" first asks for a note on the command line, "Describe la fuente…",
// and the next Enter with text chooses it), the ANTES / DESPUÉS faces
// (Session::setListen) and the ONLINE, LISTEN and PROPOSAL lamps.
//
// Settings: a click on the panel's top-left screw or on the logo (or a
// right-click anywhere on the chassis) opens the settings menu: zoom, the
// assistant's access code (an asynchronous AlertWindow with a masked field,
// saved only through assist::Settings, never in the DAW state and never
// shown back or logged) and "Acerca de IAQ8".
//
// Zoom: the six sizes of LABQ8 (40 to 140 %, 60 % by default), only those
// that fit the screen, remembered in IAQ8's own preferences file. There is
// no resize corner: the host follows the editor.
//
// Message thread only. The editor never blocks and never touches the network.
class Iaq8Editor final : public juce::AudioProcessorEditor,
                         private assist::Session::Listener,
                         private juce::Timer
{
public:
    static constexpr auto zoomChoices = preferences::zoomChoices;

    // Room left on the screen for the host's window around the editor (title
    // bar and plug-in header of Logic Pro), in points.
    static constexpr int hostWindowAllowanceWidth = 0, hostWindowAllowanceHeight = 80;

    // Settings menu: items 1..6 are zoomChoices[n - 1]; then these.
    static constexpr int accessCodeItem = 101, aboutItem = 102;

    // Set (true) on a child of the content that shows its own menu on a
    // right-click, so the settings menu does not open as well.
    static constexpr const char* ownsPopupMenuProperty = "iaq8OwnsPopupMenu";

    // The lower panel's keys.
    enum class PanelKey { listen, send, undo, apply };
    // Quick replies shown on the keys under the screen (the eighth is VER DETALLE).
    static constexpr int quickReplyKeyCount = 7;

    explicit Iaq8Editor (Iaq8AudioProcessor& processor);
    ~Iaq8Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Resizes the editor (the AU / VST3 wrapper resizes the host's window)
    // and stores the choice. Only the six choices, and only those that fit
    // the screen (40 % always does); returns false and changes nothing
    // otherwise.
    bool setZoom (float zoom);
    float getScale() const noexcept;

    juce::PopupMenu createSettingsMenu() const;
    void handleSettingsMenuResult (int itemId);
    void showSettingsMenu (juce::Component& target);

    // Whether the editor at `zoom`, plus the host window allowance, fits in
    // `available` (a display's area without menu bar and Dock).
    static bool zoomFits (float zoom, juce::Rectangle<int> available);
    // Usable area of the display the editor is on (or, before it is shown,
    // of the display under the mouse, where the host opens it).
    juce::Rectangle<int> getAvailableScreenArea() const;
    // Tests and tools: pretend every display has this area (empty: real displays).
    static void setScreenAreaForTesting (juce::Rectangle<int> area);

    // Pulls the conversation, the quick replies, the keys' states and the
    // panel's values from the Session and the processor. The setters it
    // calls compare, so repeating it with nothing new repaints nothing.
    void refreshFromSession();
    // Sends the command line's text (Enter, ENVIAR) and clears it. Empty
    // text sends nothing; while a request is in flight the text stays.
    void sendCommandLine();

    // The 15 Hz timer: runs only while listening or while a request is in
    // flight (Session stages listening and waiting), stops at rest.
    static constexpr int refreshTimerHz = 15;
    bool isRefreshTimerRunning() const noexcept { return isTimerRunning(); }
    // What a tick of that timer does (public for tests): the listening bar,
    // the seconds listened and the status bar, each only if it changed.
    void refreshTick();

    // A reply as a key shows it: capitals, in two lines balanced by length.
    static juce::String keyLabel (const juce::String& reply);
    // LEVEL MATCH, one decimal with its sign: "+0.8 dB", "-1.2 dB" (with a true minus, U+2212), "0.0 dB".
    static juce::String formatLevelMatch (double db);

    // Tests and snapshots.
    RackKey& getQuickKey (int index); // 0..7
    RackKey& getPanelKey (PanelKey);
    juce::TextEditor& getCommandLine() noexcept;
    juce::String getValueText (int box) const; // 0 SOURCE, 1 LISTENED, 2 CHANGES, 3 LEVEL MATCH
    TopRow& getTopRow() noexcept { return *topRow; }
    // "Otro" was chosen in SOURCE: the command line's next text is its note.
    bool isAwaitingSourceNote() const noexcept { return awaitingSourceNote; }
    juce::Component& getContent() noexcept { return *content; }
    juce::Component& getSettingsHotSpot (int index); // 0: top-left screw, 1: logo
    juce::AlertWindow* getOpenDialog() const noexcept { return dialog.get(); }
    Iaq8AudioProcessor& getIaq8Processor() noexcept { return iaq8Processor; }
    PhosphorScreen& getScreen() noexcept { return *screen; }
    // The listener that turns right-clicks on the chassis into the settings
    // menu (registered on the content and all its children), and how many
    // times the menu was asked for.
    juce::MouseListener& getPopupMenuListener() noexcept;
    int getSettingsMenuRequests() const noexcept { return settingsMenuRequests; }

private:
    using DialogHandler = std::function<void (int result, juce::AlertWindow&)>;
    void openDialog (std::unique_ptr<juce::AlertWindow> window, DialogHandler onClose);
    void dialogFinished (const juce::AlertWindow* window, int result);
    void showAccessCodeDialog();
    void showAboutDialog();
    juce::PopupMenu createZoomMenu() const;

    class PopupMenuForwarder;
    class CommandLine;
    class ValueBox;

    void createAssistantControls();
    void createTopRow();
    void sourceChosen (assist::Source);
    void beginSourceNote();
    void endSourceNote();
    void updateScreenLines();
    void refreshMoving();      // the parts the 15 Hz timer moves
    void updateRefreshTimer(); // runs only while listening or waiting

    void sessionChanged() override { refreshFromSession(); }
    void timerCallback() override { refreshTick(); }

    Iaq8AudioProcessor& iaq8Processor;
    Iaq8LookAndFeel lookAndFeel;

    std::unique_ptr<juce::Component> content;
    std::array<std::unique_ptr<juce::Component>, 2> hotSpots;
    std::unique_ptr<labq8::ui::LevelMeterView> meters;
    std::unique_ptr<PhosphorScreen> screen;
    std::array<std::unique_ptr<RackKey>, 8> quickKeys;
    std::array<juce::String, quickReplyKeyCount> quickReplies; // the text each key says
    std::array<std::unique_ptr<RackKey>, 4> panelKeys;          // PanelKey order
    std::unique_ptr<CommandLine> commandLine;
    std::array<std::unique_ptr<ValueBox>, 4> valueBoxes;
    std::unique_ptr<TopRow> topRow;

    // Where the command line's text goes; empty: Session::say. A mode such
    // as the source note of "Otro" (Task 6) sets it for the next Enter.
    std::function<void (const juce::String&)> commandTextTarget;
    // The source note mode of "Otro": on until a note is sent, another source
    // is picked, Escape, any other action on the conversation (quick reply,
    // ESCUCHAR, DESHACER, APLICAR), or the conversation moves on by itself
    // (its source, stage or messages change).
    bool awaitingSourceNote = false;
    std::optional<assist::Source> sourceWhenNoteBegan;
    assist::Session::Stage stageWhenNoteBegan {};
    size_t messagesWhenNoteBegan = 0;

    // Session notices shown on the screen as system lines, each after the
    // messages there were when it appeared (so the lines only grow).
    struct ShownNotice
    {
        size_t afterMessages;
        juce::String text;
    };
    std::vector<ShownNotice> shownNotices;
    juce::String lastNotice;
    size_t messagesSeen = 0;
    std::optional<std::uint64_t> conversationSeen; // Session::getConversationNumber() last shown
    std::unique_ptr<juce::MouseListener> popupForwarder;
    int settingsMenuRequests = 0;
    std::unique_ptr<juce::AlertWindow> dialog;
    DialogHandler onDialogClosed;
    juce::TooltipWindow tooltips { this, 600 };
    juce::Image scaledBackground;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Iaq8Editor)
};
} // namespace iaq8::ui
