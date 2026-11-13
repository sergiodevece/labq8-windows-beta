// E4c: the editor with the three banks (docs/MIDSIDE_E4C_AUDIT.md, T1-T11)
// and the Mid/Side copy commands. The editor only re-points its controls at
// the bank shown; it never writes a parameter except on an explicit edit.

#pragma clang diagnostic ignored "-Wfloat-equal" // bit-exactness is a tested property

#include "TestHarness.h"
#include "TestSignals.h"

#include "parameters/ParameterIDs.h"
#include "plugin/PluginProcessor.h"
#include "ui/LabQ8Editor.h"
#include "ui/UiGeometry.h"
#include "ui/UiPreferences.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <random>
#include <set>

using labq8::LabQ8AudioProcessor;
using labq8::test::str;
using labq8::ui::LabQ8Editor;
using labq8::ui::PanelControl;
using Bank = labq8::params::Bank;
namespace params = labq8::params;
namespace geo = labq8::ui::geometry;

namespace
{
const bool preferencesInMemory = []
{
    labq8::ui::preferences::setStorageFileForTesting ({});
    LabQ8Editor::setScreenAreaForTesting ({ 0, 0, 4000, 3000 });
    return true;
}();

constexpr PanelControl allControls[] = { PanelControl::frequency, PanelControl::gain, PanelControl::q, PanelControl::threshold,
                                         PanelControl::range, PanelControl::attack, PanelControl::release };
const char* bankName (Bank b) { return b == Bank::mid ? "MID" : b == Bank::side ? "SIDE" : "STEREO"; }

std::unique_ptr<LabQ8Editor> openEditor (LabQ8AudioProcessor& p)
{
    return std::unique_ptr<LabQ8Editor> (dynamic_cast<LabQ8Editor*> (p.createEditor()));
}

juce::RangedAudioParameter& param (LabQ8AudioProcessor& p, const char* id) { return *p.getValueTreeState().getParameter (id); }
float raw (LabQ8AudioProcessor& p, const char* id) { return p.getValueTreeState().getRawParameterValue (id)->load(); }
void hostSet (LabQ8AudioProcessor& p, const char* id, float value) { auto& q = param (p, id); q.setValueNotifyingHost (q.convertTo0to1 (value)); }
void setMode (LabQ8AudioProcessor& p, params::ProcMode m) { param (p, params::procModeID).setValueNotifyingHost (m == params::ProcMode::midSide ? 1.0f : 0.0f); }

std::vector<float> allValues (LabQ8AudioProcessor& p)
{
    std::vector<float> v;
    for (auto* q : p.getParameters())
        v.push_back (q->getValue());
    return v;
}

std::set<juce::String> changedIds (LabQ8AudioProcessor& p, const std::vector<float>& before)
{
    std::set<juce::String> ids;
    const auto after = allValues (p);
    for (size_t i = 0; i < after.size(); ++i)
        if (after[i] != before[i])
            ids.insert (dynamic_cast<juce::RangedAudioParameter*> (p.getParameters()[(int) i])->paramID);
    return ids;
}

void randomise (LabQ8AudioProcessor& p, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    for (auto* q : p.getParameters())
        q->setValueNotifyingHost (u (rng));
}

const char* idOf (Bank bank, int band, PanelControl c)
{
    const auto& s = params::bankOf (bank).bands[(size_t) band];
    const auto& d = params::bankOf (bank).dynamics[(size_t) band];
    switch (c)
    {
        case PanelControl::frequency: return s.frequency;
        case PanelControl::gain:      return s.gain;
        case PanelControl::q:         return s.q;
        case PanelControl::threshold: return d.threshold;
        case PanelControl::range:     return d.range;
        case PanelControl::attack:    return d.attack;
        case PanelControl::release:   return d.release;
    }
    return s.frequency;
}

// Counts value changes and gestures of every parameter (what a host records).
struct HostRecorder final : juce::AudioProcessorParameter::Listener
{
    explicit HostRecorder (LabQ8AudioProcessor& proc) : p (proc)
    {
        for (auto* q : p.getParameters())
            q->addListener (this);
    }
    ~HostRecorder() override
    {
        for (auto* q : p.getParameters())
            q->removeListener (this);
    }
    void parameterValueChanged (int index, float) override { ++changes[index]; }
    void parameterGestureChanged (int index, bool starting) override { (starting ? begins : ends)[index]++; }
    int totalBegins() const { int n = 0; for (auto& [k, v] : begins) n += v; return n; }
    int totalEnds() const { int n = 0; for (auto& [k, v] : ends) n += v; return n; }
    bool balanced() const { return begins == ends; }
    void clear() { changes.clear(); begins.clear(); ends.clear(); }

    LabQ8AudioProcessor& p;
    std::map<int, int> changes, begins, ends;
};

juce::MouseEvent mouseEvent (juce::Component& c, juce::Point<float> at, juce::Point<float> downAt, juce::ModifierKeys mods)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (source, at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downAt, now, 1, false);
}

void click (juce::Component& c)
{
    const auto centre = c.getLocalBounds().getCentre().toFloat();
    c.mouseDown (mouseEvent (c, centre, centre, juce::ModifierKeys::leftButtonModifier));
    c.mouseUp (mouseEvent (c, centre, centre, {}));
}

void dragOn (juce::Component& c, juce::Point<float> from, juce::Point<float> to, int steps = 4)
{
    const auto left = juce::ModifierKeys::leftButtonModifier;
    c.mouseDown (mouseEvent (c, from, from, left));
    for (int i = 1; i <= steps; ++i)
        c.mouseDrag (mouseEvent (c, from + (to - from) * ((float) i / (float) steps), from, left));
    c.mouseUp (mouseEvent (c, to, from, {}));
}

juce::Point<float> onScreen (LabQ8Editor& e, juce::Point<float> reference) { return reference - e.getScreen().getPosition().toFloat(); }

// The editor showing `bank` (mode set accordingly).
std::unique_ptr<LabQ8Editor> showBank (LabQ8AudioProcessor& p, Bank bank)
{
    setMode (p, bank == Bank::stereo ? params::ProcMode::stereo : params::ProcMode::midSide);
    auto e = openEditor (p);
    if (bank != Bank::stereo)
        e->setEditBank (bank);
    return e;
}

// The panel, the band buttons and the screen show exactly the bank's values.
bool panelShows (LabQ8AudioProcessor& p, LabQ8Editor& e, Bank bank, std::string& why)
{
    const auto band = e.getSelectedBand();
    for (const auto c : allControls)
        if ((float) e.getKnob (c).getValue() != raw (p, idOf (bank, band, c)))
            return why = str ("knob ", (int) c, " shows ", e.getKnob (c).getValue(), " not ", raw (p, idOf (bank, band, c))), false;
    const auto ids = params::bankOf (bank);
    if (e.isDynamicSwitchOn() != (raw (p, ids.dynamics[(size_t) band].enabled) >= 0.5f))
        return why = "dynamic switch", false;
    if (e.isBypassShown() != (raw (p, ids.bands[(size_t) band].bypass) >= 0.5f))
        return why = "bypass indicator", false;
    const auto screen = e.getScreenBandsForTesting();
    for (int b = 0; b < 8; ++b)
    {
        if (e.isBandButtonOn (b) == (raw (p, ids.bands[(size_t) b].bypass) >= 0.5f))
            return why = str ("band button ", b + 1), false;
        if ((float) screen[(size_t) b].frequencyHz != raw (p, ids.bands[(size_t) b].frequency) || (float) screen[(size_t) b].gainDb != raw (p, ids.bands[(size_t) b].gain))
            return why = str ("screen band ", b + 1), false;
    }
    return true;
}

using Stereo = std::array<std::vector<float>, 2>;

Stereo render (LabQ8AudioProcessor& p, int channels, int blocks, const std::function<void (int)>& before = {})
{
    juce::AudioProcessor::BusesLayout layout;
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    p.setBusesLayout (layout);
    p.prepareToPlay (48000.0, 480);
    Stereo out;
    juce::MidiBuffer midi;
    for (int k = 0; k < blocks; ++k)
    {
        if (before)
            before (k);
        juce::AudioBuffer<float> buffer (channels, 480);
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto n = labq8::test::signals::makeNoise (480, (std::uint32_t) (k * 2 + ch + 1), 0.3f);
            for (int i = 0; i < 480; ++i)
                buffer.setSample (ch, i, n[(size_t) i] + 0.3f * (float) std::sin (0.02 * (k * 480 + i) * (ch + 1)));
        }
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < channels; ++ch)
            out[(size_t) ch].insert (out[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + 480);
    }
    return out;
}

const Bank kBanks[] = { Bank::stereo, Bank::mid, Bank::side };
} // namespace

//==============================================================================
LQ_TEST ("e4c T1: in each bank (STEREO, MID, SIDE) every knob, reset, type, dynamic switch, band button, node and Q handle writes exactly the expected IDs of that bank")
{
    for (const auto bank : kBanks)
    {
        LabQ8AudioProcessor p;
        randomise (p, 11 + (unsigned) bank);
        // Bell on the tested bands so the dynamic switch and gain are live.
        for (const int band : { 0, 3, 7 })
            hostSet (p, params::bankOf (bank).bands[(size_t) band].type, 0.0f);
        auto e = showBank (p, bank);
        LQ_CHECK (e->getShownBank() == bank, str (bankName (bank), ": shown bank"));

        for (const int band : { 0, 3, 7 })
        {
            e->selectBand (band);
            const auto ids = params::bankOf (bank);
            auto expectOnly = [&] (const std::set<juce::String>& got, std::set<juce::String> want, const std::string& what)
            {
                LQ_CHECK (got == want, str (bankName (bank), " band ", band + 1, " ", what, ": changed {", juce::StringArray ([&] { juce::StringArray a; for (auto& x : got) a.add (x); return a; }()).joinIntoString (","),
                                            "} expected {", juce::StringArray ([&] { juce::StringArray a; for (auto& x : want) a.add (x); return a; }()).joinIntoString (","), "}"));
            };

            for (const auto c : allControls)
            {
                auto& knob = e->getKnob (c);
                auto before = allValues (p);
                const auto target = knob.valueToProportionOfLength (knob.getValue()) > 0.5 ? 0.23 : 0.77;
                knob.setValue (knob.proportionOfLengthToValue (target), juce::sendNotificationSync);
                expectOnly (changedIds (p, before), { idOf (bank, band, c) }, str ("knob ", (int) c));
                before = allValues (p);
                knob.setValue (knob.getDoubleClickReturnValue(), juce::sendNotificationSync); // double-click reset
                const auto reset = changedIds (p, before);
                LQ_CHECK (reset.empty() || reset == std::set<juce::String> { idOf (bank, band, c) }, str (bankName (bank), " reset of knob ", (int) c));
            }

            auto before = allValues (p);
            e->setFilterTypeIndex (2);
            expectOnly (changedIds (p, before), { ids.bands[(size_t) band].type }, "type");
            e->setFilterTypeIndex (0);
            before = allValues (p);
            e->toggleDynamicSwitch();
            expectOnly (changedIds (p, before), { ids.dynamics[(size_t) band].enabled }, "dynamic switch");

            // Node: frequency horizontally, gain vertically (Bell).
            before = allValues (p);
            const auto node = onScreen (*e, e->getNodePosition (band));
            dragOn (e->getScreen(), node, node + juce::Point<float> (35.0f, -18.0f));
            expectOnly (changedIds (p, before), { ids.bands[(size_t) band].frequency, ids.bands[(size_t) band].gain }, "node drag");

            // Q handle of the selected band.
            e->showQHandles (band);
            before = allValues (p);
            const auto [leftHandle, rightHandle] = e->getQHandleCentres();
            dragOn (e->getScreen(), onScreen (*e, rightHandle), onScreen (*e, rightHandle) + juce::Point<float> (25.0f, 0.0f));
            expectOnly (changedIds (p, before), { ids.bands[(size_t) band].q }, "Q handle");
        }

        // The eight ON / OFF buttons: each its own band of the bank shown.
        for (int b = 0; b < 8; ++b)
        {
            const auto before = allValues (p);
            click (e->getBandButton (b));
            const auto got = changedIds (p, before);
            LQ_CHECK (got == std::set<juce::String> { params::bankOf (bank).bands[(size_t) b].bypass },
                      str (bankName (bank), " band button ", b + 1, " changed ", got.size(), " parameters"));
        }
    }
}

LQ_TEST ("e4c T2: STEREO / M/S write only proc_mode, one gesture; the lit one and MID / SIDE write nothing and open no gesture")
{
    LabQ8AudioProcessor p;
    randomise (p, 21);
    setMode (p, params::ProcMode::stereo);
    auto e = openEditor (p);
    HostRecorder rec (p);
    const auto modeIndex = params::procModeIndex;

    auto before = allValues (p);
    click (e->getModeButton (1));
    LQ_CHECK (changedIds (p, before) == std::set<juce::String> { "proc_mode" } && raw (p, params::procModeID) == 1.0f, "M/S writes proc_mode = Mid/Side");
    LQ_CHECK (rec.begins[modeIndex] == 1 && rec.ends[modeIndex] == 1 && rec.totalBegins() == 1, str ("one gesture: ", rec.totalBegins(), " begins"));
    LQ_CHECK (e->isModeButtonLit (1) && ! e->isModeButtonLit (0) && e->getShownBank() == Bank::mid, "M/S lit, MID shown");

    rec.clear();
    before = allValues (p);
    click (e->getModeButton (1));                 // already lit
    click (e->getBankButton (Bank::side));
    click (e->getBankButton (Bank::mid));
    click (e->getBankButton (Bank::side));
    LQ_CHECK (changedIds (p, before).empty() && rec.changes.empty() && rec.totalBegins() == 0, "lit mode button and MID / SIDE write nothing, no gesture");
    LQ_CHECK (e->getShownBank() == Bank::side && e->isBankButtonSelected (Bank::side) && ! e->isBankButtonSelected (Bank::mid), "SIDE selected and shown");

    before = allValues (p);
    click (e->getModeButton (0));
    LQ_CHECK (changedIds (p, before) == std::set<juce::String> { "proc_mode" } && e->getShownBank() == Bank::stereo, "STEREO writes proc_mode only; STEREO shown");
    LQ_CHECK (rec.balanced(), "gestures balanced");
}

LQ_TEST ("e4c T3: switching bank or mode from the editor changes none of the 241 values (except proc_mode by the mode buttons) and the panel shows exactly the new bank")
{
    LabQ8AudioProcessor p;
    randomise (p, 31);
    setMode (p, params::ProcMode::stereo);
    auto e = openEditor (p);
    e->selectBand (5);
    std::string why;
    LQ_CHECK (panelShows (p, *e, Bank::stereo, why), "STEREO: " + why);

    auto before = allValues (p);
    click (e->getModeButton (1));
    LQ_CHECK (panelShows (p, *e, Bank::mid, why), "MID: " + why);
    click (e->getBankButton (Bank::side));
    LQ_CHECK (panelShows (p, *e, Bank::side, why), "SIDE: " + why);
    LQ_CHECK (e->getSelectedBand() == 5, "the band number is kept (D9)");
    click (e->getBankButton (Bank::mid));
    LQ_CHECK (panelShows (p, *e, Bank::mid, why), "MID again: " + why);
    click (e->getModeButton (0));
    LQ_CHECK (panelShows (p, *e, Bank::stereo, why), "STEREO again: " + why);
    LQ_CHECK (changedIds (p, before) == std::set<juce::String> {}, "back in Stereo: all 241 values as before");
}

LQ_TEST ("e4c T4/T5: host automation of proc_mode switches the bank shown on the next refresh; automation of the other bank moves only the grey curve")
{
    LabQ8AudioProcessor p;
    randomise (p, 41);
    setMode (p, params::ProcMode::stereo);
    auto e = openEditor (p);
    e->setView (LabQ8Editor::viewEq);
    const auto before = allValues (p);

    setMode (p, params::ProcMode::midSide); // the host
    e->refreshFromParameters();
    LQ_CHECK (e->getShownBank() == Bank::mid && e->isModeButtonLit (1) && e->isBankButtonAvailable (Bank::mid), "follows the mode");
    LQ_CHECK (e->getHeaderText().startsWith ("MID  BAND"), e->getHeaderText().toStdString());
    LQ_CHECK (e->getGhostBank() == Bank::side, "SIDE drawn in grey");
    std::set<juce::String> modeOnly { "proc_mode" };
    LQ_CHECK (changedIds (p, before) == modeOnly, "the editor wrote nothing");

    // Automation of SIDE: the screen redraws (grey curve), the panel stays on MID.
    const auto screenBands = e->getScreenBandsForTesting();
    const auto knob = e->getKnob (PanelControl::gain).getValue();
    e->getContent().createComponentSnapshot (e->getContent().getLocalBounds(), true, 0.25f); // fills the curve's image cache
    const auto paints = e->getScreenPaintCount();
    e->getContent().createComponentSnapshot (e->getContent().getLocalBounds(), true, 0.25f);
    LQ_CHECK (e->getScreenPaintCount() == paints, "the cached curve is reused while nothing changes");
    hostSet (p, params::bankOf (Bank::side).bands[(size_t) e->getSelectedBand()].gain, 7.5f);
    e->refreshFromParameters();
    e->getContent().createComponentSnapshot (e->getContent().getLocalBounds(), true, 0.25f);
    LQ_CHECK (e->getScreenPaintCount() > paints, "the cached curve image was redrawn for the grey curve");
    const auto after = e->getScreenBandsForTesting();
    bool same = true;
    for (int b = 0; b < 8; ++b)
        same = same && after[(size_t) b].gainDb == screenBands[(size_t) b].gainDb && after[(size_t) b].frequencyHz == screenBands[(size_t) b].frequencyHz;
    LQ_CHECK (same && e->getKnob (PanelControl::gain).getValue() == knob, "MID nodes and panel unchanged");

    setMode (p, params::ProcMode::stereo);
    e->refreshFromParameters();
    LQ_CHECK (e->getShownBank() == Bank::stereo && ! e->getGhostBank().has_value() && ! e->isBankButtonAvailable (Bank::mid), "back to STEREO, no grey curve");
    LQ_CHECK (e->getHeaderText().startsWith ("BAND"), e->getHeaderText().toStdString());
}

LQ_TEST ("e4c T6: a mode change during a knob or node drag waits; the gesture closes on its own bank and nothing reaches the new bank")
{
    for (const bool onNode : { false, true })
    {
        LabQ8AudioProcessor p;
        randomise (p, 51);
        hostSet (p, params::bankOf (Bank::mid).bands[2].type, 0.0f);
        auto e = showBank (p, Bank::mid);
        e->selectBand (2);
        HostRecorder rec (p);
        const auto stereoBefore = [&] { std::vector<float> v; for (int i = 0; i < 80; ++i) v.push_back (p.getParameters()[i]->getValue()); return v; }();
        const auto left = juce::ModifierKeys::leftButtonModifier;

        juce::Component& target = onNode ? e->getScreen() : (juce::Component&) e->getKnob (PanelControl::threshold);
        const auto from = onNode ? onScreen (*e, e->getNodePosition (2)) : target.getLocalBounds().getCentre().toFloat();
        target.mouseDown (mouseEvent (target, from, from, left));
        target.mouseDrag (mouseEvent (target, from + juce::Point<float> (12.0f, -20.0f), from, left));

        setMode (p, params::ProcMode::stereo); // automation, mid-drag
        e->refreshFromParameters();
        LQ_CHECK (e->isBankChangePending() && e->getShownBank() == Bank::mid, "the change waits for the drag");
        target.mouseDrag (mouseEvent (target, from + juce::Point<float> (30.0f, -40.0f), from, left));
        target.mouseUp (mouseEvent (target, from + juce::Point<float> (30.0f, -40.0f), from, {}));
        e->refreshFromParameters(); // the editor's timer (a knob's pending change is applied after JUCE closes its gesture)

        LQ_CHECK (! e->isBankChangePending() && e->getShownBank() == Bank::stereo, "applied when the drag ended");
        LQ_CHECK (rec.balanced() && rec.totalBegins() >= 1, str (onNode ? "node" : "knob", ": gestures ", rec.totalBegins(), " / ", rec.totalEnds()));
        bool onlyMid = true;
        for (auto& [index, n] : rec.changes)
        {
            const auto id = dynamic_cast<juce::RangedAudioParameter*> (p.getParameters()[index])->paramID;
            onlyMid = onlyMid && (id.startsWith ("mid_b3_") || id == "proc_mode");
        }
        LQ_CHECK (onlyMid, str (onNode ? "node" : "knob", ": the drag wrote outside MID band 3"));
        std::vector<float> stereoAfter;
        for (int i = 0; i < 80; ++i)
            stereoAfter.push_back (p.getParameters()[i]->getValue());
        LQ_CHECK (stereoAfter == stereoBefore, "nothing reached the STEREO bank");
    }
}

LQ_TEST ("e4c T7: in Stereo MID / SIDE are dimmed and take no clicks; the STEREO bank stays fully editable")
{
    LabQ8AudioProcessor p;
    randomise (p, 61);
    setMode (p, params::ProcMode::stereo);
    auto e = openEditor (p);
    const auto before = allValues (p);
    const auto editBank = p.getUiEditBank();
    click (e->getBankButton (Bank::side));
    click (e->getBankButton (Bank::mid));
    LQ_CHECK (! e->isBankButtonAvailable (Bank::mid) && ! e->isBankButtonAvailable (Bank::side), "dimmed");
    LQ_CHECK (e->getShownBank() == Bank::stereo && p.getUiEditBank() == editBank && changedIds (p, before).empty(), "no effect");
    auto& knob = e->getKnob (PanelControl::gain);
    knob.setValue (knob.proportionOfLengthToValue (0.81), juce::sendNotificationSync);
    LQ_CHECK (changedIds (p, before) == std::set<juce::String> { idOf (Bank::stereo, e->getSelectedBand(), PanelControl::gain) }, "STEREO editable");
}

LQ_TEST ("e4c T8: the audio is bit-identical with and without the editor while it switches modes' views, banks, bands and views (no parameter writes)")
{
    for (const auto mode : { params::ProcMode::stereo, params::ProcMode::midSide })
    {
        LabQ8AudioProcessor plain, withEditor;
        randomise (plain, 71);
        randomise (withEditor, 71);
        setMode (plain, mode);
        setMode (withEditor, mode);
        const auto a = render (plain, 2, 200);
        auto e = openEditor (withEditor);
        int step = 0;
        const auto b = render (withEditor, 2, 200, [&] (int k)
        {
            if (k % 7 == 0) e->setEditBank (++step % 2 ? Bank::side : Bank::mid);
            if (k % 11 == 0) e->selectBand (k % 8);
            if (k % 13 == 0) e->setView (k % 4);
            if (k % 5 == 0) e->refreshFromParameters();
        });
        LQ_CHECK (a[0] == b[0] && a[1] == b[1], str ("mode ", (int) mode, ": the editor changed the audio"));
    }
}

LQ_TEST ("e4c T9: geometry: the four buttons sit on their measured frames inside the housing; the header always ends before \"Filter Type\"")
{
    LabQ8AudioProcessor p;
    setMode (p, params::ProcMode::midSide);
    auto e = openEditor (p);
    auto rect = [] (const geo::Rect& r) { return juce::Rectangle<int> ((int) r.x, (int) r.y, (int) r.w, (int) r.h); };
    LQ_CHECK (e->getModeButton (0).getBounds() == rect (geo::modeButtonStereo) && e->getModeButton (1).getBounds() == rect (geo::modeButtonMidSide), "mode buttons");
    LQ_CHECK (e->getBankButton (Bank::mid).getBounds() == rect (geo::bankButtonMid) && e->getBankButton (Bank::side).getBounds() == rect (geo::bankButtonSide), "bank buttons");
    LQ_CHECK (! e->getBankButton (Bank::mid).getBounds().intersects (e->getBankButton (Bank::side).getBounds())
              && e->getBankButton (Bank::side).getRight() <= 760 && e->getBankButton (Bank::mid).getX() >= 662, "MID and SIDE inside the old L/R slot, apart");
    float widest = 0.0f;
    for (const auto bank : { Bank::mid, Bank::side })
    {
        e->setEditBank (bank);
        for (int band = 0; band < 8; ++band)
        {
            e->selectBand (band);
            widest = std::max (widest, e->getHeaderRight());
            LQ_CHECK (e->isHeaderTextInside(), str (bankName (bank), " band ", band + 1, ": header text clipped (", e->getHeaderText(), ")"));
        }
    }
    LQ_CHECK (widest < geo::filterTypeLabelLeft - 10.0f, str ("header ends at x ", widest, " (Filter Type at ", geo::filterTypeLabelLeft, ")"));
    ctx.note (str ("widest header (\"SIDE  BAND 8\") ends at x ", widest));
}

LQ_TEST ("e4c T10: the edited bank survives closing the editor; a new instance edits MID; restored v4 / v5 sessions show the bank of their mode")
{
    LabQ8AudioProcessor p;
    auto e = showBank (p, Bank::side);
    e.reset();
    e = openEditor (p);
    LQ_CHECK (e->getShownBank() == Bank::side, "SIDE kept per instance");

    LabQ8AudioProcessor fresh;
    LQ_CHECK (fresh.getUiEditBank() == (int) Bank::mid, "a new instance edits MID");

    auto loadFixture = [] (LabQ8AudioProcessor& q, const char* name)
    {
        juce::MemoryBlock block;
        juce::File (LABQ8_TEST_FIXTURES_DIR).getChildFile (name).loadFileAsData (block);
        q.setStateInformation (block.getData(), (int) block.getSize());
    };
    LabQ8AudioProcessor v5;
    loadFixture (v5, "e4a_custom_v5.bin"); // Mid/Side
    auto e5 = openEditor (v5);
    std::string why;
    LQ_CHECK (e5->getShownBank() == Bank::mid && panelShows (v5, *e5, Bank::mid, why), "v5 Mid/Side session opens on MID " + why);

    LabQ8AudioProcessor v4;
    loadFixture (v4, "ui2c_custom.bin");
    auto e4 = openEditor (v4);
    LQ_CHECK (e4->getShownBank() == Bank::stereo && panelShows (v4, *e4, Bank::stereo, why), "v4 session opens on STEREO " + why);

    // Loading a session with the editor open: it follows the restored mode.
    loadFixture (v4, "e4a_custom_v5.bin");
    e4->refreshFromParameters();
    LQ_CHECK (e4->getShownBank() == Bank::mid && panelShows (v4, *e4, Bank::mid, why), "follows a session loaded while open " + why);
}

LQ_TEST ("e4c T11: on a mono track in M/S, SIDE can be selected and edited (no blocking); the engine keeps SIDE inaudible")
{
    LabQ8AudioProcessor p;
    juce::AudioProcessor::BusesLayout mono;
    mono.inputBuses.add (juce::AudioChannelSet::mono());
    mono.outputBuses.add (juce::AudioChannelSet::mono());
    p.setBusesLayout (mono);
    randomise (p, 81);
    setMode (p, params::ProcMode::midSide);
    auto e = openEditor (p);
    LQ_CHECK (e->isBankButtonAvailable (Bank::side), "SIDE available on a mono track");
    click (e->getBankButton (Bank::side));
    LQ_CHECK (e->getShownBank() == Bank::side, "SIDE selected");
    const auto before = allValues (p);
    auto& knob = e->getKnob (PanelControl::gain);
    knob.setValue (knob.proportionOfLengthToValue (0.9), juce::sendNotificationSync);
    LQ_CHECK (changedIds (p, before) == std::set<juce::String> { idOf (Bank::side, e->getSelectedBand(), PanelControl::gain) }, "SIDE editable");

    LabQ8AudioProcessor q;
    q.setBusesLayout (mono);
    randomise (q, 81);
    setMode (q, params::ProcMode::midSide);
    randomise (p, 81); // same MID / STEREO as q ...
    setMode (p, params::ProcMode::midSide);
    for (int b = 0; b < 8; ++b) // ... and a very different SIDE bank on p
        hostSet (p, params::bankOf (Bank::side).bands[(size_t) b].gain, b % 2 ? 18.0f : -18.0f);
    LQ_CHECK (render (p, 1, 100)[0] == render (q, 1, 100)[0], "SIDE edits do not change a mono track");
}

LQ_TEST ("e4c rapid: 600 random clicks of mode / bank buttons, host mode automation and refreshes keep the panel on the right bank, write only proc_mode, gestures balanced")
{
    LabQ8AudioProcessor p;
    randomise (p, 91);
    auto e = openEditor (p);
    HostRecorder rec (p);
    std::mt19937 rng (92);
    std::uniform_int_distribution<int> action (0, 5);
    const auto bankValues = [&] { std::vector<float> v; for (int i = 0; i < p.getParameters().size(); ++i) if (i != params::procModeIndex) v.push_back (p.getParameters()[i]->getValue()); return v; };
    const auto before = bankValues();
    std::string why;
    bool ok = true;
    for (int k = 0; k < 600 && ok; ++k)
    {
        switch (action (rng))
        {
            case 0: click (e->getModeButton (0)); break;
            case 1: click (e->getModeButton (1)); break;
            case 2: click (e->getBankButton (Bank::mid)); break;
            case 3: click (e->getBankButton (Bank::side)); break;
            case 4: setMode (p, rng() % 2 ? params::ProcMode::midSide : params::ProcMode::stereo); e->refreshFromParameters(); break;
            default: e->selectBand ((int) (rng() % 8)); break;
        }
        const auto midSide = raw (p, params::procModeID) == 1.0f;
        const auto expected = ! midSide ? Bank::stereo : (p.getUiEditBank() == 2 ? Bank::side : Bank::mid);
        ok = e->getShownBank() == expected && panelShows (p, *e, expected, why) && e->isModeButtonLit (midSide ? 1 : 0)
             && e->isBankButtonAvailable (Bank::mid) == midSide;
        if (! ok)
            why = str ("step ", k, ": shown ", bankName (e->getShownBank()), " expected ", bankName (expected), " ", why);
    }
    LQ_CHECK (ok, why);
    LQ_CHECK (bankValues() == before, "no bank parameter written");
    LQ_CHECK (rec.balanced(), str ("gestures ", rec.totalBegins(), " / ", rec.totalEnds()));
}

LQ_TEST ("e4c D7: until E4d, in M/S the dynamic overlay is off with a discreet note (DYN / ALL), the grey curve only in EQ / ALL, and the analyzer says L/R")
{
    LabQ8AudioProcessor p;
    setMode (p, params::ProcMode::stereo);
    auto e = openEditor (p);
    for (int view = 0; view < 4; ++view)
    {
        e->setView (view);
        const bool dynamicView = view == LabQ8Editor::viewDyn || view == LabQ8Editor::viewAll;
        LQ_CHECK (e->areDynamicsShown() == dynamicView && ! e->isMidSideNoteShown() && ! e->getGhostBank().has_value(), str ("Stereo, view ", view));
        LQ_CHECK (e->getAnalyzerText().startsWith ("Analyzer") || e->getAnalyzerText() == "Pre + Post", "Stereo analyzer text unchanged");
    }
    click (e->getModeButton (1));
    for (int view = 0; view < 4; ++view)
    {
        e->setView (view);
        const bool dynamicView = view == LabQ8Editor::viewDyn || view == LabQ8Editor::viewAll;
        const bool curveView = view == LabQ8Editor::viewEq || view == LabQ8Editor::viewAll;
        LQ_CHECK (! e->areDynamicsShown(), str ("M/S, view ", view, ": no dynamic overlay"));
        LQ_CHECK (e->isMidSideNoteShown() == dynamicView, str ("M/S, view ", view, ": note"));
        LQ_CHECK (e->getGhostBank().has_value() == curveView, str ("M/S, view ", view, ": grey curve"));
        LQ_CHECK (e->getAnalyzerText().startsWith ("L/R"), str ("M/S analyzer: ", e->getAnalyzerText()));
    }
}

//==============================================================================
// D8: the copy commands.

LQ_TEST ("e4c copy: STEREO -> MID + SIDE copies all 80 values of the eight bands, never writes STEREO or proc_mode, one gesture per written parameter")
{
    LabQ8AudioProcessor p;
    randomise (p, 101);
    setMode (p, params::ProcMode::stereo);
    auto e = openEditor (p);
    HostRecorder rec (p);
    const auto before = allValues (p);
    e->handleSettingsMenuResult (LabQ8Editor::copyStereoToMidSideItem);

    std::set<juce::String> allowed;
    for (const auto bank : { Bank::mid, Bank::side })
        for (int b = 0; b < 8; ++b)
        {
            const auto& s = params::bankOf (bank).bands[(size_t) b];
            const auto& d = params::bankOf (bank).dynamics[(size_t) b];
            for (const auto* id : { s.frequency, s.gain, s.q, s.bypass, s.type, d.enabled, d.threshold, d.range, d.attack, d.release })
                allowed.insert (id);
        }
    const auto changed = changedIds (p, before);
    LQ_CHECK (std::all_of (changed.begin(), changed.end(), [&] (const juce::String& id) { return allowed.count (id) == 1; }), "only MID / SIDE written");

    int exact = 0, logUlp = 0, wrong = 0;
    for (const auto bank : { Bank::mid, Bank::side })
        for (int b = 0; b < 8; ++b)
        {
            const auto& s = params::bankOf (Bank::stereo).bands[(size_t) b];
            const auto& t = params::bankOf (bank).bands[(size_t) b];
            const auto& sd = params::bankOf (Bank::stereo).dynamics[(size_t) b];
            const auto& td = params::bankOf (bank).dynamics[(size_t) b];
            const std::pair<const char*, const char*> pairs[] = { { s.frequency, t.frequency }, { s.gain, t.gain }, { s.q, t.q }, { s.bypass, t.bypass }, { s.type, t.type },
                                                                  { sd.enabled, td.enabled }, { sd.threshold, td.threshold }, { sd.range, td.range }, { sd.attack, td.attack }, { sd.release, td.release } };
            for (const auto& [from, to] : pairs)
            {
                const auto a = raw (p, from), c = raw (p, to);
                if (a == c) ++exact;
                else if ((juce::String (from).endsWith ("_freq") || juce::String (from).endsWith ("_q")) && std::abs (a - c) <= 1.0e-6f * std::abs (a)) ++logUlp;
                else ++wrong;
            }
        }
    LQ_CHECK (wrong == 0, str (wrong, " values not copied"));
    ctx.note (str ("160 copied values: ", exact, " exact, ", logUlp, " frequency / Q within a few ulp (log mapping)"));
    int written = 0;
    for (auto& [index, n] : rec.changes)
        written += n;
    LQ_CHECK (rec.balanced() && rec.totalBegins() == written && written >= (int) changed.size(),
              str ("gestures ", rec.totalBegins(), " for ", written, " notified changes (", changed.size(), " values differ)"));
    for (int i = 0; i < 80; ++i)
        LQ_CHECK (p.getParameters()[i]->getValue() == before[(size_t) i], "STEREO untouched");
    LQ_CHECK (p.getParameters()[params::procModeIndex]->getValue() == before[(size_t) params::procModeIndex], "proc_mode untouched");
    LQ_CHECK (e->getShownBank() == Bank::stereo, "copying does not change the mode or the bank shown");

    // Again: nothing differs any more, nothing is written.
    rec.clear();
    e->handleSettingsMenuResult (LabQ8Editor::copyStereoToMidSideItem);
    juce::StringArray rewritten;
    for (auto& [index, n] : rec.changes)
    {
        auto* q = dynamic_cast<juce::RangedAudioParameter*> (p.getParameters()[index]);
        rewritten.add (q->paramID + "=" + juce::String (raw (p, q->paramID.toRawUTF8()), 9));
    }
    LQ_CHECK (rec.changes.empty() && rec.totalBegins() == 0, "a copy onto an identical bank writes nothing: " + rewritten.joinIntoString (", ").toStdString());
}

LQ_TEST ("e4c copy: MID -> SIDE and SIDE -> MID write only the target bank; the source and STEREO are untouched; restored after save and reopen")
{
    for (const bool midToSide : { true, false })
    {
        LabQ8AudioProcessor p;
        randomise (p, midToSide ? 111u : 112u);
        setMode (p, params::ProcMode::midSide);
        auto e = openEditor (p);
        const auto before = allValues (p);
        e->handleSettingsMenuResult (midToSide ? LabQ8Editor::copyMidToSideItem : LabQ8Editor::copySideToMidItem);
        const auto prefix = juce::String (midToSide ? "side_" : "mid_");
        const auto changed = changedIds (p, before);
        LQ_CHECK (! changed.empty() && std::all_of (changed.begin(), changed.end(), [&] (const juce::String& id) { return id.startsWith (prefix); }),
                  str (midToSide ? "MID -> SIDE" : "SIDE -> MID", ": wrote outside the target"));

        // Save and reopen: the copied bank comes back.
        juce::MemoryBlock state;
        p.getStateInformation (state);
        LabQ8AudioProcessor q;
        q.setStateInformation (state.getData(), (int) state.getSize());
        int differ = 0;
        for (int i = 0; i < p.getParameters().size(); ++i)
        {
            const auto id = dynamic_cast<juce::RangedAudioParameter*> (p.getParameters()[i])->paramID;
            const auto a = raw (p, id.toRawUTF8()), b = raw (q, id.toRawUTF8());
            if (a != b && ! ((id.endsWith ("_freq") || id.endsWith ("_q")) && std::abs (a - b) <= 1.0e-6f * std::abs (a)))
                ++differ;
        }
        LQ_CHECK (differ == 0, str (differ, " values not restored after save / reopen"));
    }
}

LQ_TEST ("e4c copy: after STEREO -> MID + SIDE (static bank), switching to M/S sounds like STEREO (within the copy's ulp); the menu keeps the six zoom choices")
{
    LabQ8AudioProcessor p;
    randomise (p, 121);
    for (int b = 0; b < 8; ++b)
        hostSet (p, params::bankOf (Bank::stereo).dynamics[(size_t) b].enabled, 0.0f);
    setMode (p, params::ProcMode::stereo);
    auto e = openEditor (p);
    e->handleSettingsMenuResult (LabQ8Editor::copyStereoToMidSideItem);
    const auto stereo = render (p, 2, 100);
    setMode (p, params::ProcMode::midSide);
    const auto midSide = render (p, 2, 100);
    double worst = 0.0, peak = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (size_t i = 0; i < stereo[0].size(); ++i)
        {
            worst = std::max (worst, (double) std::abs (stereo[(size_t) ch][i] - midSide[(size_t) ch][i]));
            peak = std::max (peak, (double) std::abs (stereo[(size_t) ch][i]));
        }
    LQ_CHECK (worst <= 1.0e-4 * std::max (1.0, peak), str ("M/S after the copy differs from STEREO by ", worst, " (peak ", peak, ")"));
    ctx.note (str ("copied static bank: M/S vs STEREO max difference ", worst, " for a peak of ", peak));

    juce::StringArray items;
    const auto menu = e->createSettingsMenu();
    juce::PopupMenu::MenuItemIterator it (menu);
    while (it.next())
        if (it.getItem().itemID > 0)
            items.add (it.getItem().text);
    LQ_CHECK (items.size() == 9 && items[0] == "40 %" && items[5] == "140 %" && items[6].startsWith ("Copy STEREO"), items.joinIntoString ("|").toStdString());
}
