#pragma once

#include "assist/Source.h"
#include "dsp/AssistEngine.h"
#include "ui/Iaq8LookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace iaq8::ui
{
// The editor's top row (Task 6), after the approved mockup (iaq8-mockup-v2):
//
//   SOURCE   a drop-down (LABQ8's style, drawn by Iaq8LookAndFeel) with the
//            seven sources in the Session's Spanish words, in a recessed tray.
//   COMPARE  two faces, ANTES (the applied EQ) and DESPUÉS (the proposal's
//            preview), on LABQ8's mode_face_lit / mode_face_unlit sprites; the
//            lit one is what plays. DESPUÉS is off without a proposal.
//   STATUS   three lamps: ONLINE (green), LISTEN (green), PROPOSAL (orange).
//            Their captions are the chassis silkscreen's (Iaq8Editor).
//
// The row only shows a State and reports what the user picks (onSourceChosen,
// onListenChosen); the editor talks to the Session. setState compares, so a
// state with nothing new repaints nothing; it never calls the callbacks.
// The component spans the row in the content's reference coordinates and
// takes no clicks itself (only its controls do); a right-click on any of them
// reaches the editor's settings menu (the combo opens its list only on a left
// click, the faces ignore right-clicks).
class TopRow final : public juce::Component
{
public:
    enum class Lamp { online, listen, proposal };

    struct State
    {
        std::optional<assist::Source> source; // shown in the combo; none: nothing selected
        dsp::Listen listen = dsp::Listen::applied;
        bool canPreview = false;              // DESPUÉS on (there is a proposal)
        bool online = false, listening = false, proposal = false;

        bool operator== (const State&) const = default;
    };

    TopRow();
    ~TopRow() override;

    void setState (const State&);
    const State& getState() const noexcept { return state; }

    std::function<void (assist::Source)> onSourceChosen;
    std::function<void (dsp::Listen)> onListenChosen;

    // Combo item ids: 1..7 in the Session's order (Voz ... Otro).
    static int comboIdFor (assist::Source);
    static std::optional<assist::Source> sourceForComboId (int id);
    // The text when no source is chosen yet.
    static juce::String noSourceText();

    void paint (juce::Graphics&) override;

    // Tests and snapshots.
    juce::ComboBox& getSourceCombo() noexcept { return combo; }
    juce::Button& getFace (dsp::Listen);
    bool isFaceLit (dsp::Listen) const;
    bool isLampOn (Lamp) const;

private:
    class Face;
    class StatusLamp;

    juce::Rectangle<float> local (juce::Rectangle<float> reference) const;

    State state;
    juce::ComboBox combo;
    std::array<std::unique_ptr<Face>, 2> faces;       // ANTES, DESPUÉS
    std::array<std::unique_ptr<StatusLamp>, 3> lamps; // Lamp order
    CachedLayer trays;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopRow)
};
} // namespace iaq8::ui
