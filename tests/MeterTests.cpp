// UI-2a: IN / OUT sample-peak meters of the processor (audio side). The
// meters only read the audio; the bit-exactness of the output is also
// checked against older versions by tests/compat/run_compat.sh.

#pragma clang diagnostic ignored "-Wfloat-equal" // exact peaks are a tested property

#include "AllocationGuard.h"
#include "TestHarness.h"
#include "TestSignals.h"
#include "parameters/ParameterIDs.h"
#include "plugin/LevelMeter.h"
#include "plugin/PluginProcessor.h"
#include "ui/LabQ8Editor.h"
#include "ui/LevelMeterView.h"
#include "ui/UiGeometry.h"
#include "ui/UiModel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

using labq8::LabQ8AudioProcessor;
using labq8::LevelMeter;
using labq8::test::ScopedAllocationCounter;
using labq8::test::str;
namespace params = labq8::params;

namespace
{
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

// A 1 kHz sine at 48 kHz (48 samples per period, one sample on each crest),
// so its sample peak is exactly `amplitude`.
float crestSine (int index, float amplitude)
{
    return (float) (amplitude * std::sin (2.0 * 3.14159265358979323846 * index / 48.0));
}

struct Peaks
{
    float inL, inR, outL, outR;
};

Peaks take (LabQ8AudioProcessor& p)
{
    auto& in = p.getInputMeter();
    auto& out = p.getOutputMeter();
    return { in.takePeak (0), in.takePeak (1), out.takePeak (0), out.takePeak (1) };
}

// Runs `seconds` of the signal f(channel, index) through processBlock.
template <typename Signal>
void run (LabQ8AudioProcessor& p, int channels, double seconds, int blockSize, Signal&& f, int startIndex = 0)
{
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (channels, blockSize);
    const auto total = (int) std::lround (seconds * 48000.0);
    for (int pos = 0; pos < total; pos += blockSize)
    {
        const auto n = std::min (blockSize, total - pos);
        buffer.setSize (channels, n, false, false, true);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < n; ++i)
                buffer.setSample (ch, i, f (ch, startIndex + pos + i));
        p.processBlock (buffer, midi);
    }
}
} // namespace

LQ_TEST ("meter: silence reads exactly 0; sample peaks of known signals, IN before and OUT after the EQ, above 0 dBFS too")
{
    LabQ8AudioProcessor p;
    setLayout (p, 2);
    p.prepareToPlay (48000.0, 512);

    run (p, 2, 0.5, 512, [] (int, int) { return 0.0f; });
    auto m = take (p);
    LQ_CHECK (m.inL == 0.0f && m.inR == 0.0f && m.outL == 0.0f && m.outR == 0.0f,
              str ("silence: ", m.inL, " ", m.inR, " ", m.outL, " ", m.outR));
    LQ_CHECK (p.getInputMeter().getNumChannels() == 2 && p.getOutputMeter().getNumChannels() == 2, "stereo: two channels");

    // -6.02 dBFS sine through a flat EQ: IN and OUT exactly the amplitude.
    run (p, 2, 0.5, 512, [] (int, int i) { return crestSine (i, 0.5f); });
    m = take (p);
    LQ_CHECK (m.inL == 0.5f && m.inR == 0.5f && m.outL == 0.5f && m.outR == 0.5f,
              str ("-6 dBFS sine: ", m.inL, " ", m.inR, " ", m.outL, " ", m.outR));

    // +6 dB Bell at 1 kHz: OUT follows the EQ (steady state), IN does not.
    setParam (p, params::bandIDs[0].frequency, 1000.0f);
    setParam (p, params::bandIDs[0].gain, 6.0f);
    setParam (p, params::bandIDs[0].q, 1.0f);
    run (p, 2, 0.5, 512, [] (int, int i) { return crestSine (i, 0.25f); });
    take (p); // the ramp to the new gain
    run (p, 2, 0.5, 512, [] (int, int i) { return crestSine (i, 0.25f); });
    m = take (p);
    const auto outDb = 20.0 * std::log10 (m.outL / 0.25);
    LQ_CHECK (m.inL == 0.25f && std::abs (outDb - 6.0) < 0.1, str ("Bell +6 dB: IN ", m.inL, ", OUT gain ", outDb, " dB"));

    // Above 0 dBFS: +6.02 dBFS samples are measured, never clipped.
    setParam (p, params::bandIDs[0].gain, 0.0f);
    run (p, 2, 0.5, 512, [] (int, int i) { return crestSine (i, 2.0f); });
    take (p);
    run (p, 2, 0.2, 512, [] (int, int i) { return crestSine (i, 2.0f); });
    m = take (p);
    LQ_CHECK (m.inL == 2.0f && m.outL == 2.0f && m.outR == 2.0f, str ("+6 dBFS: IN ", m.inL, " OUT ", m.outL, " ", m.outR));

    // Back to silence: exactly 0 again (nothing held, nothing invented).
    run (p, 2, 0.1, 512, [] (int, int) { return 0.0f; });
    take (p);
    run (p, 2, 0.1, 512, [] (int, int) { return 0.0f; });
    m = take (p);
    LQ_CHECK (m.inL == 0.0f && m.inR == 0.0f && m.outL == 0.0f && m.outR == 0.0f, "silence again reads 0");
}

LQ_TEST ("meter: channels are independent; a mono track measures its one channel and nothing else")
{
    LabQ8AudioProcessor p;
    setLayout (p, 2);
    p.prepareToPlay (48000.0, 256);
    run (p, 2, 0.2, 256, [] (int ch, int i) { return ch == 0 ? crestSine (i, 0.5f) : 0.0f; });
    auto m = take (p);
    LQ_CHECK (m.inL == 0.5f && m.outL == 0.5f && m.inR == 0.0f && m.outR == 0.0f, str ("L only: ", m.inL, " ", m.inR, " ", m.outL, " ", m.outR));
    run (p, 2, 0.2, 256, [] (int ch, int i) { return ch == 1 ? crestSine (i, 0.125f) : 0.0f; });
    m = take (p);
    LQ_CHECK (m.inR == 0.125f && m.outR == 0.125f && m.inL == 0.0f && m.outL == 0.0f, str ("R only: ", m.inL, " ", m.inR, " ", m.outL, " ", m.outR));
    run (p, 2, 0.2, 256, [] (int ch, int i) { return crestSine (i, ch == 0 ? 0.5f : 0.0625f); });
    m = take (p);
    LQ_CHECK (m.inL == 0.5f && m.inR == 0.0625f, "different levels per channel");

    LabQ8AudioProcessor mono;
    setLayout (mono, 1);
    mono.prepareToPlay (48000.0, 256);
    run (mono, 1, 0.2, 256, [] (int, int i) { return crestSine (i, 0.5f); });
    LQ_CHECK (mono.getInputMeter().getNumChannels() == 1 && mono.getOutputMeter().getNumChannels() == 1, "mono: one channel");
    m = take (mono);
    LQ_CHECK (m.inL == 0.5f && m.outL == 0.5f, str ("mono level: ", m.inL, " ", m.outL));
    LQ_CHECK (m.inR == 0.0f && m.outR == 0.0f, "no second channel is measured or invented");
}

LQ_TEST ("meter: a one-sample peak is caught at any position and block size; held until taken, then reset")
{
    std::mt19937 rng (7);
    for (const int blockSize : { 1, 64, 512, 4096 })
        for (const float spike : { 0.9f, -0.9f })
        {
            LabQ8AudioProcessor p;
            setLayout (p, 2);
            p.prepareToPlay (48000.0, blockSize);
            const auto position = std::uniform_int_distribution<int> (0, 8191) (rng);
            const auto noise = labq8::test::signals::makeNoise (8192, 99, 0.01f);
            run (p, 2, 8192.0 / 48000.0, blockSize,
                 [&] (int ch, int i) { return ch == 0 && i == position ? spike : noise[(size_t) i]; });
            const auto m = take (p);
            LQ_CHECK (m.inL == 0.9f, str ("spike ", spike, " at ", position, ", blocks of ", blockSize, ": IN ", m.inL));
            LQ_CHECK (m.inR <= 0.01f && m.inR > 0.0f, str ("R keeps its own level: ", m.inR));
        }

    // Held across many blocks until taken; the next take starts afresh.
    LabQ8AudioProcessor p;
    setLayout (p, 2);
    p.prepareToPlay (48000.0, 64);
    run (p, 2, 64.0 / 48000.0, 64, [] (int, int i) { return i == 10 ? 0.7f : 0.0f; });
    run (p, 2, 0.5, 64, [] (int, int) { return 0.001f; });
    auto m = take (p);
    LQ_CHECK (m.inL == 0.7f && m.outL == 0.7f, str ("peak held over 375 blocks: ", m.inL));
    run (p, 2, 0.1, 64, [] (int, int) { return 0.001f; });
    m = take (p);
    LQ_CHECK (m.inL == 0.001f, str ("after a take, only what came later: ", m.inL));
    m = take (p);
    LQ_CHECK (m.inL == 0.0f && m.outL == 0.0f, "nothing processed since: 0");

    // prepareToPlay forgets an old peak.
    run (p, 2, 0.01, 64, [] (int, int) { return 0.5f; });
    p.prepareToPlay (48000.0, 64);
    m = take (p);
    LQ_CHECK (m.inL == 0.0f && m.outL == 0.0f, "reset by prepareToPlay");
}

LQ_TEST ("meter: host bypass passes the signal through unchanged and meters it (IN = OUT); NaN / Inf read as above the scale")
{
    for (const int channels : { 1, 2 })
    {
        LabQ8AudioProcessor p;
        setLayout (p, channels);
        setParam (p, params::bandIDs[0].gain, 12.0f); // would change the sound if processed
        p.prepareToPlay (48000.0, 512);
        juce::MidiBuffer midi;
        bool unchanged = true;
        for (int block = 0; block < 20; ++block)
        {
            juce::AudioBuffer<float> buffer (channels, 512);
            const auto noise = labq8::test::signals::makeNoise (512 * (size_t) channels, 400u + (unsigned) block, 0.3f);
            for (int ch = 0; ch < channels; ++ch)
                buffer.copyFrom (ch, 0, noise.data() + ch * 512, 512);
            const auto original = buffer;
            p.processBlockBypassed (buffer, midi);
            for (int ch = 0; ch < channels; ++ch)
                unchanged = unchanged && std::equal (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + 512, original.getReadPointer (ch));
        }
        LQ_CHECK (unchanged, str ("bypass changed the audio, channels=", channels));
        const auto m = take (p);
        LQ_CHECK (m.inL > 0.0f && m.inL == m.outL && m.inR == m.outR, str ("bypass: IN = OUT, channels=", channels, ": ", m.inL, " ", m.outL));
        LQ_CHECK (channels == 2 || m.inR == 0.0f, "mono bypass: one channel");
    }

    for (const float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() })
    {
        std::vector<float> block (256, 0.1f);
        block[100] = bad;
        LQ_CHECK (std::isinf (LevelMeter::blockPeak (block.data(), 256)), str ("non-finite sample ", bad, " reads +inf"));
    }
    const std::vector<float> fine { 0.1f, -0.75f, 0.5f };
    LQ_CHECK (LevelMeter::blockPeak (fine.data(), 3) == 0.75f && LevelMeter::blockPeak (fine.data(), 0) == 0.0f, "finite peaks");
}

LQ_TEST ("meter rt: lock-free atomics; no allocation while metering in processBlock and processBlockBypassed")
{
    LQ_CHECK (std::atomic<float>::is_always_lock_free && std::atomic<int>::is_always_lock_free, "lock-free");
    for (const int channels : { 1, 2 })
    {
        LabQ8AudioProcessor p;
        setLayout (p, channels);
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buffer (channels, 512);
        juce::MidiBuffer midi;
        long allocations = 0;
        for (int block = 0; block < 200; ++block)
        {
            const auto noise = labq8::test::signals::makeNoise (512, (std::uint32_t) block);
            for (int ch = 0; ch < channels; ++ch)
                buffer.copyFrom (ch, 0, noise.data(), 512);
            ScopedAllocationCounter counter;
            if (block % 4 == 3)
                p.processBlockBypassed (buffer, midi);
            else
                p.processBlock (buffer, midi);
            allocations += counter.count();
        }
        LQ_CHECK (allocations == 0, str ("allocated ", allocations, " times, channels=", channels));
    }
}

LQ_TEST ("meter concurrency: the editor taking the peaks at any moment never loses one and never reads one twice")
{
    // Unique values (a shuffled 1..N) written by an "audio" thread while an
    // "editor" thread takes them as fast as it can.
    //
    // The check relies only on the meter's own atomic (no ordering between
    // different atomics is assumed: with relaxed / acquire-release memory
    // that ordering is not guaranteed). The writes of one thread to one
    // atomic keep their order, so the takes cut the written sequence into
    // consecutive segments, and each take must return exactly the largest
    // value of its segment (0 if empty); the take after the writer ends
    // closes the last one. The test checks that such a cut exists:
    //   - the non-zero takes are values that were written, in increasing
    //     write position (none invented, none read twice);
    //   - before the first one, nothing larger was written;
    //   - between two consecutive ones at positions p < q, the cut can go
    //     at the first value after p larger than r_p (or at q), and nothing
    //     from there up to q may exceed r_q;
    //   - after the last one, nothing larger was written.
    // A writer that stores max(current, peak) after its load instead of the
    // CAS republishes a value already taken; a take that is not one atomic
    // exchange loses values. Both break the cut. Six rounds, so that such
    // interleavings are all but certain to occur.
    bool lost = false, twice = false, invented = false;
    long totalTakes = 0, totalNonZero = 0;
    for (const unsigned seed : { 5u, 6u, 7u, 8u, 9u, 10u })
    {
        constexpr int n = 400000;
        std::vector<float> values (n);
        std::iota (values.begin(), values.end(), 1.0f);
        std::shuffle (values.begin(), values.end(), std::mt19937 (seed));
        std::vector<int> position ((size_t) n + 1);
        for (int k = 0; k < n; ++k)
            position[(size_t) values[(size_t) k]] = k;

        LevelMeter meter;
        std::atomic<bool> done { false };
        std::thread audio ([&]
        {
            for (int k = 0; k < n; ++k)
                meter.accumulate (0, values[(size_t) k]);
            done.store (true, std::memory_order_release);
        });

        std::vector<float> taken;
        taken.reserve (4000000);
        while (! done.load (std::memory_order_acquire) && taken.size() < taken.capacity() - 1)
            taken.push_back (meter.takePeak (0));
        audio.join();
        taken.push_back (meter.takePeak (0));

        std::vector<int> at; // write positions of the non-zero takes, in order
        for (const auto r : taken)
        {
            if (r == 0.0f)
                continue;
            const auto v = (int) r;
            if ((float) v != r || v < 1 || v > n)
            {
                invented = true;
                continue;
            }
            if (! at.empty() && position[(size_t) v] <= at.back())
                twice = true;
            at.push_back (position[(size_t) v]);
        }
        auto rangeMax = [&] (int from, int to) // values[from, to), 0 if empty
        {
            float m = 0.0f;
            for (int k = from; k < to; ++k)
                m = std::max (m, values[(size_t) k]);
            return m;
        };
        if (at.empty() || twice || invented)
            lost = lost || at.empty();
        else
        {
            lost = lost || rangeMax (0, at.front()) > values[(size_t) at.front()];
            for (size_t j = 1; j < at.size(); ++j)
            {
                const auto p = at[j - 1], q = at[j];
                auto cut = p + 1;
                while (cut < q && values[(size_t) cut] < values[(size_t) p])
                    ++cut;
                lost = lost || rangeMax (cut, q) > values[(size_t) q];
            }
            lost = lost || rangeMax (at.back() + 1, n) > values[(size_t) at.back()];
        }
        totalTakes += (long) taken.size();
        totalNonZero += (long) at.size();
    }
    LQ_CHECK (! lost, "a peak was lost (no cut of the writes explains the takes)");
    LQ_CHECK (! twice, "a peak was read twice");
    LQ_CHECK (! invented, "a value that was never written was read");
    ctx.note (str ("6 rounds: ", totalTakes, " takes during 6 x 400000 writes, ", totalNonZero, " non-zero"));
    LQ_CHECK (totalNonZero > 1000, str ("the threads really overlapped: ", totalNonZero, " non-zero takes"));
}

//==============================================================================
// UI-2a: the meters of the editor (scale, colours, ballistics, painting).
namespace
{
namespace ui = labq8::ui;
namespace geo = labq8::ui::geometry;

std::unique_ptr<ui::LabQ8Editor> openEditor (LabQ8AudioProcessor& p)
{
    return std::unique_ptr<ui::LabQ8Editor> (dynamic_cast<ui::LabQ8Editor*> (p.createEditor()));
}

// Plays `blocks` of 480 samples of a crest sine (`amplitude` on L, `right`
// on R) through the processor.
void play (LabQ8AudioProcessor& p, int channels, int blocks, float amplitude, float right)
{
    juce::AudioBuffer<float> buffer (channels, 480);
    juce::MidiBuffer midi;
    for (int block = 0; block < blocks; ++block)
    {
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < 480; ++i)
                buffer.setSample (ch, i, crestSine (block * 480 + i, ch == 0 ? amplitude : right));
        p.processBlock (buffer, midi);
    }
}
} // namespace

LQ_TEST ("meter scale: 26 cells on the printed scale; green up to 0 dBFS, amber to +6, red above")
{
    // Expected thresholds, bottom to top (the scale at each cell's centre
    // row; the "0" cell at exactly 0, the top one at +12).
    const double expected[] = { -49.3, -43.1, -37.2, -31.2, -25.3, -22.1, -19.6, -17.0, -14.6, -12.3, -10.8, -9.4, -7.7,
                                -6.1, -4.6, -3.1, -1.6, 0.0, 1.3, 2.8, 4.4, 5.9, 7.4, 9.0, 10.6, 12.0 };
    bool matches = true, rising = true;
    for (int c = 0; c < geo::numMeterCells; ++c)
    {
        const auto t = ui::meterCellThresholdDb (c);
        matches = matches && std::abs (t - expected[c]) < 0.06;
        rising = rising && (c == 0 || t > ui::meterCellThresholdDb (c - 1));
    }
    LQ_CHECK (matches && rising, "thresholds");
    LQ_CHECK (ui::meterCellThresholdDb (17) == 0.0 && ui::meterCellThresholdDb (25) == 12.0, "0 and +12 exactly");

    LQ_CHECK (ui::meterLitCells (-std::numeric_limits<double>::infinity()) == 0 && ui::meterLitCells (-60.0) == 0
                  && ui::meterLitCells (ui::MeterBallistics::restDb) == 0, "silence lights nothing");
    LQ_CHECK (ui::meterLitCells (-49.0) == 1 && ui::meterLitCells (-49.6) == 0, "the lowest cell at about -49 dBFS");
    LQ_CHECK (ui::meterLitCells (-0.001) == 17 && ui::meterLitCells (0.0) == 18, "the 0 cell at full scale");
    LQ_CHECK (ui::meterLitCells (11.99) == 25 && ui::meterLitCells (12.0) == 26
                  && ui::meterLitCells (std::numeric_limits<double>::infinity()) == 26, "top cell at +12");

    int green = 0, amber = 0, red = 0;
    bool ordered = true;
    for (int c = 0; c < geo::numMeterCells; ++c)
    {
        const auto colour = ui::meterCellColour (c);
        const auto t = ui::meterCellThresholdDb (c);
        green += colour == ui::MeterColour::green;
        amber += colour == ui::MeterColour::amber;
        red += colour == ui::MeterColour::red;
        ordered = ordered && (colour == ui::MeterColour::green) == (t <= 0.0) && (colour == ui::MeterColour::red) == (t > 6.0);
    }
    LQ_CHECK (green == 18 && amber == 4 && red == 4 && ordered, str ("colours: ", green, " green, ", amber, " amber, ", red, " red"));

    // Geometry: 26 cells per column, top to bottom, not overlapping; the
    // four columns with the design's spans.
    bool cellsOrdered = true;
    for (size_t t = 1; t < geo::meterCells.size(); ++t)
        cellsOrdered = cellsOrdered && geo::meterCells[t].top > geo::meterCells[t - 1].bottom;
    LQ_CHECK (cellsOrdered && geo::meterCells[(size_t) geo::meterZeroCell].top == 298.0f
                  && geo::meterCells[(size_t) geo::meterZeroCell + 1].top == 307.0f, "cell rows");
}

LQ_TEST ("meter ballistics: instant rise, linear fall of 11.8 dB/s independent of the frame rate, then rest")
{
    ui::MeterBallistics m;
    LQ_CHECK (m.atRest() && m.update (0.0f, 0.016) == ui::MeterBallistics::restDb, "starts at rest; silence keeps it there");
    LQ_CHECK (std::abs (m.update (0.5f, 0.016) - 20.0 * std::log10 (0.5)) < 1e-9, "instant rise to the peak");
    LQ_CHECK (std::abs (m.update (0.0f, 1.0) - (20.0 * std::log10 (0.5) - 11.8)) < 1e-9, "falls 11.8 dB in one second");

    ui::MeterBallistics fast, slow;
    fast.update (1.0f, 0.0);
    slow.update (1.0f, 0.0);
    for (int frame = 0; frame < 120; ++frame)
        fast.update (0.0f, 1.0 / 120.0);
    for (int frame = 0; frame < 30; ++frame)
        slow.update (0.0f, 1.0 / 30.0);
    LQ_CHECK (std::abs (fast.levelDb - slow.levelDb) < 1e-9 && std::abs (fast.levelDb + 11.8) < 1e-9,
              str ("120 vs 30 frames per second: ", fast.levelDb, " / ", slow.levelDb));

    // A smaller peak while falling does not pull the level down.
    ui::MeterBallistics hold;
    hold.update (1.0f, 0.0);
    hold.update (0.1f, 0.5);
    LQ_CHECK (std::abs (hold.levelDb + 5.9) < 1e-9, "a lower peak during the fall changes nothing");

    // Above the scale (+inf) is held at the ceiling, then falls to rest.
    ui::MeterBallistics over;
    LQ_CHECK (over.update (std::numeric_limits<float>::infinity(), 0.016) == ui::MeterBallistics::ceilingDb, "+inf at the ceiling");
    LQ_CHECK (ui::meterLitCells (over.levelDb) == 26, "every cell");
    over.update (0.0f, (ui::MeterBallistics::ceilingDb - ui::MeterBallistics::restDb) / 11.8 + 0.01);
    LQ_CHECK (over.atRest() && ui::meterLitCells (over.levelDb) == 0, "at rest");
}

LQ_TEST ("meter view: real levels light the right cells; mono in both columns; nothing repainted at rest; old peaks dropped")
{
    LabQ8AudioProcessor p;
    setLayout (p, 2);
    p.prepareToPlay (48000.0, 480);
    play (p, 2, 10, 0.9f, 0.9f); // measured before the editor opens
    auto editor = openEditor (p);
    auto& meters = editor->getMeters();
    meters.advance (0.0);
    for (int c = 0; c < 4; ++c)
        LQ_CHECK (meters.getLitCells (c) == 0, "peaks from before the editor opened are not shown");

    // Silence from the start: no painting at all.
    for (int frame = 0; frame < 120; ++frame)
    {
        play (p, 2, 1, 0.0f, 0.0f);
        meters.advance (1.0 / 60.0);
    }
    LQ_CHECK (meters.getRepaintRequests() == 0, str ("silence repainted ", meters.getRepaintRequests(), " times"));

    // -12 dBFS on L, -30 dBFS on R: IN and OUT (flat EQ) the same.
    play (p, 2, 1, std::pow (10.0f, -12.0f / 20.0f), std::pow (10.0f, -30.0f / 20.0f));
    meters.advance (1.0 / 60.0);
    const auto l = ui::meterLitCells (-12.0), r = ui::meterLitCells (-30.0);
    LQ_CHECK (meters.getLitCells (0) == l && meters.getLitCells (2) == l && meters.getLitCells (1) == r && meters.getLitCells (3) == r,
              str ("lit ", meters.getLitCells (0), " ", meters.getLitCells (1), " ", meters.getLitCells (2), " ", meters.getLitCells (3)));
    const auto afterSignal = meters.getRepaintRequests();
    LQ_CHECK (afterSignal == 4, str ("one repaint per column that changed: ", afterSignal));

    // Silence: falls, then rests; once at rest, no more repaints.
    int frames = 0;
    while (frames < 60 * 20 && ! (meters.getLitCells (0) == 0 && meters.getLitCells (1) == 0))
    {
        play (p, 2, 1, 0.0f, 0.0f);
        meters.advance (1.0 / 60.0);
        ++frames;
    }
    const auto expectedSeconds = (-12.0 - ui::meterCellThresholdDb (0)) / 11.8;
    LQ_CHECK (std::abs (frames / 60.0 - expectedSeconds) < 2.0 / 60.0, str ("fell in ", frames / 60.0, " s, expected ", expectedSeconds));
    const auto atRest = meters.getRepaintRequests();
    for (int frame = 0; frame < 600; ++frame)
    {
        play (p, 2, 1, 0.0f, 0.0f);
        meters.advance (1.0 / 60.0);
    }
    LQ_CHECK (meters.getRepaintRequests() == atRest, "no repaint at rest");
    LQ_CHECK (atRest - afterSignal <= 2 * (l + r), "at most one repaint per cell going out");

    // Mono: its one channel in both columns of IN and of OUT.
    LabQ8AudioProcessor mono;
    setLayout (mono, 1);
    mono.prepareToPlay (48000.0, 480);
    auto monoEditor = openEditor (mono);
    play (mono, 1, 1, 0.5f, 0.0f);
    monoEditor->getMeters().advance (1.0 / 60.0);
    const auto m = ui::meterLitCells (20.0 * std::log10 (0.5));
    auto& mm = monoEditor->getMeters();
    LQ_CHECK (mm.getLitCells (0) == m && mm.getLitCells (1) == m && mm.getLitCells (2) == m && mm.getLitCells (3) == m,
              str ("mono lit ", mm.getLitCells (0), " ", mm.getLitCells (1), " ", mm.getLitCells (2), " ", mm.getLitCells (3)));

    // The meters never take the mouse.
    bool takes = true, children = true;
    meters.getInterceptsMouseClicks (takes, children);
    LQ_CHECK (! takes && ! children, "meters ignore the mouse");
}

LQ_TEST ("meter view: at the master's level the meters are drawn exactly as in the approved design")
{
    const auto masterFile = juce::File (LABQ8_TEST_FIXTURES_DIR).getParentDirectory().getParentDirectory()
                                .getChildFile ("docs/design/LABQ8_UI_DESIGN_APPROVED_01.png");
    const auto master = juce::ImageFileFormat::loadFrom (masterFile).convertedToFormat (juce::Image::RGB);
    if (! master.isValid())
    {
        ctx.skip ("master PNG not found");
        return;
    }

    LabQ8AudioProcessor p;
    setLayout (p, 2);
    p.prepareToPlay (48000.0, 480);
    auto editor = openEditor (p);
    editor->setZoom (1.0f);
    play (p, 2, 1, std::pow (10.0f, -1.0f / 20.0f), std::pow (10.0f, -1.0f / 20.0f)); // 17 cells, as in the master
    editor->getMeters().advance (0.0);
    for (int c = 0; c < 4; ++c)
        LQ_CHECK (editor->getMeters().getLitCells (c) == 17, "17 cells lit");
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f).convertedToFormat (juce::Image::RGB);

    int worst = 0;
    for (int y = 300; y < 480; ++y)
        for (int x = 1385; x < 1555; ++x)
        {
            const auto a = image.getPixelAt (x, y), b = master.getPixelAt (x, y);
            worst = std::max ({ worst, std::abs (a.getRed() - b.getRed()), std::abs (a.getGreen() - b.getGreen()), std::abs (a.getBlue() - b.getBlue()) });
        }
    LQ_CHECK (worst == 0, str ("largest difference with the master's meters: ", worst, " of 255"));
}
