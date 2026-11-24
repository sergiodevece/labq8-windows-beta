#include "TestHarness.h"

#include "parameters/Iaq8Parameters.h"
#include "parameters/ParameterLayout.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace
{
// Minimal host for an APVTS: the tests only need the parameter tree.
class DummyProcessor final : public juce::AudioProcessor
{
public:
    DummyProcessor() : AudioProcessor (BusesProperties()) {}
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "Dummy"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

// Exact float equality on purpose: the layouts must be bit-identical.
bool same (float a, float b) { return ! (a < b) && ! (a > b); }
} // namespace

LQ_TEST ("iaq8 params: 40 parameters with LABQ8 STEREO ids, ranges and defaults")
{
    DummyProcessor iaqHost, labHost;
    juce::AudioProcessorValueTreeState iaq (iaqHost, nullptr, "IAQ8", iaq8::params::createLayout());
    juce::AudioProcessorValueTreeState lab (labHost, nullptr, "LABQ8", labq8::params::createParameterLayout());

    const auto& iaqParams = iaqHost.getParameters();
    LQ_CHECK (iaqParams.size() == 40, "IAQ8 exposes exactly 40 parameters");
    LQ_CHECK (iaq8::params::stateVersion == 1, "state version is 1");

    for (auto* p : iaqParams)
    {
        const auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
        LQ_CHECK (ranged != nullptr, "every parameter is a RangedAudioParameter");
        if (ranged == nullptr)
            continue;

        const auto id = ranged->getParameterID();
        LQ_CHECK (! id.contains ("_dyn") && id != "proc_mode" && ! id.startsWith ("mid_") && ! id.startsWith ("side_"),
                  "no dynamics, mode, mid or side ids: " + id.toStdString());

        const auto* ref = lab.getParameter (id);
        LQ_CHECK (ref != nullptr, "id exists in LABQ8: " + id.toStdString());
        if (ref == nullptr)
            continue;

        LQ_CHECK (ranged->getName (256) == ref->getName (256), "same name: " + id.toStdString());
        LQ_CHECK (ranged->getParameterID() == ref->getParameterID(), "same id");
        LQ_CHECK (ranged->getVersionHint() == ref->getVersionHint(), "same version hint: " + id.toStdString());
        LQ_CHECK (same (ranged->getDefaultValue(), ref->getDefaultValue()), "same default: " + id.toStdString());
        LQ_CHECK (ranged->getNumSteps() == ref->getNumSteps(), "same steps: " + id.toStdString());
        LQ_CHECK (ranged->getLabel() == ref->getLabel(), "same label: " + id.toStdString());

        const auto a = ranged->getNormalisableRange();
        const auto b = ref->getNormalisableRange();
        LQ_CHECK (same (a.start, b.start) && same (a.end, b.end) && same (a.interval, b.interval) && same (a.skew, b.skew),
                  "same range: " + id.toStdString());

        // The log mappings are lambdas, so compare their behaviour too.
        for (const float n : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            LQ_CHECK (same (ranged->convertFrom0to1 (n), ref->convertFrom0to1 (n)), "same mapping: " + id.toStdString());
    }
}
