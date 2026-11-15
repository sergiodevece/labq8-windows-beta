#include "plugin/PluginProcessor.h"

#include "parameters/ParameterIDs.h"
#include "parameters/ParameterLayout.h"
#include "ui/LabQ8Editor.h"

#include <cmath>

namespace labq8
{
namespace
{
    std::atomic<float>* rawParameter (juce::AudioProcessorValueTreeState& state, const char* parameterID)
    {
        auto* value = state.getRawParameterValue (parameterID);
        jassert (value != nullptr); // every ID in ParameterIDs.h must exist in the layout
        return value;
    }
} // namespace

static_assert (params::numBands == dsp::EqCore::numBands);

LabQ8AudioProcessor::LabQ8AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, stateType, params::createParameterLayout())
{
    // E4b: the three banks, each from its own IDs (params::bankOf); the
    // STEREO bank is the 80 pre-E4 parameters.
    static_assert ((int) dsp::StereoBank::stereo == (int) params::Bank::stereo
                   && (int) dsp::StereoBank::mid == (int) params::Bank::mid
                   && (int) dsp::StereoBank::side == (int) params::Bank::side);
    for (int b = 0; b < params::numBanks; ++b)
    {
        const auto ids = params::bankOf (static_cast<params::Bank> (b));
        auto& bank = bankParameters[(size_t) b];
        for (size_t band = 0; band < bank.bands.size(); ++band)
        {
            const auto& s = ids.bands[band];
            bank.bands[band] = { rawParameter (parameters, s.frequency),
                                 rawParameter (parameters, s.gain),
                                 rawParameter (parameters, s.q),
                                 rawParameter (parameters, s.bypass),
                                 rawParameter (parameters, s.type) };

            const auto& dyn = ids.dynamics[band];
            bank.dynamics[band] = { rawParameter (parameters, dyn.enabled),
                                    rawParameter (parameters, dyn.threshold),
                                    rawParameter (parameters, dyn.range),
                                    rawParameter (parameters, dyn.attack),
                                    rawParameter (parameters, dyn.release) };
        }
    }
    procMode = rawParameter (parameters, params::procModeID);
}

bool LabQ8AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

dsp::BandSettings LabQ8AudioProcessor::readBandSettings (const BandParameters& band) const noexcept
{
    // The host gain parameter snaps to a 0.01 dB grid in float, so "0.00 dB"
    // arrives as about -5.4e-7 dB. Anything that displays as 0.00 dB is made
    // exactly 0 so the band can go idle (bit-exact, no CPU).
    constexpr double neutralGainDb = 0.005;

    dsp::BandSettings s;
    s.frequencyHz = band.frequency->load (std::memory_order_relaxed);
    s.gainDb      = band.gain->load (std::memory_order_relaxed);
    if (std::abs (s.gainDb) < neutralGainDb)
        s.gainDb = 0.0;

    s.q           = band.q->load (std::memory_order_relaxed);
    s.bypassed    = band.bypass->load (std::memory_order_relaxed) >= 0.5f;
    s.type        = dsp::filterTypeFromIndex (static_cast<int> (std::lround (band.type->load (std::memory_order_relaxed))));
    return s;
}

dsp::DynamicSettings LabQ8AudioProcessor::readDynamicSettings (const DynamicParameters& d) noexcept
{
    // Each section receives only its own band's parameters. Threshold and
    // range are smoothed (20 ms) and the enable fades (20 ms) inside the DSP.
    dsp::DynamicSettings s;
    s.enabled     = d.enabled->load (std::memory_order_relaxed) >= 0.5f;
    s.thresholdDb = d.threshold->load (std::memory_order_relaxed);
    s.rangeDb     = d.range->load (std::memory_order_relaxed);
    s.attackMs    = d.attack->load (std::memory_order_relaxed);
    s.releaseMs   = d.release->load (std::memory_order_relaxed);
    return s;
}

void LabQ8AudioProcessor::pushParametersToCore() noexcept
{
    // STEREO first, exactly as before E4b (same values, same order); then
    // the MID and SIDE banks and the mode. Each core receives only its own
    // bank's parameters.
    for (int b = 0; b < dsp::StereoModeEngine::numBanks; ++b)
    {
        const auto bank = static_cast<dsp::StereoBank> (b);
        const auto& source = bankParameters[(size_t) b];
        for (size_t band = 0; band < source.bands.size(); ++band)
        {
            engine.setBandTargets (bank, static_cast<int> (band), readBandSettings (source.bands[band]));
            engine.setBandDynamics (bank, static_cast<int> (band), readDynamicSettings (source.dynamics[band]));
        }
    }

    const auto mode = static_cast<int> (std::lround (procMode->load (std::memory_order_relaxed)));
    engine.setMode (mode == static_cast<int> (params::ProcMode::midSide) ? dsp::StereoMode::midSide : dsp::StereoMode::stereo);
}

void LabQ8AudioProcessor::prepareToPlay (double sampleRate, int)
{
    // Targets first so prepare() snaps to the current host values (no ramp
    // from defaults when playback starts).
    pushParametersToCore();
    engine.prepare (sampleRate, juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels()));
    inputMeter.reset();
    outputMeter.reset();
    const auto channels = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels());
    streamPosition = 0;
    preTap.prepare (sampleRate, channels);
    postTap.prepare (sampleRate, channels);
    dynamicsTelemetry.prepare();
}

void LabQ8AudioProcessor::offerDynamics (bool hostBypassed) noexcept
{
    // E4d: the dynamics of the bank the editor shows (DynamicsTelemetry's
    // watched bank), tagged with it. Real values only while that bank's
    // path is the one heard with no mode change in progress; otherwise the
    // data is "unavailable" (a bank in standby, or the 20 ms fade), never
    // shown as a reduction.
    const auto watched = dynamicsTelemetry.getWatchedBank();
    const auto bank = static_cast<dsp::StereoBank> (watched);
    const bool bankIsStereo = bank == dsp::StereoBank::stereo;
    const bool heard = ! engine.isFading() && ((engine.getHeardMode() == dsp::StereoMode::stereo) == bankIsStereo);
    const auto state = hostBypassed ? DynamicsTelemetry::State::hostBypassed
                     : heard        ? DynamicsTelemetry::State::live
                                    : DynamicsTelemetry::State::unavailable;
    const auto& eq = engine.getCore (bank);

    // Values the DSP has already computed for the last control tick; reading
    // them changes nothing (const accessors, audio thread).
    dynamicsTelemetry.offer (state, watched, [&eq] (int b, DynamicsTelemetry::Band& out)
    {
        const auto& band = eq.getBand (b);
        out.staticGainDb = (float) band.getCurrentGainDb();
        out.reductionDb = (float) eq.getDynamics (b).getGainReductionDb();
        out.frequencyHz = (float) band.getCurrentFrequencyHz();
        out.q = (float) band.getCurrentQ();
        out.bell = band.getActiveFilter() == dsp::FilterType::bell;
    });
}

void LabQ8AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    for (auto ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // The meters and the analyzer taps only read the buffer: IN / PRE
    // before the EQ (it works in place), OUT / POST after it.
    const auto numChannels = juce::jmin (numIn, buffer.getNumChannels());
    inputMeter.measure (buffer.getArrayOfReadPointers(), numChannels, buffer.getNumSamples());
    preTap.write (buffer.getArrayOfReadPointers(), numChannels, buffer.getNumSamples(), streamPosition);

    pushParametersToCore();
    auto* const* channels = buffer.getArrayOfWritePointers();
    float* segment[dsp::EqCore::maxChannels] {};
    const auto segmentChannels = juce::jmin (numChannels, dsp::EqCore::maxChannels);
    for (int position = 0; position < buffer.getNumSamples(); position += eqSubBlockSamples)
    {
        const auto n = juce::jmin (eqSubBlockSamples, buffer.getNumSamples() - position);
        for (int ch = 0; ch < segmentChannels; ++ch)
            segment[ch] = channels[ch] + position;
        engine.process (segment, segmentChannels, n);
        offerDynamics (false);
    }

    outputMeter.measure (buffer.getArrayOfReadPointers(), numChannels, buffer.getNumSamples());
    postTap.write (buffer.getArrayOfReadPointers(), numChannels, buffer.getNumSamples(), streamPosition);
    streamPosition += (std::uint64_t) buffer.getNumSamples(); // E4d: PRE and POST of one block share their stream position
}

void LabQ8AudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const auto numChannels = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    inputMeter.measure (buffer.getArrayOfReadPointers(), numChannels, buffer.getNumSamples());
    preTap.write (buffer.getArrayOfReadPointers(), numChannels, buffer.getNumSamples(), streamPosition);
    AudioProcessor::processBlockBypassed (buffer, midi);
    outputMeter.measure (buffer.getArrayOfReadPointers(), numChannels, buffer.getNumSamples());
    postTap.write (buffer.getArrayOfReadPointers(), numChannels, buffer.getNumSamples(), streamPosition);
    streamPosition += (std::uint64_t) buffer.getNumSamples(); // E4d: PRE and POST of one block share their stream position
    offerDynamics (true); // nothing was processed: no dynamics to show
}

juce::AudioProcessorEditor* LabQ8AudioProcessor::createEditor()
{
    // UI-1: the approved design (docs/design/LABQ8_UI_DESIGN_APPROVED_01.png).
    return new ui::LabQ8Editor (*this);
}

void LabQ8AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (stateVersionProperty, stateVersion, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void LabQ8AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return; // unknown or corrupt data: keep current state

    auto tree = juce::ValueTree::fromXml (*xml);
    params::migrateState (tree, parameters);
    parameters.replaceState (tree);
}
} // namespace labq8

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new labq8::LabQ8AudioProcessor();
}
