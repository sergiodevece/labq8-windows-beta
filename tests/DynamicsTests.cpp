// UI-2c: dynamics telemetry (audio side) and the dynamic view (editor side).
// The telemetry only copies values the DSP has computed; the bit-exactness of
// the output is also checked against older versions by tests/compat/run_compat.sh.

#pragma clang diagnostic ignored "-Wfloat-equal" // exact copies are a tested property

#include "AllocationGuard.h"
#include "TestHarness.h"
#include "parameters/ParameterIDs.h"
#include "plugin/DynamicsTelemetry.h"
#include "plugin/PluginProcessor.h"
#include "ui/DynamicsOverlay.h"
#include "ui/LabQ8Editor.h"
#include "ui/UiGeometry.h"
#include "ui/UiModel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

using labq8::DynamicsTelemetry;
using labq8::LabQ8AudioProcessor;
using labq8::test::ScopedAllocationCounter;
using labq8::test::str;
namespace params = labq8::params;
namespace dsp = labq8::dsp;

namespace
{
constexpr double pi = 3.14159265358979323846;

void setLayout (LabQ8AudioProcessor& p, int channels)
{
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    p.setBusesLayout (layout);
}

void setParam (LabQ8AudioProcessor& p, const char* id, float value)
{
    auto* parameter = p.getValueTreeState().getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

// Band b: Bell with dynamics on (threshold -40, range 12, fast attack).
void dynamicBell (LabQ8AudioProcessor& p, int b, float hz, float gainDb, float rangeDb = 12.0f)
{
    setParam (p, params::bandIDs[(size_t) b].type, 0.0f);
    setParam (p, params::bandIDs[(size_t) b].frequency, hz);
    setParam (p, params::bandIDs[(size_t) b].gain, gainDb);
    setParam (p, params::bandIDs[(size_t) b].q, 1.0f);
    setParam (p, params::dynamicIDs[(size_t) b].enabled, 1.0f);
    setParam (p, params::dynamicIDs[(size_t) b].threshold, -40.0f);
    setParam (p, params::dynamicIDs[(size_t) b].range, rangeDb);
    setParam (p, params::dynamicIDs[(size_t) b].attack, 1.0f);
    setParam (p, params::dynamicIDs[(size_t) b].release, 50.0f);
}

// Tone bursts: 1 kHz and 5 kHz, 30 ms on, 70 ms off (a moving reduction).
float burst (int ch, int i)
{
    const auto on = (i % 4800) < 1440;
    const auto x = 0.5 * std::sin (2.0 * pi * 1000.0 * i / 48000.0) + 0.3 * std::sin (2.0 * pi * 5000.0 * i / 48000.0 + ch);
    return on ? (float) x : 0.0f;
}

template <typename Signal, typename AfterBlock>
void run (LabQ8AudioProcessor& p, int channels, int totalSamples, int blockSize, Signal&& f, AfterBlock&& after, int startIndex = 0)
{
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (channels, blockSize);
    for (int pos = 0; pos < totalSamples; pos += blockSize)
    {
        const auto n = std::min (blockSize, totalSamples - pos);
        buffer.setSize (channels, n, false, false, true);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < n; ++i)
                buffer.setSample (ch, i, f (ch, startIndex + pos + i));
        p.processBlock (buffer, midi);
        after (buffer);
    }
}

DynamicsTelemetry::Snapshot takeSnapshot (LabQ8AudioProcessor& p)
{
    DynamicsTelemetry::Snapshot s;
    const auto ok = p.getDynamicsTelemetry().take (s);
    jassert (ok);
    juce::ignoreUnused (ok);
    return s;
}

DynamicsTelemetry::Band dspBand (const LabQ8AudioProcessor& p, int b)
{
    const auto& core = p.getEqCoreForTesting();
    const auto& band = core.getBand (b);
    return { (float) band.getCurrentGainDb(), (float) core.getDynamics (b).getGainReductionDb(), (float) band.getCurrentFrequencyHz(),
             (float) band.getCurrentQ(), band.getActiveFilter() == dsp::FilterType::bell };
}

bool sameBand (const DynamicsTelemetry::Band& a, const DynamicsTelemetry::Band& b)
{
    return a.staticGainDb == b.staticGainDb && a.reductionDb == b.reductionDb && a.frequencyHz == b.frequencyHz && a.q == b.q && a.bell == b.bell;
}

double totalReduction (const DynamicsTelemetry::Snapshot& s)
{
    double t = 0.0;
    for (const auto& b : s.bands)
        t += DynamicsTelemetry::effectiveReductionDb (b);
    return t;
}

// The test session of the first test: two dynamic Bells, a shelf with
// dyn_on stored.
void testSession (LabQ8AudioProcessor& p)
{
    setLayout (p, 2);
    dynamicBell (p, 0, 1000.0f, 6.0f);
    dynamicBell (p, 3, 5000.0f, 0.0f, 9.0f);
    setParam (p, params::bandIDs[5].type, 1.0f);
    setParam (p, params::bandIDs[5].gain, 4.0f);
    setParam (p, params::dynamicIDs[5].enabled, 1.0f);
}
} // namespace

LQ_TEST ("dynamics telemetry: up to 256 samples each block shows exactly what the DSP applied at its end, for all eight bands")
{
    for (const auto blockSize : { 1, 7, 16, 64, 200, 256 })
    {
        LabQ8AudioProcessor p;
        testSession (p);
        p.prepareToPlay (48000.0, blockSize);
        p.getDynamicsTelemetry().setActive (true);
        takeSnapshot (p);

        int mismatches = 0, acting = 0, blocks = 0;
        double maxReduction = 0.0;
        std::uint64_t lastOffered = 0;
        run (p, 2, 24000, blockSize, burst, [&] (juce::AudioBuffer<float>&)
        {
            const auto s = takeSnapshot (p);
            ++blocks;
            if (s.offered <= lastOffered || s.id != s.offered || s.hostBypassed)
                ++mismatches; // one offer per block, and it is the one shown
            lastOffered = s.offered;
            for (int b = 0; b < 8; ++b)
            {
                mismatches += sameBand (s.bands[(size_t) b], dspBand (p, b)) ? 0 : 1;
                if (s.bands[(size_t) b].reductionDb < 0.0f)
                    ++mismatches; // never negative: no upward expansion
            }
            maxReduction = std::max (maxReduction, (double) s.bands[0].reductionDb);
            acting += s.bands[0].reductionDb > 0.0f ? 1 : 0;
            if (s.bands[5].reductionDb != 0.0f || s.bands[5].bell || DynamicsTelemetry::effectiveReductionDb (s.bands[5]) != 0.0)
                ++mismatches; // the shelf never shows dynamics
        });
        LQ_CHECK (mismatches == 0 && acting > 0 && maxReduction > 3.0,
                  str ("block ", blockSize, ": ", mismatches, " mismatches, band 1 acting in ", acting, " of ", blocks, " blocks, max ", maxReduction, " dB"));
    }
}

LQ_TEST ("dynamics telemetry: in longer blocks (A4 + A2) the shown instant is the most reducing 256-sample sub-block end, all bands of that instant")
{
    for (const auto blockSize : { 512, 1024, 4096 })
    {
        // Reference: the same session in 256-sample blocks, every sub-block end read.
        LabQ8AudioProcessor ref, p;
        for (auto* x : { &ref, &p })
        {
            testSession (*x);
            x->prepareToPlay (48000.0, blockSize);
            x->getDynamicsTelemetry().setActive (true);
            takeSnapshot (*x);
        }
        std::vector<DynamicsTelemetry::Snapshot> subBlocks;
        std::vector<float> refOut;
        run (ref, 2, 48000, 256, burst, [&] (juce::AudioBuffer<float>& b)
        {
            subBlocks.push_back (takeSnapshot (ref));
            refOut.insert (refOut.end(), b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
        });

        int mismatches = 0, blocks = 0, notAtEnd = 0, outputDifferences = 0;
        size_t position = 0;
        run (p, 2, 48000, blockSize, burst, [&] (juce::AudioBuffer<float>& b)
        {
            const auto s = takeSnapshot (p);
            const auto per = (size_t) (b.getNumSamples() + 255) / 256;
            const auto first = (size_t) blocks * (size_t) (blockSize / 256);
            // The expected instant: the largest total, the first one on ties.
            size_t best = first;
            for (size_t k = first; k < first + per; ++k)
                if (totalReduction (subBlocks[k]) > totalReduction (subBlocks[best]))
                    best = k;
            for (int band = 0; band < 8; ++band)
                mismatches += sameBand (s.bands[(size_t) band], subBlocks[best].bands[(size_t) band]) ? 0 : 1;
            notAtEnd += best != first + per - 1 ? 1 : 0;
            outputDifferences += std::memcmp (b.getReadPointer (0), refOut.data() + position, sizeof (float) * (size_t) b.getNumSamples()) != 0 ? 1 : 0;
            position += (size_t) b.getNumSamples();
            ++blocks;
        });
        LQ_CHECK (mismatches == 0 && notAtEnd > 0 && outputDifferences == 0,
                  str ("block ", blockSize, ": ", mismatches, " mismatches; peak instant inside the block in ", notAtEnd, " of ", blocks,
                       " blocks; output differences vs 256-sample blocks: ", outputDifferences));
    }
}

LQ_TEST ("dynamics telemetry: A2 keeps the most reducing offer until the editor reads it, then follows the next one, whatever its value")
{
    DynamicsTelemetry t;
    t.setActive (true);
    auto offer = [&t] (float reduction)
    {
        t.offer (false, [reduction] (int b, DynamicsTelemetry::Band& out) { out = { 0.0f, b == 2 ? reduction : 0.0f, 1000.0f, 1.0f, true }; });
    };
    DynamicsTelemetry::Snapshot s;
    t.take (s);
    offer (1.0f);
    offer (5.0f);
    offer (3.0f);
    LQ_CHECK (t.take (s) && s.bands[2].reductionDb == 5.0f && s.offered == s.id + 1, str ("kept the peak: ", s.bands[2].reductionDb));
    offer (2.0f);
    LQ_CHECK (t.take (s) && s.bands[2].reductionDb == 2.0f, str ("after a read, the next offer: ", s.bands[2].reductionDb));
    const auto id = s.id, offered = s.offered;
    LQ_CHECK (t.take (s) && s.id == id && s.offered == offered, "no new offer: the same instant, offers unchanged (the editor sees audio stopped)");
    offer (0.0f);
    LQ_CHECK (t.take (s) && s.bands[2].reductionDb == 0.0f, "silence after a read replaces the peak at once");
    // Clamp: static -20 dB, reduction 12 dB: the band can only go to -24.
    DynamicsTelemetry::Band clamped { -20.0f, 12.0f, 1000.0f, 1.0f, true }, shelf { 3.0f, 6.0f, 1000.0f, 1.0f, false };
    LQ_CHECK (DynamicsTelemetry::effectiveReductionDb (clamped) == 4.0 && DynamicsTelemetry::effectiveReductionDb (shelf) == 0.0,
              str ("effective reduction with the +-24 dB clamp: ", DynamicsTelemetry::effectiveReductionDb (clamped)));
}

LQ_TEST ("dynamics telemetry: inactive offers nothing; host bypass shows no dynamics; prepareToPlay starts a new generation")
{
    LabQ8AudioProcessor p;
    setLayout (p, 2);
    dynamicBell (p, 0, 1000.0f, 0.0f);
    p.prepareToPlay (48000.0, 256);
    auto& t = p.getDynamicsTelemetry();
    const auto none = takeSnapshot (p);
    run (p, 2, 4800, 256, burst, [] (auto&) {});
    LQ_CHECK (takeSnapshot (p).offered == none.offered, "inactive: the audio thread offers nothing");

    t.setActive (true);
    run (p, 2, 4800, 256, burst, [] (auto&) {});
    auto s = takeSnapshot (p);
    LQ_CHECK (s.offered > none.offered && ! s.hostBypassed && s.bands[0].reductionDb > 1.0f && s.bands[0].bell,
              str ("active: published, band 1 reducing ", s.bands[0].reductionDb, " dB"));

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, 256);
    buffer.clear();
    p.processBlockBypassed (buffer, midi);
    s = takeSnapshot (p);
    auto anyReduction = false;
    for (const auto& b : s.bands)
        anyReduction = anyReduction || b.reductionDb != 0.0f || b.bell;
    LQ_CHECK (s.hostBypassed && ! anyReduction, "host bypass: flagged, no band shows dynamics");

    const auto generation = s.generation;
    p.prepareToPlay (48000.0, 256);
    s = takeSnapshot (p);
    LQ_CHECK (s.generation == generation + 1 && s.bands[0].reductionDb == 0.0f, str ("prepareToPlay: generation ", s.generation));
}

LQ_TEST ("dynamics telemetry: a type change away from Bell stops showing dynamics at once; band bypass releases it within the 20 ms fade")
{
    for (const auto bypassInstead : { false, true })
    {
        LabQ8AudioProcessor p;
        setLayout (p, 2);
        dynamicBell (p, 0, 1000.0f, 0.0f);
        p.prepareToPlay (48000.0, 64);
        p.getDynamicsTelemetry().setActive (true);
        auto tone = [] (int, int i) { return (float) (0.5 * std::sin (2.0 * pi * 1000.0 * i / 48000.0)); };
        run (p, 2, 9600, 64, tone, [] (auto&) {});
        const auto before = takeSnapshot (p).bands[0];

        if (bypassInstead)
            setParam (p, params::bandIDs[0].bypass, 1.0f);
        else
            setParam (p, params::bandIDs[0].type, 2.0f); // High Shelf
        int firstBlock = -1, block = 0, restBlock = -1;
        run (p, 2, 4800, 64, tone, [&] (auto&)
        {
            const auto s = takeSnapshot (p).bands[0];
            if (firstBlock < 0)
                firstBlock = s.bell ? 1 : 0;
            if (restBlock < 0 && s.reductionDb == 0.0f)
                restBlock = block;
            ++block;
        }, 9600);
        const auto restMs = restBlock * 64 / 48.0;
        if (bypassInstead)
            LQ_CHECK (before.reductionDb > 1.0f && firstBlock == 1 && restBlock >= 0 && restMs <= 21.4,
                      str ("bypass: was ", before.reductionDb, " dB, still Bell, 0 dB after ", restMs, " ms"));
        else
            LQ_CHECK (before.reductionDb > 1.0f && firstBlock == 0 && restBlock >= 0 && restMs <= 21.4,
                      str ("to High Shelf: was ", before.reductionDb, " dB, Bell shown: ", firstBlock, ", 0 dB after ", restMs, " ms"));
    }
}

LQ_TEST ("dynamics telemetry: offering never allocates and the output is bit-identical with the telemetry active or not")
{
    LabQ8AudioProcessor a, b;
    for (auto* p : { &a, &b })
    {
        setLayout (*p, 2);
        for (int band = 0; band < 8; ++band)
            dynamicBell (*p, band, 60.0f * std::pow (2.0f, (float) band), band % 2 == 0 ? 3.0f : -2.0f);
        p->prepareToPlay (48000.0, 512);
    }
    a.getDynamicsTelemetry().setActive (true);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> ba (2, 512), bb (2, 512);
    long allocations = 0;
    int differences = 0;
    for (int block = 0; block < 200; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                ba.setSample (ch, i, burst (ch, block * 512 + i));
        bb.makeCopyOf (ba, true);
        {
            ScopedAllocationCounter counter;
            if (block % 7 == 6)
                a.processBlockBypassed (ba, midi);
            else
                a.processBlock (ba, midi);
            allocations += counter.count();
        }
        if (block % 7 == 6)
            b.processBlockBypassed (bb, midi);
        else
            b.processBlock (bb, midi);
        for (int ch = 0; ch < 2; ++ch)
            differences += std::memcmp (ba.getReadPointer (ch), bb.getReadPointer (ch), sizeof (float) * 512) != 0 ? 1 : 0;
    }
    LQ_CHECK (allocations == 0, str ("allocations on the audio thread: ", allocations));
    LQ_CHECK (differences == 0, str ("blocks differing with the telemetry active: ", differences));
}

LQ_TEST ("dynamics telemetry: under a concurrent writer every accepted snapshot is one instant, never a mix; a read peak never sticks")
{
    DynamicsTelemetry t;
    t.setActive (true);
    std::atomic<bool> stop { false };
    auto encode = [] (std::uint64_t n, int b, DynamicsTelemetry::Band& out)
    {
        // Every field of offer n encodes n: a torn copy cannot agree with itself.
        const auto v = (float) ((n * 7919u + (std::uint64_t) b) % 1000u);
        out = { 30.0f, v / 100.0f, v + 2.0f, v + 3.0f, true };
    };
    std::thread writer ([&]
    {
        std::uint64_t n = 0;
        while (! stop.load (std::memory_order_relaxed))
        {
            ++n;
            t.offer (false, [&encode, n] (int b, DynamicsTelemetry::Band& out) { encode (n, b, out); });
        }
    });

    long accepted = 0, refused = 0, torn = 0;
    int backwards = 0;
    std::uint64_t lastId = 0;
    const auto end = juce::Time::getMillisecondCounterHiRes() + 1500.0;
    while (juce::Time::getMillisecondCounterHiRes() < end)
    {
        DynamicsTelemetry::Snapshot s;
        if (! t.take (s))
        {
            ++refused;
            continue;
        }
        ++accepted;
        backwards += s.id < lastId ? 1 : 0;
        lastId = s.id;
        if (s.id == 0)
            continue; // nothing offered yet: the initial values
        // The kept values belong to some offer m: find it from band 0 and check the others agree.
        const auto& b0 = s.bands[0];
        const auto v0 = b0.frequencyHz - 2.0f;
        for (int b = 1; b < 8; ++b)
        {
            const auto v = std::fmod (v0 + (float) b, 1000.0f);
            const auto& x = s.bands[(size_t) b];
            if (x.frequencyHz != v + 2.0f || x.q != v + 3.0f || x.reductionDb != v / 100.0f)
            {
                ++torn;
                break;
            }
        }
        if (b0.q != v0 + 3.0f || b0.reductionDb != v0 / 100.0f)
            ++torn;
    }
    stop = true;
    writer.join();
    // After the editor's read, any offer replaces the kept one, however small.
    DynamicsTelemetry::Snapshot s;
    t.take (s);
    t.offer (false, [] (int, DynamicsTelemetry::Band& out) { out = { 30.0f, 0.0f, 1.0f, 1.0f, true }; });
    const auto followed = t.take (s) && s.bands[0].reductionDb == 0.0f && s.bands[7].frequencyHz == 1.0f;
    LQ_CHECK (torn == 0 && backwards == 0 && accepted > 1000 && followed,
              str ("accepted ", accepted, ", refused ", refused, ", torn ", torn, ", backwards ", backwards, ", follows after a read: ", followed));
}

namespace
{
// The processor before A4: EqCore driven once per host block with the
// parameters read the same way (PluginProcessor::readBandSettings / readDynamicSettings).
void pushLikeProcessor (LabQ8AudioProcessor& p, dsp::EqCore& core)
{
    auto raw = [&p] (const char* id) { return p.getValueTreeState().getRawParameterValue (id)->load (std::memory_order_relaxed); };
    for (int b = 0; b < 8; ++b)
    {
        const auto& ids = params::bandIDs[(size_t) b];
        dsp::BandSettings s;
        s.frequencyHz = raw (ids.frequency);
        s.gainDb = raw (ids.gain);
        if (std::abs (s.gainDb) < 0.005)
            s.gainDb = 0.0;
        s.q = raw (ids.q);
        s.bypassed = raw (ids.bypass) >= 0.5f;
        s.type = dsp::filterTypeFromIndex ((int) std::lround (raw (ids.type)));
        core.setBandTargets (b, s);
        const auto& d = params::dynamicIDs[(size_t) b];
        core.setBandDynamics (b, dsp::DynamicSettings { raw (d.enabled) >= 0.5f, raw (d.threshold), raw (d.range), raw (d.attack), raw (d.release) });
    }
}
} // namespace

LQ_TEST ("dynamics A4: the processor's 256-sample sub-blocks give the same output, bit for bit, as one EQ call per host block (with automation)")
{
    int failures = 0;
    std::string detail;
    for (const auto blockSize : { 1, 100, 255, 256, 257, 511, 1000, 2049, 4096 })
    {
        LabQ8AudioProcessor p;
        setLayout (p, 2);
        for (int band = 0; band < 8; ++band)
            dynamicBell (p, band, 50.0f * std::pow (2.2f, (float) band), band % 3 == 0 ? 4.0f : -3.0f, 6.0f + (float) band);
        p.prepareToPlay (48000.0, blockSize);
        p.getDynamicsTelemetry().setActive (blockSize % 2 == 0); // the telemetry changes nothing either
        dsp::EqCore core;
        pushLikeProcessor (p, core);
        core.prepare (48000.0, 2);

        std::mt19937 random (77u + (unsigned) blockSize);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buffer (2, blockSize), reference (2, blockSize);
        int differences = 0, position = 0, nextChange = 2400;
        const auto total = 96000;
        while (position < total)
        {
            const auto n = std::min (blockSize, total - position);
            if (position >= nextChange)
            {
                // Automation between blocks: types, gains, bypass, dynamics on / off.
                const auto band = (int) (random() % 8);
                switch (random() % 5)
                {
                    case 0: setParam (p, params::bandIDs[(size_t) band].type, (float) (random() % 6)); break;
                    case 1: setParam (p, params::bandIDs[(size_t) band].gain, (float) ((int) (random() % 49) - 24)); break;
                    case 2: setParam (p, params::bandIDs[(size_t) band].bypass, (float) (random() % 2)); break;
                    case 3: setParam (p, params::dynamicIDs[(size_t) band].enabled, (float) (random() % 2)); break;
                    default: setParam (p, params::dynamicIDs[(size_t) band].threshold, -60.0f + (float) (random() % 60)); break;
                }
                nextChange += 1200 + (int) (random() % 4800);
            }
            buffer.setSize (2, n, false, false, true);
            reference.setSize (2, n, false, false, true);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    buffer.setSample (ch, i, burst (ch, position + i));
            reference.makeCopyOf (buffer, true);
            p.processBlock (buffer, midi);
            pushLikeProcessor (p, core);
            core.process (reference.getArrayOfWritePointers(), 2, n);
            for (int ch = 0; ch < 2; ++ch)
                differences += std::memcmp (buffer.getReadPointer (ch), reference.getReadPointer (ch), sizeof (float) * (size_t) n) != 0 ? 1 : 0;
            position += n;
        }
        if (differences != 0)
        {
            ++failures;
            detail += str (" block ", blockSize, ": ", differences, " differing channel-blocks;");
        }
    }
    LQ_CHECK (failures == 0, failures == 0 ? std::string ("bit-identical at 9 block sizes") : detail);
}

//==============================================================================
// UI-2c stage 2: the dynamic view.

namespace
{
using labq8::ui::LabQ8Editor;
using labq8::ui::DynamicsOverlay;

std::unique_ptr<LabQ8Editor> openEditor (LabQ8AudioProcessor& p)
{
    return std::unique_ptr<LabQ8Editor> (dynamic_cast<LabQ8Editor*> (p.createEditor()));
}

// `frames` screen frames of audio (1600 samples at 48 kHz each) in host
// blocks of `blockSize`, then one overlay frame each.
template <typename Signal>
void playFrames (LabQ8AudioProcessor& p, DynamicsOverlay& overlay, int frames, int blockSize, Signal&& f)
{
    static int position = 0;
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, blockSize);
    for (int frame = 0; frame < frames; ++frame)
    {
        for (int done = 0; done < 1600;)
        {
            const auto n = std::min (blockSize, 1600 - done);
            buffer.setSize (2, n, false, false, true);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    buffer.setSample (ch, i, f (ch, position + i));
            p.processBlock (buffer, midi);
            position += n;
            done += n;
        }
        overlay.advance (1.0 / 30.0);
    }
}

float steadyTones (int, int i)
{
    return (float) (0.4 * std::sin (2.0 * pi * 1000.0 * i / 48000.0) + 0.3 * std::sin (2.0 * pi * 300.0 * i / 48000.0));
}

// The DSP's composite response at the overlay's frequencies (dB).
double worstDifference (const LabQ8AudioProcessor& p, const DynamicsOverlay& overlay)
{
    const auto& f = overlay.getFrequencies();
    const auto& shown = overlay.getDynamicResponseDb();
    double worst = 0.0;
    for (size_t i = 0; i < f.size(); ++i)
        worst = std::max (worst, std::abs (shown[i] - 20.0 * std::log10 (p.getEqCoreForTesting().magnitudeAt (f[i]))));
    return worst;
}

// Session of the curve tests: three dynamic Bells, two overlapping, one
// clamped at -24 dB; a static Low Shelf and a shelf with dyn_on stored.
void curveSession (LabQ8AudioProcessor& p)
{
    setLayout (p, 2);
    dynamicBell (p, 0, 1000.0f, 6.0f, 12.0f);
    dynamicBell (p, 3, 1500.0f, -3.0f, 6.0f);
    dynamicBell (p, 6, 300.0f, -20.0f, 12.0f); // can only go 4 dB lower
    setParam (p, params::bandIDs[1].type, 1.0f);
    setParam (p, params::bandIDs[1].gain, 3.0f);
    setParam (p, params::bandIDs[1].frequency, 80.0f);
    setParam (p, params::bandIDs[5].type, 2.0f);
    setParam (p, params::bandIDs[5].gain, -4.0f);
    setParam (p, params::dynamicIDs[5].enabled, 1.0f);
}
} // namespace

LQ_TEST ("dynamics view: in steady state the dotted curve is the EQ's real response (EqCore::magnitudeAt), clamp included; transitions measured")
{
    LabQ8AudioProcessor p;
    curveSession (p);
    p.prepareToPlay (48000.0, 256);
    auto editor = openEditor (p);
    editor->setView (LabQ8Editor::viewDyn);
    auto& overlay = editor->getDynamics();
    playFrames (p, overlay, 45, 256, steadyTones); // 1.5 s: settled
    const auto& r = overlay.getEffectiveReductions();
    const auto steady = worstDifference (p, overlay);
    LQ_CHECK (overlay.isShowingDynamics() && std::abs (r[0] - 12.0) < 1.0e-3 && std::abs (r[3] - 6.0) < 1.0e-3 && std::abs (r[6] - 4.0) < 1.0e-3
                  && r[1] == 0.0 && r[5] == 0.0 && r[2] == 0.0,
              str ("effective reductions: b1 ", r[0], ", b4 ", r[3], ", b7 ", r[6], " (clamped from 12), shelves ", r[1], " ", r[5]));
    LQ_CHECK (steady < 0.01, str ("steady state: dotted curve vs EqCore::magnitudeAt, worst ", steady, " dB over ", overlay.getFrequencies().size(), " points"));
    // The static curve under it is the parameters' (what the nodes sit on).
    const auto staticNow = labq8::ui::cascadeResponseDb (editor->getScreenBandsForTesting(), 48000.0, overlay.getFrequencies());
    LQ_CHECK (staticNow == overlay.getStaticResponseDb(), "the static curve is the cached screen's");

    // Transitions: bursts, attack 1 ms. The overlay reads after every host
    // block of 256 (one offer each), so its instant is the end of that block
    // and can be compared with the DSP there.
    double transient = 0.0;
    int compared = 0;
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, 256);
    for (int block = 0; block < 400; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                buffer.setSample (ch, i, burst (ch, 200000 + block * 256 + i));
        p.processBlock (buffer, midi);
        overlay.advance (256.0 / 48000.0);
        if (overlay.isShowingDynamics() && overlay.getEffectiveReductions()[0] > 0.0)
        {
            transient = std::max (transient, worstDifference (p, overlay));
            ++compared;
        }
    }
    std::printf ("       measured: steady %.6f dB, transitions %.6f dB worst over %d instants\n", steady, transient, compared);
    LQ_CHECK (compared > 10 && transient < 0.05, str ("bursts: worst ", transient, " dB in ", compared, " instants"));
}

LQ_TEST ("dynamics view: only Bells that really reduce; silence returns to the static curve; 100 ms without audio, host bypass or a new stream remove it")
{
    LabQ8AudioProcessor p;
    curveSession (p);
    p.prepareToPlay (48000.0, 512);
    auto editor = openEditor (p);
    editor->setView (LabQ8Editor::viewAll);
    auto& overlay = editor->getDynamics();
    LQ_CHECK (p.getDynamicsTelemetry().isActive(), "ALL: the telemetry is on");
    playFrames (p, overlay, 30, 512, steadyTones);
    LQ_CHECK (overlay.isShowingDynamics() && overlay.getBandDeltaDb (5).empty() && overlay.getBandDeltaDb (1).empty() && ! overlay.areReadoutsShown(),
              "acting Bells only; ALL without readouts");
    for (size_t i = 0; i < overlay.getFrequencies().size(); ++i)
        if (overlay.getDynamicResponseDb()[i] > overlay.getStaticResponseDb()[i] + 1.0e-9)
        {
            LQ_CHECK (false, str ("the dynamic curve rises above the static one at ", overlay.getFrequencies()[i], " Hz (no expansion exists)"));
            break;
        }

    // Silence: the real release, then exactly the static curve.
    int frames = 0;
    while (overlay.isShowingDynamics() && frames < 90)
    {
        playFrames (p, overlay, 1, 512, [] (int, int) { return 0.0f; });
        ++frames;
    }
    LQ_CHECK (! overlay.isShowingDynamics() && overlay.getDynamicResponseDb().empty(), str ("silence: released after ", frames, " frames"));

    // Audio stops (no blocks): kept 100 ms, then removed without any invented release.
    playFrames (p, overlay, 20, 512, steadyTones);
    LQ_CHECK (overlay.isShowingDynamics(), "acting again");
    const auto before = overlay.getEffectiveReductions();
    overlay.advance (1.0 / 30.0);
    overlay.advance (1.0 / 30.0);
    LQ_CHECK (overlay.isShowingDynamics() && overlay.getEffectiveReductions() == before, "67 ms without audio: unchanged");
    overlay.advance (1.0 / 30.0);
    overlay.advance (1.0 / 30.0);
    LQ_CHECK (! overlay.isShowingDynamics(), "133 ms without audio: removed");

    // Host bypass.
    playFrames (p, overlay, 5, 512, steadyTones);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    p.processBlockBypassed (buffer, midi);
    overlay.advance (1.0 / 30.0);
    LQ_CHECK (! overlay.isShowingDynamics(), "host bypass: removed");

    // A new stream: nothing until its audio arrives.
    playFrames (p, overlay, 5, 512, steadyTones);
    p.prepareToPlay (48000.0, 512);
    overlay.advance (1.0 / 30.0);
    overlay.advance (1.0 / 30.0);
    LQ_CHECK (! overlay.isShowingDynamics(), "prepareToPlay: removed");

    // Showing again after a pause: nothing from before, only new audio.
    playFrames (p, overlay, 10, 512, steadyTones);
    editor->setView (LabQ8Editor::viewEq);
    editor->setView (LabQ8Editor::viewAll);
    overlay.advance (1.0 / 30.0);
    overlay.advance (1.0 / 30.0);
    LQ_CHECK (! overlay.isShowingDynamics(), "re-shown without new audio: nothing from before");
    playFrames (p, overlay, 2, 512, steadyTones);
    LQ_CHECK (overlay.isShowingDynamics(), "re-shown with new audio: shown");

    // EQ and SPECTRUM: the telemetry is off; closing the editor too.
    editor->setView (LabQ8Editor::viewEq);
    LQ_CHECK (! p.getDynamicsTelemetry().isActive() && ! overlay.isShown(), "EQ: no telemetry");
    editor->setView (LabQ8Editor::viewSpectrum);
    LQ_CHECK (! p.getDynamicsTelemetry().isActive(), "SPECTRUM: no telemetry");
    editor->setView (LabQ8Editor::viewDyn);
    editor.reset();
    LQ_CHECK (! p.getDynamicsTelemetry().isActive(), "editor closed: no telemetry");
}

LQ_TEST ("dynamics view: the static curve's cache is never repainted by the dynamics; nothing repaints at rest; DYN readouts sit under their points")
{
    LabQ8AudioProcessor p;
    curveSession (p);
    p.prepareToPlay (48000.0, 480);
    auto editor = openEditor (p);
    editor->setVisible (true);
    editor->setZoom (1.0f);
    editor->setView (LabQ8Editor::viewAll);
    auto& overlay = editor->getDynamics();
    auto snapshot = [&] { return editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f); };
    snapshot();
    const auto paints = editor->getScreenPaintCount();
    const auto overlayPaints = overlay.getPaintCount();
    for (int i = 0; i < 20; ++i)
    {
        playFrames (p, overlay, 1, 480, burst);
        snapshot();
    }
    LQ_CHECK (editor->getScreenPaintCount() == paints && overlay.getPaintCount() > overlayPaints + 5,
              str ("moving dynamics: static curve repainted ", editor->getScreenPaintCount() - paints, " times; overlay ",
                   overlay.getPaintCount() - overlayPaints));

    // At rest (reductions settled): no repaint requests.
    playFrames (p, overlay, 45, 480, steadyTones);
    const auto requests = overlay.getRepaintRequests();
    playFrames (p, overlay, 15, 480, steadyTones);
    LQ_CHECK (overlay.getRepaintRequests() == requests, str ("settled: ", overlay.getRepaintRequests() - requests, " repaint requests"));

    // A parameter change moves the static curve and the overlay with it.
    setParam (p, params::bandIDs[2].gain, 5.0f);
    editor->refreshFromParameters();
    snapshot();
    LQ_CHECK (editor->getScreenPaintCount() > paints && overlay.getRepaintRequests() > requests, "a parameter change redraws both");
    LQ_CHECK (overlay.getStaticResponseDb() == labq8::ui::cascadeResponseDb (editor->getScreenBandsForTesting(), 48000.0, overlay.getFrequencies()),
              "the overlay's static curve follows the new parameters");

    // DYN: one readout per acting band, under its dynamic point, inside the plot, not overlapping.
    editor->setView (LabQ8Editor::viewDyn);
    playFrames (p, overlay, 45, 480, steadyTones);
    const auto boxes = overlay.getReadoutBounds();
    namespace geo = labq8::ui::geometry;
    const juce::Rectangle<float> plot (geo::plotArea.x, geo::plotArea.y, geo::plotArea.w, geo::plotArea.h);
    auto inside = true, apart = true;
    for (size_t i = 0; i < boxes.size(); ++i)
    {
        inside = inside && plot.contains (boxes[i]);
        for (size_t j = i + 1; j < boxes.size(); ++j)
            apart = apart && ! boxes[i].intersects (boxes[j]);
    }
    LQ_CHECK (boxes.size() == 3 && inside && apart, str (boxes.size(), " readouts, inside ", inside, ", apart ", apart));
}
