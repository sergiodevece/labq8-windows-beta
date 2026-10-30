// UI-1 / UI-1.1 editor tests: the approved design's editor drives only
// public parameters, never the audio; band selection is pure view state.

#pragma clang diagnostic ignored "-Wfloat-equal" // bit-exactness is a tested property

#include "TestHarness.h"
#include "TestSignals.h"

#include "dsp/EqCore.h"
#include "parameters/ParameterIDs.h"
#include "plugin/PluginProcessor.h"
#include "ui/LabQ8Editor.h"
#include "ui/UiGeometry.h"
#include "ui/UiModel.h"
#include "ui/UiPreferences.h"

#include <algorithm>
#include <limits>
#include <random>

using labq8::LabQ8AudioProcessor;
using labq8::test::str;
using labq8::ui::LabQ8Editor;
using labq8::ui::PanelControl;
using namespace labq8::test::signals;
namespace params = labq8::params;
namespace geo = labq8::ui::geometry;

namespace
{
// The tests never read or write the user's real preferences file, and do
// not depend on the screens of the machine (every zoom fits unless a test
// says otherwise).
const juce::Rectangle<int> largeScreen { 0, 0, 4000, 3000 };
const bool preferencesInMemory = []
{
    labq8::ui::preferences::setStorageFileForTesting ({});
    LabQ8Editor::setScreenAreaForTesting (largeScreen);
    return true;
}();

void setParam (LabQ8AudioProcessor& p, const char* id, float value)
{
    auto* parameter = p.getValueTreeState().getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

float getParam (LabQ8AudioProcessor& p, const char* id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}

std::vector<float> allValues (LabQ8AudioProcessor& p)
{
    std::vector<float> v;
    for (auto* parameter : p.getParameters())
        v.push_back (parameter->getValue());
    return v;
}

// Every band different, random but reproducible.
void randomise (LabQ8AudioProcessor& p, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    for (auto* parameter : p.getParameters())
        parameter->setValueNotifyingHost (u (rng));
}

const char* idFor (int band, PanelControl c)
{
    const auto& ids = params::bandIDs[(size_t) band];
    const auto& dyn = params::dynamicIDs[(size_t) band];
    switch (c)
    {
        case PanelControl::frequency: return ids.frequency;
        case PanelControl::gain:      return ids.gain;
        case PanelControl::q:         return ids.q;
        case PanelControl::threshold: return dyn.threshold;
        case PanelControl::range:     return dyn.range;
        case PanelControl::attack:    return dyn.attack;
        case PanelControl::release:   return dyn.release;
    }
    return ids.frequency;
}

constexpr PanelControl allControls[] = { PanelControl::frequency, PanelControl::gain, PanelControl::q, PanelControl::threshold,
                                         PanelControl::range, PanelControl::attack, PanelControl::release };

std::unique_ptr<LabQ8Editor> openEditor (LabQ8AudioProcessor& p)
{
    return std::unique_ptr<LabQ8Editor> (dynamic_cast<LabQ8Editor*> (p.createEditor()));
}

juce::AudioBuffer<float> noiseBlock (int numSamples, unsigned seed)
{
    juce::AudioBuffer<float> b (2, numSamples);
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto n = makeNoise ((size_t) numSamples, seed * 3 + (unsigned) ch);
        b.copyFrom (ch, 0, n.data(), numSamples);
    }
    return b;
}

void click (juce::Component& c)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto centre = c.getLocalBounds().getCentre().toFloat();
    const auto now = juce::Time::getCurrentTime();
    juce::MouseEvent down (source, centre, juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, centre, now, 1, false);
    juce::MouseEvent up (source, centre, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, centre, now, 1, false);
    c.mouseDown (down);
    c.mouseUp (up);
}

// Counts begin / end gestures of one parameter (host automation recording).
struct GestureCounter final : juce::AudioProcessorParameter::Listener
{
    int begins = 0, ends = 0;
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends)++; }
};

void clickAt (juce::Component& c, juce::Point<float> local, bool rightButton = false)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const auto mods = rightButton ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier;
    juce::MouseEvent down (source, local, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, local, now, 1, false);
    juce::MouseEvent up (source, local, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, local, now, 1, false);
    c.mouseDown (down);
    c.mouseUp (up);
}

// A left-button drag on c: press at `from`, move through `to` in steps, release.
void dragOn (juce::Component& c, juce::Point<float> from, juce::Point<float> to, int steps = 4)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const auto left = juce::ModifierKeys::leftButtonModifier;
    auto event = [&] (juce::Point<float> at, juce::ModifierKeys mods)
    {
        return juce::MouseEvent (source, at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, from, now, 1, false);
    };
    c.mouseDown (event (from, left));
    for (int i = 1; i <= steps; ++i)
        c.mouseDrag (event (from + (to - from) * ((float) i / (float) steps), left));
    c.mouseUp (event (to, {}));
}

// Changed parameter IDs between two snapshots of allValues().
juce::StringArray changedIds (LabQ8AudioProcessor& p, const std::vector<float>& before)
{
    juce::StringArray ids;
    const auto after = allValues (p);
    const auto& all = p.getParameters();
    for (size_t i = 0; i < after.size(); ++i)
        if (after[i] != before[i])
            ids.add (dynamic_cast<juce::RangedAudioParameter*> (all[(int) i])->paramID);
    return ids;
}
} // namespace

//==============================================================================
LQ_TEST ("ui model: display texts, axes and the static response equal the DSP's")
{
    using labq8::ui::formatValue;
    LQ_CHECK (formatValue (PanelControl::frequency, 820.0) == "820 Hz" && formatValue (PanelControl::frequency, 2500.0) == "2.50 kHz"
                  && formatValue (PanelControl::frequency, 16000.0) == "16.0 kHz", "frequency text");
    LQ_CHECK (formatValue (PanelControl::gain, -4.2) == "-4.2 dB" && formatValue (PanelControl::gain, -0.02) == "0.0 dB", "gain text");
    LQ_CHECK (formatValue (PanelControl::q, 1.2) == "1.2" && formatValue (PanelControl::q, 0.71) == "0.71", "Q text");
    LQ_CHECK (formatValue (PanelControl::threshold, -20.0) == "-20 dB" && formatValue (PanelControl::threshold, -20.5) == "-20.5 dB", "threshold text");
    LQ_CHECK (formatValue (PanelControl::range, 8.0) == "8.0 dB" && formatValue (PanelControl::attack, 10.0) == "10 ms"
                  && formatValue (PanelControl::attack, 0.5) == "0.5 ms" && formatValue (PanelControl::release, 120.0) == "120 ms", "time texts");

    // The frequency axis passes through every printed label and is monotonic and invertible.
    bool anchors = true, monotonic = true, inverse = true;
    for (const auto& a : geo::frequencyAnchors)
        anchors = anchors && std::abs (labq8::ui::frequencyToX (a.hz) - a.x) < 1e-3f;
    float last = -1e9f;
    for (double f = 15.0; f < 24000.0; f *= 1.01)
    {
        const auto x = labq8::ui::frequencyToX (f);
        monotonic = monotonic && x > last;
        last = x;
        inverse = inverse && std::abs (labq8::ui::xToFrequency (x) / f - 1.0) < 1e-4;
    }
    LQ_CHECK (anchors && monotonic && inverse, "frequency axis");
    // +-24 dB on the seven grid lines (approved): +-24 at the outer lines, 8 dB per line.
    LQ_CHECK (labq8::ui::decibelsToY (0) == geo::plotZeroDbY && std::abs (labq8::ui::decibelsToY (-24) - 473.0f) < 0.01f
                  && std::abs (labq8::ui::decibelsToY (24) - 251.0f) < 0.01f && std::abs (labq8::ui::decibelsToY (-8) - 399.0f) < 0.01f, "dB axis");

    // The drawn response is the core's own composite magnitude (same
    // designs, clamps and effective Q), for all six types.
    std::array<labq8::ui::BandView, 8> bands;
    labq8::dsp::EqCore core;
    const labq8::dsp::FilterType types[] = { labq8::dsp::FilterType::bell, labq8::dsp::FilterType::lowShelf, labq8::dsp::FilterType::highShelf,
                                             labq8::dsp::FilterType::lowCut, labq8::dsp::FilterType::highCut, labq8::dsp::FilterType::notch,
                                             labq8::dsp::FilterType::bell, labq8::dsp::FilterType::bell };
    for (int b = 0; b < 8; ++b)
    {
        bands[(size_t) b] = { 60.0 * std::pow (2.3, b), b % 2 ? 5.0 : -7.0, 0.5 + 1.7 * b, types[b], b == 6 };
        core.setBandTargets (b, labq8::dsp::BandSettings { bands[(size_t) b].frequencyHz, bands[(size_t) b].gainDb, bands[(size_t) b].q,
                                                           bands[(size_t) b].bypassed, types[b] });
    }
    core.prepare (48000.0, 2);
    std::vector<double> freqs;
    for (double f = 20.0; f < 23000.0; f *= 1.05)
        freqs.push_back (f);
    const auto drawn = labq8::ui::cascadeResponseDb (bands, 48000.0, freqs);
    double worst = 0.0;
    for (size_t i = 0; i < freqs.size(); ++i)
        worst = std::max (worst, std::abs (drawn[i] - 20.0 * std::log10 (core.magnitudeAt (freqs[i]))));
    LQ_CHECK (worst < 1e-6, str ("drawn response differs from the core by ", worst, " dB"));
}

LQ_TEST ("ui editor: opens and closes repeatedly; default 60 %, no resize corner, proportional hit boxes at every zoom")
{
    LabQ8AudioProcessor p;
    for (int i = 0; i < 30; ++i)
    {
        auto editor = openEditor (p);
        LQ_CHECK (editor != nullptr, "editor type");
        if (editor == nullptr)
            return;
        if (i == 0)
        {
            LQ_CHECK (preferencesInMemory && editor->getWidth() == 1003 && editor->getHeight() == 565,
                      str ("default size ", editor->getWidth(), "x", editor->getHeight()));
            bool corner = false;
            for (auto* child : editor->getChildren())
                corner = corner || dynamic_cast<juce::ResizableCornerComponent*> (child) != nullptr;
            LQ_CHECK (! editor->isResizable() && ! corner, "no resize corner, not resizable by the host");

            for (const int width : { 669, 1003, 1338, 1672, 2006, 2341 })
            {
                editor->setSize (width, (int) std::lround (width * 941.0 / 1672.0));
                const auto scale = (float) width / 1672.0f;
                auto& knob = editor->getKnob (PanelControl::threshold);
                const auto shown = editor->getLocalArea (&knob, knob.getLocalBounds().toFloat());
                const auto c = geo::knobCentres[3];
                LQ_CHECK (std::abs (shown.getCentreX() - c.x * scale) < 1.0f && std::abs (shown.getCentreY() - c.y * scale) < 1.0f
                              && std::abs (shown.getWidth() - 2 * geo::knobHitRadius * scale) < 1.5f,
                          str ("threshold knob hit box at width ", width));
                auto& button = editor->getBandButton (7);
                const auto b = editor->getLocalArea (&button, button.getLocalBounds().toFloat());
                LQ_CHECK (std::abs (b.getX() - (geo::bandButtonLeft (7) - 2.0f) * scale) < 1.5f, str ("band 8 button at width ", width));
            }
        }
    }
}

LQ_TEST ("ui editor: selecting each band shows exactly its parameters and changes none")
{
    LabQ8AudioProcessor p;
    randomise (p, 11);
    const auto before = allValues (p);
    auto editor = openEditor (p);

    std::mt19937 rng (12);
    std::uniform_int_distribution<int> pick (0, 7);
    for (int round = 0; round < 40; ++round)
    {
        const auto band = round < 8 ? round : pick (rng);
        editor->selectBand (band);
        bool shows = editor->getSelectedBand() == band && p.getUiSelectedBand() == band;
        for (const auto c : allControls)
            shows = shows && (float) editor->getKnob (c).getValue() == getParam (p, idFor (band, c));
        LQ_CHECK (shows, str ("panel does not show band ", band + 1));
        LQ_CHECK (editor->getDisplayText (PanelControl::frequency).toStdString()
                      == labq8::ui::formatValue (PanelControl::frequency, getParam (p, idFor (band, PanelControl::frequency))),
                  "display shows the real value");
    }
    LQ_CHECK (allValues (p) == before, "selecting bands changed a parameter");

    // The selection survives closing and reopening the editor.
    editor->selectBand (5);
    editor.reset();
    editor = openEditor (p);
    LQ_CHECK (editor->getSelectedBand() == 5, "selected band restored on reopening");
    LQ_CHECK (allValues (p) == before, "reopening changed a parameter");

    // Bands are selected from their nodes (UI-1.1), never from the buttons.
    clickAt (editor->getScreen(), editor->getNodePosition (2) - editor->getScreen().getPosition().toFloat());
    LQ_CHECK (editor->getSelectedBand() == 2 && allValues (p) == before, "node click selects without changing anything");
}

LQ_TEST ("ui editor: panel edits reach only the selected band")
{
    LabQ8AudioProcessor p;
    randomise (p, 21);
    auto editor = openEditor (p);
    editor->selectBand (4);
    const auto before = allValues (p);

    for (const auto c : allControls)
    {
        auto& knob = editor->getKnob (c);
        knob.setValue (knob.proportionOfLengthToValue (0.37), juce::sendNotificationSync);
    }
    if (getParam (p, "b5_type") != 0.0f)
        editor->setFilterTypeIndex (0);        // Dynamic EQ is only available on Bell
    editor->toggleDynamicSwitch();
    editor->setFilterTypeIndex (3);

    const auto after = allValues (p);
    int changedOtherBands = 0, changedBand5 = 0;
    const auto& all = p.getParameters();
    for (size_t i = 0; i < after.size(); ++i)
    {
        if (after[i] == before[i])
            continue;
        const auto id = dynamic_cast<juce::RangedAudioParameter*> (all[(int) i])->paramID;
        (id.startsWith ("b5_") ? changedBand5 : changedOtherBands)++;
    }
    LQ_CHECK (changedOtherBands == 0, str (changedOtherBands, " parameters of other bands changed"));
    LQ_CHECK (changedBand5 == 9, str ("band 5: ", changedBand5, " of 9 panel parameters changed"));
    LQ_CHECK (std::abs (p.getValueTreeState().getParameter ("b5_q")->getValue() - 0.37f) < 1e-4f, "knob position = normalised parameter value");
    LQ_CHECK (getParam (p, "b5_type") == 3.0f, "Filter Type writes the choice index");
}

LQ_TEST ("ui editor: host automation updates the panel; automation of other bands is kept")
{
    LabQ8AudioProcessor p;
    auto editor = openEditor (p);
    editor->selectBand (1);

    setParam (p, "b2_freq", 333.0f);
    setParam (p, "b2_threshold", -41.5f);
    LQ_CHECK (std::abs (editor->getKnob (PanelControl::frequency).getValue() - 333.0) < 0.01, "host frequency reaches the knob");
    LQ_CHECK (editor->getDisplayText (PanelControl::threshold) == "-41.5 dB", str ("display ", editor->getDisplayText (PanelControl::threshold)));

    // A band that is not shown keeps its automation and shows it when selected.
    setParam (p, "b7_gain", 9.5f);
    LQ_CHECK (editor->getKnob (PanelControl::gain).getValue() != 9.5, "panel must not follow another band");
    editor->selectBand (6);
    LQ_CHECK (std::abs (editor->getKnob (PanelControl::gain).getValue() - 9.5) < 1e-4, "band 7 shows its automated gain");
    LQ_CHECK (std::abs (getParam (p, "b2_freq") - 333.0f) < 0.01f, "band 2 automation kept");
}

LQ_TEST ("ui editor: audio is bit-identical with the editor open, closed and switching bands")
{
    constexpr double fs = 48000.0;
    auto prepare = [] (LabQ8AudioProcessor& p)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        p.setBusesLayout (layout);
        randomise (p, 31);
        for (const auto& dyn : params::dynamicIDs)
            setParam (p, dyn.enabled, 1.0f); // dynamics on: detectors running
        p.prepareToPlay (fs, 256);
    };
    LabQ8AudioProcessor plain, withEditor;
    prepare (plain);
    prepare (withEditor);

    juce::MidiBuffer midi;
    std::unique_ptr<LabQ8Editor> editor;
    bool identical = true;
    for (int block = 0; block < 300; ++block)
    {
        if (block % 50 == 0)
            editor = openEditor (withEditor);      // open / reopen
        if (block % 50 == 25)
            editor.reset();                        // close
        if (editor != nullptr)
            editor->selectBand (block % 8);        // switching bands while playing

        auto a = noiseBlock (256, (unsigned) block), b = a;
        plain.processBlock (a, midi);
        withEditor.processBlock (b, midi);
        for (int ch = 0; ch < 2; ++ch)
            identical = identical && std::equal (a.getReadPointer (ch), a.getReadPointer (ch) + 256, b.getReadPointer (ch));
    }
    LQ_CHECK (identical, "the editor changed the audio");
}

LQ_TEST ("ui editor: loading a state with the editor open restores all 80 parameters and the panel follows")
{
    LabQ8AudioProcessor source;
    randomise (source, 41);
    juce::MemoryBlock state;
    source.getStateInformation (state);

    LabQ8AudioProcessor p;
    auto editor = openEditor (p);
    editor->selectBand (6);
    p.setStateInformation (state.getData(), (int) state.getSize());

    bool restored = true;
    for (auto* parameter : p.getParameters())
    {
        const auto id = dynamic_cast<juce::RangedAudioParameter*> (parameter)->paramID.toRawUTF8();
        restored = restored && getParam (p, id) == getParam (source, id);
    }
    LQ_CHECK (restored, "80 parameters restored with the editor open");
    bool panel = true;
    for (const auto c : allControls)
        panel = panel && (float) editor->getKnob (c).getValue() == getParam (p, idFor (6, c));
    LQ_CHECK (panel, "panel shows the loaded values of the selected band");
}

LQ_TEST ("ui editor: controls of the design without implementation change nothing")
{
    LabQ8AudioProcessor p;
    randomise (p, 51);
    auto editor = openEditor (p);
    const auto before = allValues (p);
    int found = 0;
    for (auto* child : editor->getContent().getChildren())
        for (const auto* name : { "Stereo Mode", "Polarity", "Intention", "Display view", "Phase Behavior" })
            if (child->getTitle() == name)
            {
                ++found;
                click (*child);
                auto* tip = dynamic_cast<juce::TooltipClient*> (child);
                LQ_CHECK (tip != nullptr && tip->getTooltip().contains ("not available"), str (name, " explains it is not available"));
            }
    LQ_CHECK (found == 5, str (found, " of 5 inactive areas (the meters are real since UI-2a)"));
    LQ_CHECK (allValues (p) == before && editor->getSelectedBand() == 0, "inactive controls changed something");
}

LQ_TEST ("ui bypass: each band button switches only its own band on / off, with one gesture; BAND n is an indicator only")
{
    LabQ8AudioProcessor p;
    randomise (p, 61);
    for (const auto& ids : params::bandIDs)
        setParam (p, ids.bypass, 0.0f);
    auto editor = openEditor (p);
    editor->selectBand (2);

    for (int b = 0; b < 8; ++b)
    {
        const auto* id = params::bandIDs[(size_t) b].bypass;
        auto& button = editor->getBandButton (b);
        LQ_CHECK (editor->isBandButtonOn (b), str ("button ", b + 1, " lit while active"));

        GestureCounter gestures;
        auto* bypass = p.getValueTreeState().getParameter (id);
        bypass->addListener (&gestures);
        const auto before = allValues (p);

        // Anywhere on the button, its edges included.
        clickAt (button, { 4.0f, 4.0f });
        const auto changed = changedIds (p, before);
        LQ_CHECK (getParam (p, id) == 1.0f && ! editor->isBandButtonOn (b), str ("click bypasses band ", b + 1));
        LQ_CHECK (changed.size() == 1 && changed[0] == id, str ("button ", b + 1, " changed ", changed.joinIntoString (",")));
        LQ_CHECK (gestures.begins == 1 && gestures.ends == 1, str ("gestures ", gestures.begins, "/", gestures.ends));
        LQ_CHECK (editor->getSelectedBand() == 2, "a button never selects");

        clickAt (button, button.getLocalBounds().getCentre().toFloat());
        LQ_CHECK (getParam (p, id) == 0.0f && editor->isBandButtonOn (b), str ("second click restores band ", b + 1));
        clickAt (button, button.getLocalBounds().getCentre().toFloat(), true);
        LQ_CHECK (getParam (p, id) == 0.0f, "right click does nothing");
        bypass->removeListener (&gestures);
    }

    // The colour dot next to BAND n takes no clicks.
    auto& indicator = editor->getBypassIndicator();
    bool takesClicks = true, childrenTakeClicks = true;
    indicator.getInterceptsMouseClicks (takesClicks, childrenTakeClicks);
    LQ_CHECK (! takesClicks, "indicator ignores the mouse");
    const auto dot = juce::Point<int> ((int) geo::bandDotCentre.x, (int) geo::bandDotCentre.y);
    LQ_CHECK (editor->getContent().getComponentAt (dot) != &indicator, "clicks at the dot do not reach the indicator");
    LQ_CHECK (getParam (p, "b3_bypass") == 0.0f, "indicator never toggles");
}

LQ_TEST ("ui routing: at every zoom the mouse reaches each band button, the settings icon and the screen; no corner")
{
    LabQ8AudioProcessor p;
    auto editor = openEditor (p);
    editor->setVisible (true); // as in a host window (hit testing skips invisible components)
    for (const float zoom : LabQ8Editor::zoomChoices)
    {
        LQ_CHECK (editor->setZoom (zoom), str ("zoom ", zoom));
        const auto s = editor->getScale();
        auto at = [&] (float x, float y) { return editor->getComponentAt (juce::Point<float> (x * s, y * s).roundToInt()); };
        for (int b = 0; b < 8; ++b)
        {
            const auto cx = geo::bandButtonLeft (b) + geo::bandButtonWidth * 0.5f;
            for (const float dx : { -45.0f, 0.0f, 45.0f })
                LQ_CHECK (at (cx + dx, geo::bandButtonCentreY) == &editor->getBandButton (b), str ("button ", b + 1, " at ", zoom));
        }
        LQ_CHECK (at (geo::settingsIconCentre.x, geo::settingsIconCentre.y) == &editor->getSettingsButton(), str ("settings icon at ", zoom));
        const auto node = editor->getNodePosition (4);
        LQ_CHECK (at (node.x, node.y) == &editor->getScreen(), str ("screen node at ", zoom));
        auto* corner = editor->getComponentAt (editor->getWidth() - 4, editor->getHeight() - 4);
        LQ_CHECK (dynamic_cast<juce::ResizableCornerComponent*> (corner) == nullptr, str ("no resize corner at ", zoom));
    }
}

LQ_TEST ("ui bypass: host automation, save and restore update the indicator")
{
    LabQ8AudioProcessor p;
    auto editor = openEditor (p);
    editor->selectBand (5);
    setParam (p, "b6_bypass", 1.0f);
    LQ_CHECK (editor->isBypassShown(), "host bypass of the shown band dims the indicator");
    setParam (p, "b2_bypass", 1.0f);
    editor->selectBand (1);
    LQ_CHECK (editor->isBypassShown(), "another band's automated bypass shows when selected");
    setParam (p, "b2_bypass", 0.0f);
    LQ_CHECK (! editor->isBypassShown(), "host un-bypass");

    juce::MemoryBlock state;
    p.getStateInformation (state); // b6 bypassed, b2 active
    LabQ8AudioProcessor q;
    auto other = openEditor (q);
    other->selectBand (5);
    q.setStateInformation (state.getData(), (int) state.getSize());
    LQ_CHECK (getParam (q, "b6_bypass") == 1.0f && other->isBypassShown(), "restored bypass shown");
    other->selectBand (1);
    LQ_CHECK (getParam (q, "b2_bypass") == 0.0f && ! other->isBypassShown(), "restored active band shown");

    // The band buttons follow the host and the restored state too.
    LQ_CHECK (! other->isBandButtonOn (5) && other->isBandButtonOn (1), "restored buttons");
    setParam (q, "b8_bypass", 1.0f);
    LQ_CHECK (! other->isBandButtonOn (7), "host bypass unlights button 8");
    setParam (q, "b8_bypass", 0.0f);
    LQ_CHECK (other->isBandButtonOn (7), "host un-bypass lights it again");
}

LQ_TEST ("ui dynamic: the switch is disabled for non-Bell types and the stored bN_dyn_on is kept")
{
    LabQ8AudioProcessor p;
    setParam (p, "b4_dyn_on", 1.0f);
    auto editor = openEditor (p);
    editor->selectBand (3);
    LQ_CHECK (editor->isDynamicSwitchAvailable() && editor->isDynamicSwitchOn(), "Bell: available and on");

    for (const int type : { 1, 2, 3, 4, 5 })
    {
        editor->setFilterTypeIndex (type);
        LQ_CHECK (! editor->isDynamicSwitchAvailable(), str ("type ", type, ": switch disabled"));
        editor->toggleDynamicSwitch();
        LQ_CHECK (getParam (p, "b4_dyn_on") == 1.0f, str ("type ", type, ": toggle ignored, stored dynamics unchanged"));
        click (editor->getDynamicSwitch());
        LQ_CHECK (getParam (p, "b4_dyn_on") == 1.0f, str ("type ", type, ": click ignored, stored dynamics unchanged"));
        auto* tip = dynamic_cast<juce::TooltipClient*> (&editor->getDynamicSwitch());
        LQ_CHECK (tip != nullptr && tip->getTooltip().contains ("Bell"), "explains why");
    }

    // Host automation of the type also updates it; back to Bell restores the stored state.
    setParam (p, "b4_type", 0.0f);
    LQ_CHECK (editor->isDynamicSwitchAvailable() && editor->isDynamicSwitchOn(), "Bell again: available, still on");
    setParam (p, "b4_type", 5.0f);
    LQ_CHECK (! editor->isDynamicSwitchAvailable(), "host type change disables it");
    setParam (p, "b4_type", 0.0f);
    editor->toggleDynamicSwitch();
    LQ_CHECK (getParam (p, "b4_dyn_on") == 0.0f && ! editor->isDynamicSwitchOn(), "usable again on Bell");

    // Per band: band 2 on a shelf is disabled, band 1 on Bell is not.
    setParam (p, "b2_type", 1.0f);
    editor->selectBand (1);
    LQ_CHECK (! editor->isDynamicSwitchAvailable(), "band 2 (shelf) disabled");
    editor->selectBand (0);
    LQ_CHECK (editor->isDynamicSwitchAvailable(), "band 1 (Bell) available");
}

//==============================================================================
// UI-1.1: zoom, a global preference of the user, chosen from six sizes.
namespace
{
struct ZoomMenuItems
{
    juce::StringArray names;
    std::vector<bool> enabled;
    int ticked = -1;
};

ZoomMenuItems zoomMenuItems (const LabQ8Editor& editor)
{
    ZoomMenuItems result;
    const auto menu = editor.createZoomMenu();
    juce::PopupMenu::MenuItemIterator items (menu);
    while (items.next())
        if (items.getItem().itemID > 0)
        {
            result.names.add (items.getItem().text);
            result.enabled.push_back (items.getItem().isEnabled);
            if (items.getItem().isTicked)
                result.ticked = items.getItem().itemID;
        }
    return result;
}
} // namespace

LQ_TEST ("ui zoom: menu of the settings icon, 40-140 % in six sizes, remembered globally; never part of the plugin state")
{
    namespace prefs = labq8::ui::preferences;
    prefs::setStorageFileForTesting ({});
    LabQ8AudioProcessor p;
    randomise (p, 71);
    juce::MemoryBlock stateBefore;
    p.getStateInformation (stateBefore);
    const auto before = allValues (p);

    auto editor = openEditor (p);
    LQ_CHECK (editor->getWidth() == 1003 && editor->getHeight() == 565 && std::abs (editor->getScale() - 0.6f) < 0.001f, "opens at 60 %");

    // The menu: Zoom header + six choices, all available on a large screen,
    // the current one ticked.
    auto menu = zoomMenuItems (*editor);
    LQ_CHECK (menu.names.joinIntoString ("|") == "40 %|60 %|80 %|100 %|120 %|140 %", menu.names.joinIntoString ("|").toStdString());
    LQ_CHECK (std::count (menu.enabled.begin(), menu.enabled.end(), true) == 6, "all six available");
    LQ_CHECK (menu.ticked == 2, str ("ticked item ", menu.ticked));

    // Every size exactly, with the proportions of the design; each choice is
    // stored at once and the editor reopens at it.
    const std::pair<int, int> sizes[] = { { 669, 376 }, { 1003, 565 }, { 1338, 753 }, { 1672, 941 }, { 2006, 1129 }, { 2341, 1317 } };
    for (int item = 1; item <= 6; ++item)
    {
        editor->handleZoomMenuResult (item);
        const auto zoom = LabQ8Editor::zoomChoices[(size_t) item - 1];
        const auto [w, h] = sizes[item - 1];
        LQ_CHECK (editor->getWidth() == w && editor->getHeight() == h,
                  str ("item ", item, ": ", editor->getWidth(), "x", editor->getHeight()));
        LQ_CHECK (std::abs ((double) editor->getWidth() / editor->getHeight() - 1672.0 / 941.0) < 0.003, "proportions");
        LQ_CHECK (std::abs (editor->getContent().getTransform().mat00 - (float) w / 1672.0f) < 1e-6f, "content scaled with the editor");
        LQ_CHECK (prefs::loadZoom() == zoom, "choice stored as the preference");
        LQ_CHECK (zoomMenuItems (*editor).ticked == item, "menu ticks the current size");
        editor.reset();
        editor = openEditor (p);
        LQ_CHECK (editor->getWidth() == w, str ("reopens at the stored ", zoom, ": ", editor->getWidth()));
    }

    // Only the six sizes: anything else changes nothing.
    editor->handleZoomMenuResult (3); // 80 %
    for (const float other : { 0.5f, 0.75f, 0.9f, 1.5f, 3.0f })
        LQ_CHECK (! editor->setZoom (other) && editor->getWidth() == 1338 && prefs::loadZoom() == 0.8f, str ("refused ", other));
    for (const float other : { 0.75f, 0.0f, std::numeric_limits<float>::quiet_NaN() })
    {
        prefs::saveZoom (other);
        LQ_CHECK (prefs::loadZoom() == 0.8f, str ("only the six sizes are stored, not ", other));
    }
    editor->handleZoomMenuResult (0); // menu dismissed
    editor->handleZoomMenuResult (7);
    LQ_CHECK (editor->getWidth() == 1338, "dismissed or unknown item changes nothing");

    // The settings icon sits on the design's gear and explains itself.
    auto& settings = editor->getSettingsButton();
    LQ_CHECK (std::abs ((float) settings.getBounds().getCentreX() - geo::settingsIconCentre.x) <= 1.0f
                  && std::abs ((float) settings.getBounds().getCentreY() - geo::settingsIconCentre.y) <= 1.0f, "on the gear");
    auto* tip = dynamic_cast<juce::TooltipClient*> (&settings);
    LQ_CHECK (tip != nullptr && tip->getTooltip().contains ("zoom"), "tooltip");

    // A size change by the host is not remembered (only menu choices are).
    editor->setSize (900, 506);
    editor.reset();
    LQ_CHECK (prefs::loadZoom() == 0.8f, "host size not stored");

    // Zooming never touches the parameters or the plugin state.
    juce::MemoryBlock stateAfter;
    p.getStateInformation (stateAfter);
    LQ_CHECK (allValues (p) == before && stateAfter == stateBefore, "zoom changed the plugin state");

    // A real file (temporary, never the user's): written at once, read back
    // by a new editor as after restarting the host; a size of an earlier
    // build or garbage falls back to 60 %.
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("labq8_ui11_test/LABQ8.settings");
    file.getParentDirectory().deleteRecursively();
    prefs::setStorageFileForTesting (file);
    LQ_CHECK (prefs::loadZoom() == 0.6f, "no file: 60 %");
    editor = openEditor (p);
    editor->handleZoomMenuResult (5); // 120 %
    LQ_CHECK (file.existsAsFile() && file.loadFileAsString().contains ("editorZoom"), "written at once to the file");
    editor.reset();
    prefs::setStorageFileForTesting (file); // forget everything kept in memory
    editor = openEditor (p);
    LQ_CHECK (editor->getWidth() == 2006, str ("restored from the file: ", editor->getWidth()));
    editor.reset();
    for (const char* stored : { "0.75", "0.5", "3", "nonsense", "" })
    {
        file.replaceWithText (juce::String ("<?xml version=\"1.0\"?><PROPERTIES><VALUE name=\"editorZoom\" val=\"") + stored + "\"/></PROPERTIES>");
        LQ_CHECK (prefs::loadZoom() == 0.6f, str ("stored '", stored, "' gives ", prefs::loadZoom()));
    }
    file.replaceWithText ("not xml");
    LQ_CHECK (prefs::loadZoom() == 0.6f, "unreadable file: 60 %");
    file.getParentDirectory().deleteRecursively();
    LQ_CHECK (prefs::defaultStorageFile().getFullPathName().endsWith ("Library/Application Support/VitaPlug/LABQ8/LABQ8.settings"),
              prefs::defaultStorageFile().getFullPathName().toStdString());
    prefs::setStorageFileForTesting ({});
}

LQ_TEST ("ui zoom: sizes that do not fit the screen are disabled, never applied, and the editor opens at the largest that fits")
{
    namespace prefs = labq8::ui::preferences;
    prefs::setStorageFileForTesting ({});
    LabQ8AudioProcessor p;

    // This MacBook's display: 1710 x 1072 points usable. Up to 100 % fits
    // with the host's window around it; 120 and 140 % do not.
    LabQ8Editor::setScreenAreaForTesting ({ 0, 0, 1710, 1072 });
    auto editor = openEditor (p);
    auto menu = zoomMenuItems (*editor);
    LQ_CHECK ((menu.enabled == std::vector<bool> { true, true, true, true, false, false }), "120 / 140 % disabled");
    LQ_CHECK (menu.names[4].contains ("too large for this screen") && ! menu.names[3].contains ("too large"), menu.names[4].toStdString());
    editor->handleZoomMenuResult (6);
    LQ_CHECK (editor->getWidth() == 1003 && prefs::loadZoom() == 0.6f, "140 % not applied nor stored");
    LQ_CHECK (editor->setZoom (1.0f) && editor->getWidth() == 1672, "100 % applied");

    // The stored choice is kept for a larger screen; on a small one the
    // editor opens at the largest size that fits, never clipped.
    LabQ8Editor::setScreenAreaForTesting (largeScreen);
    LQ_CHECK (editor->setZoom (1.4f), "140 % on a large screen");
    editor.reset();
    LabQ8Editor::setScreenAreaForTesting ({ 0, 0, 1440, 875 }); // 13" laptop
    editor = openEditor (p);
    LQ_CHECK (editor->getWidth() == 1338 && prefs::loadZoom() == 1.4f, str ("opens at 80 %, keeps 140 %: ", editor->getWidth()));
    LabQ8Editor::setScreenAreaForTesting ({ 0, 0, 640, 400 });
    editor.reset();
    editor = openEditor (p);
    LQ_CHECK (editor->getWidth() == 669, "40 % is always available");
    LQ_CHECK ((zoomMenuItems (*editor).enabled == std::vector<bool> { true, false, false, false, false, false }), "only 40 % on a tiny screen");

    // Exact limits: 100 % needs 1672 x (941 + 80).
    LQ_CHECK (LabQ8Editor::zoomFits (1.0f, { 0, 0, 1672, 1021 }) && ! LabQ8Editor::zoomFits (1.0f, { 0, 0, 1671, 1021 })
                  && ! LabQ8Editor::zoomFits (1.0f, { 0, 0, 1672, 1020 }), "fit limits");

    editor.reset();
    LabQ8Editor::setScreenAreaForTesting (largeScreen);
    prefs::setStorageFileForTesting ({});
}

//==============================================================================
// UI-1.1: selection and editing from the screen nodes.
namespace
{
// Eight Bell bands, well apart, flat: no node overlaps another.
void spreadBands (LabQ8AudioProcessor& p)
{
    for (int b = 0; b < 8; ++b)
    {
        const auto& ids = params::bandIDs[(size_t) b];
        setParam (p, ids.frequency, (float) (40.0 * std::pow (2.0, b * 1.25)));
        setParam (p, ids.gain, 0.0f);
        setParam (p, ids.q, 1.0f);
        setParam (p, ids.type, 0.0f);
        setParam (p, ids.bypass, 0.0f);
    }
}

juce::Point<float> onScreen (LabQ8Editor& editor, juce::Point<float> reference)
{
    return reference - editor.getScreen().getPosition().toFloat();
}
} // namespace

LQ_TEST ("ui nodes: a click selects the band and shows its Q handles; other node switches, empty screen and chassis hide")
{
    LabQ8AudioProcessor p;
    spreadBands (p);
    setParam (p, "b6_bypass", 1.0f);
    auto editor = openEditor (p);
    auto& screen = editor->getScreen();
    const auto before = allValues (p);
    LQ_CHECK (! editor->areQHandlesShown(), "no handles on opening");

    clickAt (screen, onScreen (*editor, editor->getNodePosition (2)) + juce::Point<float> (4.0f, -3.0f));
    LQ_CHECK (editor->getSelectedBand() == 2 && editor->areQHandlesShown(), "node 3 selected, handles shown");
    LQ_CHECK ((float) editor->getKnob (PanelControl::frequency).getValue() == getParam (p, "b3_freq"), "panel shows band 3");

    clickAt (screen, onScreen (*editor, editor->getNodePosition (5)));
    LQ_CHECK (editor->getSelectedBand() == 5 && editor->areQHandlesShown(), "a bypassed band is selectable");
    LQ_CHECK (editor->isBypassShown(), "BAND 6 indicator dimmed");

    clickAt (screen, onScreen (*editor, { 420.0f, 270.0f }));
    LQ_CHECK (! editor->areQHandlesShown() && editor->getSelectedBand() == 5, "empty screen hides the handles, keeps the band");

    editor->showQHandles (1);
    clickAt (editor->getContent(), { 60.0f, 400.0f });
    LQ_CHECK (! editor->areQHandlesShown(), "a click on the chassis hides them");

    clickAt (screen, onScreen (*editor, editor->getNodePosition (4)), true);
    LQ_CHECK (editor->getSelectedBand() == 1, "right click does not select");
    LQ_CHECK (allValues (p) == before, str ("selection changed ", changedIds (p, before).joinIntoString (",")));
}

LQ_TEST ("ui nodes: dragging moves FREQUENCY horizontally, GAIN vertically, both diagonally; one gesture each; panel follows")
{
    LabQ8AudioProcessor p;
    spreadBands (p);
    auto editor = openEditor (p);
    auto& screen = editor->getScreen();
    auto* freq = p.getValueTreeState().getParameter ("b4_freq");
    auto* gain = p.getValueTreeState().getParameter ("b4_gain");
    GestureCounter freqGestures, gainGestures;
    freq->addListener (&freqGestures);
    gain->addListener (&gainGestures);

    // Horizontal: frequency only (gain untouched, bit for bit).
    auto before = allValues (p);
    const auto f0 = getParam (p, "b4_freq");
    auto node = onScreen (*editor, editor->getNodePosition (3));
    dragOn (screen, node, node + juce::Point<float> (60.0f, 0.0f));
    const auto expected = labq8::ui::xToFrequency (labq8::ui::frequencyToX (f0) + 60.0f);
    LQ_CHECK (std::abs (getParam (p, "b4_freq") / expected - 1.0) < 1e-3, str ("frequency ", getParam (p, "b4_freq"), " vs ", expected));
    auto changed = changedIds (p, before);
    LQ_CHECK (changed.size() == 1 && changed[0] == "b4_freq", str ("horizontal changed ", changed.joinIntoString (",")));
    LQ_CHECK (freqGestures.begins == 1 && freqGestures.ends == 1 && gainGestures.begins == 0, "one frequency gesture");
    LQ_CHECK ((float) editor->getKnob (PanelControl::frequency).getValue() == getParam (p, "b4_freq")
                  && editor->getDisplayText (PanelControl::frequency).toStdString() == labq8::ui::formatValue (PanelControl::frequency, getParam (p, "b4_freq")),
              "knob and display follow the node");

    // Vertical: gain only; 37 px = 8 dB.
    before = allValues (p);
    node = onScreen (*editor, editor->getNodePosition (3));
    dragOn (screen, node, node + juce::Point<float> (0.0f, -37.0f));
    LQ_CHECK (std::abs (getParam (p, "b4_gain") - 8.0f) < 0.02f, str ("gain ", getParam (p, "b4_gain")));
    changed = changedIds (p, before);
    LQ_CHECK (changed.size() == 1 && changed[0] == "b4_gain", str ("vertical changed ", changed.joinIntoString (",")));
    LQ_CHECK (gainGestures.begins == 1 && gainGestures.ends == 1 && freqGestures.begins == 1, "one gain gesture");

    // Diagonal: both, each its own gesture.
    before = allValues (p);
    node = onScreen (*editor, editor->getNodePosition (3));
    dragOn (screen, node, node + juce::Point<float> (-40.0f, 18.5f));
    changed = changedIds (p, before);
    LQ_CHECK (changed.size() == 2 && changed.contains ("b4_freq") && changed.contains ("b4_gain"), changed.joinIntoString (",").toStdString());
    LQ_CHECK (std::abs (getParam (p, "b4_gain") - 4.0f) < 0.02f, str ("diagonal gain ", getParam (p, "b4_gain")));
    LQ_CHECK (freqGestures.begins == 2 && freqGestures.ends == 2 && gainGestures.begins == 2 && gainGestures.ends == 2, "gestures");

    // Limits: never beyond the parameter ranges.
    node = onScreen (*editor, editor->getNodePosition (3));
    dragOn (screen, node, node + juce::Point<float> (2000.0f, -2000.0f), 8);
    LQ_CHECK (std::abs (getParam (p, "b4_freq") - 20000.0f) < 0.01f && std::abs (getParam (p, "b4_gain") - 24.0f) < 1e-5f,
              str ("clamped at 20 kHz / +24 dB: ", getParam (p, "b4_freq"), " Hz ", getParam (p, "b4_gain"), " dB"));

    // A press with < 3 px jitter only selects.
    before = allValues (p);
    node = onScreen (*editor, editor->getNodePosition (1));
    dragOn (screen, node, node + juce::Point<float> (1.5f, 1.5f));
    LQ_CHECK (editor->getSelectedBand() == 1 && allValues (p) == before, "jitter changes nothing");

    // Cuts and notch have no gain: vertical movement is ignored.
    setParam (p, "b2_type", 3.0f);
    before = allValues (p);
    node = onScreen (*editor, editor->getNodePosition (1));
    dragOn (screen, node, node + juce::Point<float> (0.0f, -50.0f));
    LQ_CHECK (allValues (p) == before, "vertical drag on a Low Cut changes nothing");
    freq->removeListener (&freqGestures);
    gain->removeListener (&gainGestures);

    // Host automation moves the node.
    const auto x0 = editor->getNodePosition (6).x;
    setParam (p, "b7_freq", 300.0f);
    LQ_CHECK (std::abs (editor->getNodePosition (6).x - labq8::ui::frequencyToX (300.0)) < 0.01f && editor->getNodePosition (6).x != x0,
              "automated frequency moves the node");
}

LQ_TEST ("ui Q handles: Bell / Notch on their real band edges; shelves / cuts a pure Q control within the effective range")
{
    LabQ8AudioProcessor p;
    spreadBands (p);
    p.prepareToPlay (48000.0, 256);
    auto editor = openEditor (p);
    using labq8::dsp::FilterType;

    // Bell: half-gain (dB) points; Notch: -3 dB points (when wider than the minimum distance).
    for (const auto& [type, gainDb, q, level] : { std::tuple { 0.0f, 12.0f, 0.8f, 6.0 }, std::tuple { 0.0f, -10.0f, 1.5f, -5.0 },
                                                   std::tuple { 5.0f, 0.0f, 0.6f, -3.0103 } })
    {
        setParam (p, "b4_type", type);
        setParam (p, "b4_gain", gainDb);
        setParam (p, "b4_q", q);
        editor->showQHandles (3);
        const auto [left, right] = editor->getQHandleCentres();
        labq8::ui::BandView band { getParam (p, "b4_freq"), getParam (p, "b4_gain"), getParam (p, "b4_q"),
                                   labq8::dsp::filterTypeFromIndex ((int) type), false };
        const auto edges = labq8::ui::bandResponseDb (band, 48000.0, { labq8::ui::xToFrequency (left.x), labq8::ui::xToFrequency (right.x) });
        LQ_CHECK (std::abs (edges[0] - level) < 0.05 && std::abs (edges[1] - level) < 0.05,
                  str ("type ", type, " Q ", q, ": edges at ", edges[0], " / ", edges[1], " dB, expected ", level));
    }

    // Shelves and cuts: equal distances, growing as Q falls, between 14 and 48 px.
    for (const auto type : { 1.0f, 2.0f, 3.0f, 4.0f })
    {
        setParam (p, "b4_type", type);
        setParam (p, "b4_gain", 6.0f);
        float last = 0.0f;
        bool ok = true;
        for (const float q : { 18.0f, 2.0f, 1.0f, 0.5f, 0.2f, 0.1f })
        {
            setParam (p, "b4_q", q);
            const auto node = editor->getNodePosition (3);
            const auto [left, right] = editor->getQHandleCentres();
            const auto d = right.x - node.x;
            ok = ok && std::abs ((node.x - left.x) - d) < 1e-3f && d >= last - 1e-3f && d >= 14.0f - 1e-3f && d <= 48.0f + 1e-3f;
            last = d;
        }
        LQ_CHECK (ok && last > 47.9f, str ("type ", type, " handle distances"));
    }

    // Dragging: outwards lowers Q (halves every 45 px), inwards raises it,
    // one gesture, only b4_q changes.
    setParam (p, "b4_type", 0.0f);
    setParam (p, "b4_q", 2.0f);
    editor->showQHandles (3);
    auto* qParam = p.getValueTreeState().getParameter ("b4_q");
    GestureCounter gestures;
    qParam->addListener (&gestures);
    auto before = allValues (p);
    auto right = editor->getQHandleCentres().second;
    dragOn (editor->getScreen(), onScreen (*editor, right), onScreen (*editor, right + juce::Point<float> (45.0f, 0.0f)));
    LQ_CHECK (std::abs (getParam (p, "b4_q") - 1.0f) < 0.01f, str ("outwards: Q ", getParam (p, "b4_q")));
    auto changed = changedIds (p, before);
    LQ_CHECK (changed.size() == 1 && changed[0] == "b4_q" && gestures.begins == 1 && gestures.ends == 1, "only Q, one gesture");
    LQ_CHECK (editor->getDisplayText (PanelControl::q) == "1.0", editor->getDisplayText (PanelControl::q).toStdString());
    auto left = editor->getQHandleCentres().first;
    dragOn (editor->getScreen(), onScreen (*editor, left), onScreen (*editor, left + juce::Point<float> (90.0f, 0.0f)));
    LQ_CHECK (std::abs (getParam (p, "b4_q") - 4.0f) < 0.04f, str ("inwards: Q ", getParam (p, "b4_q")));
    qParam->removeListener (&gestures);

    // Within the effective range: a High Cut stored at Q 6 (effective 2)
    // starts from 2, narrowing has no room and writes nothing...
    setParam (p, "b4_type", 4.0f);
    setParam (p, "b4_q", 6.0f);
    before = allValues (p);
    right = editor->getQHandleCentres().second;
    dragOn (editor->getScreen(), onScreen (*editor, right), onScreen (*editor, right + juce::Point<float> (-40.0f, 0.0f)));
    LQ_CHECK (allValues (p) == before, "no Q beyond the effective maximum");
    // ...widening goes down from the effective 2.
    right = editor->getQHandleCentres().second;
    dragOn (editor->getScreen(), onScreen (*editor, right), onScreen (*editor, right + juce::Point<float> (45.0f, 0.0f)));
    LQ_CHECK (std::abs (getParam (p, "b4_q") - 1.0f) < 0.01f, str ("High Cut widened to Q ", getParam (p, "b4_q")));
    // Shelf: at most 1; the bottom of every range is 0.1.
    setParam (p, "b4_type", 1.0f);
    setParam (p, "b4_q", 0.7f);
    right = editor->getQHandleCentres().second;
    dragOn (editor->getScreen(), onScreen (*editor, right), onScreen (*editor, right + juce::Point<float> (-200.0f, 0.0f)));
    LQ_CHECK (std::abs (getParam (p, "b4_q") - 1.0f) < 1e-4f, str ("Low Shelf narrowed to its limit: Q ", getParam (p, "b4_q")));
    right = editor->getQHandleCentres().second;
    dragOn (editor->getScreen(), onScreen (*editor, right), onScreen (*editor, right + juce::Point<float> (600.0f, 0.0f)), 8);
    LQ_CHECK (std::abs (getParam (p, "b4_q") - 0.1f) < 1e-4f, str ("widened to the minimum: Q ", getParam (p, "b4_q")));
}

LQ_TEST ("ui nodes: editing from the screen leaves the audio path to the parameters (bit-identical to the same automation)")
{
    // The screen only writes parameters: rendering after a node drag equals
    // rendering after setting the same final values directly.
    constexpr double fs = 48000.0;
    auto prepare = [] (LabQ8AudioProcessor& p)
    {
        spreadBands (p);
        p.prepareToPlay (fs, 256);
    };
    LabQ8AudioProcessor edited, direct;
    prepare (edited);
    prepare (direct);
    {
        auto editor = openEditor (edited);
        const auto node = onScreen (*editor, editor->getNodePosition (2));
        dragOn (editor->getScreen(), node, node + juce::Point<float> (35.0f, -20.0f));
        const auto right = editor->getQHandleCentres().second;
        dragOn (editor->getScreen(), onScreen (*editor, right), onScreen (*editor, right + juce::Point<float> (-30.0f, 0.0f)));
    }
    for (const auto* id : { "b3_freq", "b3_gain", "b3_q" })
        setParam (direct, id, getParam (edited, id));
    LQ_CHECK (allValues (edited) == allValues (direct), "same parameters");

    juce::MidiBuffer midi;
    bool identical = true;
    for (int block = 0; block < 100; ++block)
    {
        auto a = noiseBlock (256, (unsigned) block + 900), b = a;
        edited.processBlock (a, midi);
        direct.processBlock (b, midi);
        for (int ch = 0; ch < 2; ++ch)
            identical = identical && std::equal (a.getReadPointer (ch), a.getReadPointer (ch) + 256, b.getReadPointer (ch));
    }
    LQ_CHECK (identical, "audio differs");
}
