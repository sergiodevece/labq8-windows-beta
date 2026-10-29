// UI-1 editor tests: the approved design's editor drives only public
// parameters, never the audio; band selection is pure view state.

#pragma clang diagnostic ignored "-Wfloat-equal" // bit-exactness is a tested property

#include "TestHarness.h"
#include "TestSignals.h"

#include "dsp/EqCore.h"
#include "parameters/ParameterIDs.h"
#include "plugin/PluginProcessor.h"
#include "ui/LabQ8Editor.h"
#include "ui/UiGeometry.h"
#include "ui/UiModel.h"

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

LQ_TEST ("ui editor: opens and closes repeatedly; default 75 %, fixed aspect, proportional hit boxes")
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
            LQ_CHECK (editor->getWidth() == 1254 && editor->getHeight() == 706, str ("default size ", editor->getWidth(), "x", editor->getHeight()));
            LQ_CHECK (editor->isResizable() && std::abs (editor->getConstrainer()->getFixedAspectRatio() - 1672.0 / 941.0) < 1e-9, "fixed aspect ratio");
            LQ_CHECK (editor->getConstrainer()->getMinimumWidth() == 836 && editor->getConstrainer()->getMaximumWidth() == 1672, "size limits");

            for (const int width : { 836, 1000, 1254, 1672 })
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
                LQ_CHECK (std::abs (b.getX() - geo::bandButtonLeft (7) * scale) < 1.5f, str ("band 8 button at width ", width));
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

    // Clicking a band button selects it.
    click (editor->getBandButton (2));
    LQ_CHECK (editor->getSelectedBand() == 2 && allValues (p) == before, "band button click");
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
        for (const auto* name : { "Stereo Mode", "Polarity", "Intention", "Display view", "Phase Behavior", "Level meters" })
            if (child->getTitle() == name)
            {
                ++found;
                click (*child);
                auto* tip = dynamic_cast<juce::TooltipClient*> (child);
                LQ_CHECK (tip != nullptr && tip->getTooltip().contains ("not available"), str (name, " explains it is not available"));
            }
    LQ_CHECK (found == 6, str (found, " of 6 inactive areas"));
    LQ_CHECK (allValues (p) == before && editor->getSelectedBand() == 0, "inactive controls changed something");
}

//==============================================================================
namespace
{
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
} // namespace

LQ_TEST ("ui bypass: the BAND n indicator toggles bN_bypass of the shown band only, with a gesture; 34 x 34 click area")
{
    LabQ8AudioProcessor p;
    randomise (p, 61);
    for (const auto& ids : params::bandIDs)
        setParam (p, ids.bypass, 0.0f);
    auto editor = openEditor (p);
    editor->selectBand (2);

    auto& indicator = editor->getBypassIndicator();
    LQ_CHECK (indicator.getWidth() == 34 && indicator.getHeight() == 34
                  && std::abs ((float) indicator.getBounds().getCentreX() - geo::bandDotCentre.x) <= 1.0f
                  && std::abs ((float) indicator.getBounds().getCentreY() - geo::bandDotCentre.y) <= 1.0f,
              "34 x 34 area centred on the dot");

    GestureCounter gestures;
    auto* bypass3 = p.getValueTreeState().getParameter ("b3_bypass");
    bypass3->addListener (&gestures);
    const auto before = allValues (p);

    // A click near the corner of the area (outside the 19 px dot) works.
    clickAt (indicator, { 2.0f, 2.0f });
    LQ_CHECK (getParam (p, "b3_bypass") == 1.0f && editor->isBypassShown(), "click bypasses band 3");
    LQ_CHECK (gestures.begins == 1 && gestures.ends == 1, str ("gestures ", gestures.begins, "/", gestures.ends));
    int otherChanges = 0;
    const auto after = allValues (p);
    const auto& all = p.getParameters();
    for (size_t i = 0; i < after.size(); ++i)
        if (after[i] != before[i] && dynamic_cast<juce::RangedAudioParameter*> (all[(int) i])->paramID != "b3_bypass")
            ++otherChanges;
    LQ_CHECK (otherChanges == 0, str (otherChanges, " other parameters changed"));
    LQ_CHECK (editor->getSelectedBand() == 2, "bypass does not change the selection");

    clickAt (indicator, { 17.0f, 17.0f });
    LQ_CHECK (getParam (p, "b3_bypass") == 0.0f && ! editor->isBypassShown(), "second click restores band 3");
    clickAt (indicator, { 17.0f, 17.0f }, true);
    LQ_CHECK (getParam (p, "b3_bypass") == 0.0f, "right click does nothing");
    bypass3->removeListener (&gestures);

    // Selecting other bands never toggles bypass; the band buttons still select.
    for (int b = 0; b < 8; ++b)
    {
        click (editor->getBandButton (b));
        LQ_CHECK (editor->getSelectedBand() == b, str ("button ", b + 1, " selects"));
    }
    bool none = true;
    for (const auto& ids : params::bandIDs)
        none = none && getParam (p, ids.bypass) == 0.0f;
    LQ_CHECK (none, "selecting bands toggled a bypass");
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
