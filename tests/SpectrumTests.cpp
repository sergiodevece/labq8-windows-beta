// UI-2b: spectrum analyzer. Capture of the PRE / POST audio (SpectrumTap),
// analysis (SpectrumAnalyzer) and the screen's spectrum view. The taps only
// read the audio; the bit-exactness of the output is also checked against
// older versions by tests/compat/run_compat.sh.

#pragma clang diagnostic ignored "-Wfloat-equal" // exact capture is a tested property

#include "AllocationGuard.h"
#include "TestHarness.h"
#include "TestSignals.h"
#include "parameters/ParameterIDs.h"
#include "plugin/PluginProcessor.h"
#include "plugin/SpectrumTap.h"
#include "ui/LabQ8Editor.h"
#include "ui/SpectrumAnalyzer.h"
#include "ui/SpectrumView.h"
#include "ui/UiGeometry.h"
#include "ui/UiModel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <thread>
#include <vector>

using labq8::LabQ8AudioProcessor;
using labq8::SpectrumTap;
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

// Copies the latest `count` samples of a tap (all of its history: from 0).
std::vector<std::vector<float>> latest (const SpectrumTap& tap, int channels, int count, SpectrumTap::Frame& frame, bool& coherent)
{
    std::vector<std::vector<float>> out ((size_t) channels, std::vector<float> ((size_t) count));
    std::vector<float*> dest;
    for (auto& v : out)
        dest.push_back (v.data());
    coherent = tap.copyLatest (dest.data(), channels, count, 0, frame);
    return out;
}
} // namespace

LQ_TEST ("spectrum tap: PRE holds exactly the input and POST exactly the output, any block size, across the ring's wrap")
{
    for (const int channels : { 1, 2 })
    {
        LabQ8AudioProcessor p;
        setLayout (p, channels);
        setParam (p, params::bandIDs[2].gain, 9.0f); // POST differs from PRE
        p.prepareToPlay (48000.0, 4096);
        p.getPreTap().setActive (true);
        p.getPostTap().setActive (true);

        std::mt19937 rng (3u + (unsigned) channels);
        std::uniform_int_distribution<int> blockSize (1, 4096);
        juce::MidiBuffer midi;
        std::vector<std::vector<float>> in ((size_t) channels), out ((size_t) channels);
        long total = 0;
        unsigned seed = 0;
        while (total < 3 * SpectrumTap::capacity + 777) // wraps the ring three times
        {
            const auto n = blockSize (rng);
            juce::AudioBuffer<float> buffer (channels, n);
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto noise = labq8::test::signals::makeNoise ((size_t) n, ++seed, 0.4f);
                buffer.copyFrom (ch, 0, noise.data(), n);
                in[(size_t) ch].insert (in[(size_t) ch].end(), noise.begin(), noise.end());
            }
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < channels; ++ch)
                out[(size_t) ch].insert (out[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n);
            total += n;
        }

        for (const int count : { 1, 8192, 32768 })
        {
            SpectrumTap::Frame frame;
            bool coherent = false;
            const auto pre = latest (p.getPreTap(), channels, count, frame, coherent);
            bool samePre = coherent && frame.end == (std::uint64_t) total && frame.channels == channels && frame.sampleRate == 48000.0;
            const auto post = latest (p.getPostTap(), channels, count, frame, coherent);
            bool samePost = coherent;
            for (int ch = 0; ch < channels; ++ch)
            {
                samePre = samePre && std::equal (pre[(size_t) ch].begin(), pre[(size_t) ch].end(), in[(size_t) ch].end() - count);
                samePost = samePost && std::equal (post[(size_t) ch].begin(), post[(size_t) ch].end(), out[(size_t) ch].end() - count);
            }
            LQ_CHECK (samePre, str ("PRE != input, channels=", channels, " count=", count));
            LQ_CHECK (samePost, str ("POST != output, channels=", channels, " count=", count));
        }
        LQ_CHECK (out[0] != in[0], "POST really differs (the EQ is active)");
    }
}

LQ_TEST ("spectrum tap: inactive writes nothing; the audio is bit-identical with the taps on and off; host bypass gives PRE = POST = input")
{
    LabQ8AudioProcessor on, off;
    for (auto* p : { &on, &off })
    {
        setLayout (*p, 2);
        setParam (*p, params::bandIDs[4].gain, -7.0f);
        setParam (*p, params::dynamicIDs[4].enabled, 1.0f);
        p->prepareToPlay (48000.0, 512);
    }
    on.getPreTap().setActive (true);
    on.getPostTap().setActive (true);
    juce::MidiBuffer midi;
    bool identical = true;
    for (int block = 0; block < 300; ++block)
    {
        juce::AudioBuffer<float> a (2, 512);
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto noise = labq8::test::signals::makeNoise (512, 900u + (unsigned) (block * 2 + ch), 0.5f);
            a.copyFrom (ch, 0, noise.data(), 512);
        }
        auto b = a;
        on.processBlock (a, midi);
        off.processBlock (b, midi);
        for (int ch = 0; ch < 2; ++ch)
            identical = identical && std::equal (a.getReadPointer (ch), a.getReadPointer (ch) + 512, b.getReadPointer (ch));
    }
    LQ_CHECK (identical, "taps changed the audio");
    LQ_CHECK (off.getPreTap().getWritten() == 0 && off.getPostTap().getWritten() == 0, "inactive taps wrote");
    LQ_CHECK (on.getPreTap().getWritten() == 300u * 512u && on.getPostTap().getWritten() == 300u * 512u, "active taps wrote every sample");

    // Host bypass: the signal passes unchanged and both taps hold it.
    LabQ8AudioProcessor p;
    setLayout (p, 2);
    setParam (p, params::bandIDs[0].gain, 12.0f);
    p.prepareToPlay (48000.0, 256);
    p.getPreTap().setActive (true);
    p.getPostTap().setActive (true);
    juce::AudioBuffer<float> buffer (2, 256);
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto noise = labq8::test::signals::makeNoise (256, 50u + (unsigned) ch, 0.3f);
        buffer.copyFrom (ch, 0, noise.data(), 256);
    }
    const auto original = buffer;
    p.processBlockBypassed (buffer, midi);
    SpectrumTap::Frame frame;
    bool c1 = false, c2 = false;
    const auto pre = latest (p.getPreTap(), 2, 256, frame, c1);
    const auto post = latest (p.getPostTap(), 2, 256, frame, c2);
    bool same = c1 && c2;
    for (int ch = 0; ch < 2; ++ch)
        same = same && std::equal (pre[(size_t) ch].begin(), pre[(size_t) ch].end(), original.getReadPointer (ch))
                    && std::equal (post[(size_t) ch].begin(), post[(size_t) ch].end(), original.getReadPointer (ch))
                    && std::equal (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + 256, original.getReadPointer (ch));
    LQ_CHECK (same, "bypass: PRE = POST = input = output");
}

LQ_TEST ("spectrum tap: a new stream (prepareToPlay) starts a generation; nothing older, and nothing before `from`, is given")
{
    LabQ8AudioProcessor p;
    setLayout (p, 2);
    p.prepareToPlay (44100.0, 512);
    auto& tap = p.getPreTap();
    tap.setActive (true);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> ones (2, 512);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (ones.getWritePointer (ch), 1.0f, 512);
    for (int i = 0; i < 4; ++i)
    {
        auto b = ones;
        p.processBlock (b, midi);
    }
    SpectrumTap::Frame first;
    bool coherent = false;
    latest (tap, 2, 16, first, coherent);

    p.prepareToPlay (96000.0, 512);
    juce::AudioBuffer<float> twos (2, 100);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (twos.getWritePointer (ch), 2.0f, 100);
    p.processBlock (twos, midi);
    SpectrumTap::Frame frame;
    const auto copy = latest (tap, 2, 300, frame, coherent);
    LQ_CHECK (coherent && frame.generation == first.generation + 1 && frame.sampleRate == 96000.0, "new generation and rate");
    LQ_CHECK (frame.start == 2048 && frame.end == 2148, str ("generation starts at ", frame.start, ", ends at ", frame.end));
    bool zerosThenTwos = true;
    for (int i = 0; i < 300; ++i)
        zerosThenTwos = zerosThenTwos && copy[0][(size_t) i] == (i < 200 ? 0.0f : 2.0f) && copy[1][(size_t) i] == copy[0][(size_t) i];
    LQ_CHECK (zerosThenTwos, "the previous stream's samples are not given");

    // `from`: a reader that (re)starts later sees only what came after it.
    std::vector<float> l (300), r (300);
    float* dest[] = { l.data(), r.data() };
    tap.copyLatest (dest, 2, 300, 2100, frame);
    bool fromOk = true;
    for (int i = 0; i < 300; ++i)
        fromOk = fromOk && l[(size_t) i] == (i < 252 ? 0.0f : 2.0f);
    LQ_CHECK (fromOk, "nothing before `from`");

    // Fewer samples than asked since the very start: zeros first.
    SpectrumTap fresh;
    fresh.setActive (true);
    const float a[] = { 5.0f, 6.0f, 7.0f };
    const float* in[] = { a, a };
    fresh.write (in, 2, 3);
    const auto few = latest (fresh, 2, 5, frame, coherent);
    LQ_CHECK (coherent && few[0] == std::vector<float> ({ 0.0f, 0.0f, 5.0f, 6.0f, 7.0f }), "short history padded with zeros");
}

LQ_TEST ("spectrum tap rt: lock-free; no allocation while capturing in processBlock and processBlockBypassed")
{
    LQ_CHECK (std::atomic<float>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free, "lock-free");
    for (const int channels : { 1, 2 })
    {
        LabQ8AudioProcessor p;
        setLayout (p, channels);
        p.prepareToPlay (48000.0, 512);
        p.getPreTap().setActive (true);
        p.getPostTap().setActive (true);
        juce::AudioBuffer<float> buffer (channels, 512);
        juce::MidiBuffer midi;
        long allocations = 0;
        for (int block = 0; block < 400; ++block)
        {
            const auto noise = labq8::test::signals::makeNoise (512, (std::uint32_t) block);
            for (int ch = 0; ch < channels; ++ch)
                buffer.copyFrom (ch, 0, noise.data(), 512);
            ScopedAllocationCounter counter;
            if (block % 5 == 4)
                p.processBlockBypassed (buffer, midi);
            else
                p.processBlock (buffer, midi);
            allocations += counter.count();
        }
        LQ_CHECK (allocations == 0, str ("allocated ", allocations, " times, channels=", channels));
    }
}

LQ_TEST ("spectrum tap coherence: a copy is intact exactly while the writer has not claimed the sample that overwrites its oldest one")
{
    constexpr std::uint64_t cap = SpectrumTap::capacity;
    for (const std::uint64_t begin : { (std::uint64_t) 0, (std::uint64_t) 12345, cap * 7 + 3 })
    {
        LQ_CHECK (SpectrumTap::untouched (begin + cap, begin), str ("claimed up to begin + capacity: intact, begin=", begin));
        LQ_CHECK (! SpectrumTap::untouched (begin + cap + 1, begin), str ("one sample more: overwritten, begin=", begin));
        LQ_CHECK (SpectrumTap::untouched (begin, begin), "nothing claimed beyond the copy");
    }
}

LQ_TEST ("spectrum tap concurrency: every accepted copy is exactly the latest samples, contiguous, under stress; overwritten copies are refused")
{
    // Sample k of channel 0 is k mod 2^24 (exact in float), of channel 1 its
    // negative: any accepted copy must be exactly the run ending at its end.
    // The writer goes as fast as it can with random block sizes; the reader
    // copies up to half the ring, so the writer often laps it during a copy.
    SpectrumTap tap;
    tap.setActive (true);
    tap.prepare (48000.0, 2);
    constexpr std::uint64_t total = 200000000;
    std::atomic<bool> done { false };
    std::thread writer ([&]
    {
        // Mostly host-sized blocks, sometimes larger than half the ring (as
        // an offline bounce may send), so a copy plus the block in flight can
        // exceed the ring.
        std::mt19937 rng (11);
        std::uniform_int_distribution<int> size (1, 8192), large (8192, 40000), pick (0, 9);
        std::vector<float> l (40000), r (40000);
        std::uint64_t k = 0;
        while (k < total)
        {
            const auto n = (int) std::min<std::uint64_t> ((std::uint64_t) (pick (rng) == 0 ? large (rng) : size (rng)), total - k);
            for (int i = 0; i < n; ++i)
            {
                l[(size_t) i] = (float) ((k + (std::uint64_t) i) & 0xFFFFFF);
                r[(size_t) i] = -l[(size_t) i];
            }
            const float* data[] = { l.data(), r.data() };
            tap.write (data, 2, n);
            k += (std::uint64_t) n;
        }
        done.store (true, std::memory_order_release);
    });

    std::mt19937 rng (12);
    std::uniform_int_distribution<int> countDist (1024, SpectrumTap::capacity / 2);
    std::vector<float> l ((size_t) SpectrumTap::capacity / 2), r ((size_t) SpectrumTap::capacity / 2);
    float* dest[] = { l.data(), r.data() };
    long accepted = 0, refused = 0, wrong = 0, notMonotonic = 0;
    std::uint64_t lastEnd = 0;
    while (! done.load (std::memory_order_acquire))
    {
        const auto count = countDist (rng);
        SpectrumTap::Frame frame;
        if (! tap.copyLatest (dest, 2, count, 0, frame))
        {
            ++refused;
            continue;
        }
        ++accepted;
        notMonotonic += frame.end < lastEnd ? 1 : 0;
        lastEnd = frame.end;
        for (int i = 0; i < count; ++i)
        {
            const auto index = (std::int64_t) frame.end - count + i;
            const auto expected = index < 0 ? 0.0f : (float) ((std::uint64_t) index & 0xFFFFFF);
            if (l[(size_t) i] != expected || r[(size_t) i] != -expected)
            {
                ++wrong;
                break;
            }
        }
    }
    writer.join();
    ctx.note (str (accepted, " copies accepted, ", refused, " refused (overwritten during the copy) while writing ", total, " samples"));
    LQ_CHECK (wrong == 0, str (wrong, " accepted copies were not exactly the latest samples"));
    LQ_CHECK (notMonotonic == 0, "the end of the copies went backwards");
    LQ_CHECK (accepted > 100, str ("enough accepted copies: ", accepted));
    LQ_CHECK (refused > 0, "the writer lapped the reader at least once (the refusal was exercised)");
}

//==============================================================================
// UI-2b: analysis.
namespace
{
using labq8::ui::SpectrumAnalyzer;
using labq8::ui::SpectrumBallistics;
namespace geo = labq8::ui::geometry;

// The screen's display points: one every 1.5 reference pixels (as the EQ
// curve), from the left edge of the plot.
std::vector<double> displayFrequencies()
{
    std::vector<double> f;
    for (float x = geo::plotArea.x; x <= geo::plotArea.x + geo::plotArea.w; x += 1.5f)
        f.push_back (labq8::ui::xToFrequency (x));
    return f;
}

std::vector<float> sine (int n, double fs, double frequency, double amplitude, double phase = 0.3)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = (float) (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / fs + phase));
    return v;
}

struct Analysis
{
    std::vector<float> bins, points;
};

Analysis analyse (SpectrumAnalyzer& a, const std::vector<std::vector<float>>& channels)
{
    std::vector<const float*> ptrs;
    for (const auto& c : channels)
        ptrs.push_back (c.data());
    a.analyse (ptrs.data(), (int) ptrs.size());
    Analysis out { a.getBinsDb(), {} };
    a.toDisplay (out.points);
    return out;
}

double db (double amplitude) { return 20.0 * std::log10 (amplitude); }

// Largest display level within +-`px` reference pixels of `hz`, and where.
std::pair<float, float> displayPeakNear (const std::vector<double>& freqs, const std::vector<float>& points, double hz, float px)
{
    const auto x0 = labq8::ui::frequencyToX (hz);
    float best = -1000.0f, bestX = 0.0f;
    for (size_t i = 0; i < freqs.size(); ++i)
    {
        const auto x = labq8::ui::frequencyToX (freqs[i]);
        if (std::abs (x - x0) <= px && points[i] > best)
        {
            best = points[i];
            bestX = x;
        }
    }
    return { best, bestX };
}
} // namespace

LQ_TEST ("spectrum analysis: FFT size by rate; sines read their dBFS level at their frequency's line, centred on a bin or between two")
{
    const auto freqs = displayFrequencies();
    const std::pair<double, int> sizes[] = { { 44100.0, 8192 }, { 48000.0, 8192 }, { 88200.0, 16384 }, { 96000.0, 16384 }, { 176400.0, 32768 }, { 192000.0, 32768 } };
    double worstBin = 0.0, worstPoint = 0.0, worstX = 0.0;
    for (const auto& [fs, n] : sizes)
    {
        SpectrumAnalyzer a;
        a.prepare (fs, freqs);
        LQ_CHECK (a.getSize() == n, str ("FFT size at ", fs, ": ", a.getSize()));
        const auto binHz = fs / n;
        for (const double target : { 100.0, 1000.0, 10000.0 })
            for (const double offset : { 0.0, 0.5 })
                for (const double amplitude : { 0.5, 0.1, 0.001 })
                {
                    const auto f = (std::round (target / binHz) + offset) * binHz;
                    const auto result = analyse (a, { sine (n, fs, f, amplitude) });
                    const auto k = (int) std::lround (f / binHz);
                    const auto peak = *std::max_element (result.bins.begin() + k - 3, result.bins.begin() + k + 4);
                    worstBin = std::max (worstBin, std::abs (peak - db (amplitude)) + (peak > db (amplitude) + 0.05 ? 10.0 : 0.0));
                    LQ_CHECK (peak <= db (amplitude) + 0.05 && peak >= db (amplitude) - 0.85,
                              str ("bin level ", peak, " for ", db (amplitude), " dBFS at ", f, " Hz, fs ", fs));
                    const auto [level, x] = displayPeakNear (freqs, result.points, f, 12.0f);
                    worstPoint = std::max (worstPoint, std::abs (level - db (amplitude)));
                    worstX = std::max (worstX, (double) std::abs (x - labq8::ui::frequencyToX (f)));
                    LQ_CHECK (level <= db (amplitude) + 0.05 && level >= db (amplitude) - 1.3,
                              str ("display level ", level, " for ", db (amplitude), " dBFS at ", f, " Hz, fs ", fs));
                    // Between two bins both read the same: the position is
                    // known to half a bin (3.6 px at 100 Hz, 0.4 px at 1 kHz).
                    const auto halfBinPx = labq8::ui::frequencyToX (f + binHz / 2.0) - labq8::ui::frequencyToX (f);
                    const auto tolerance = std::max (1.5f, halfBinPx + 0.75f);
                    LQ_CHECK (std::abs (x - labq8::ui::frequencyToX (f)) <= tolerance, str ("peak at x ", x, " for ", f, " Hz (tolerance ", tolerance, ")"));
                }
    }
    ctx.note (str ("worst: bins ", worstBin, " dB, display ", worstPoint, " dB, position ", worstX, " px"));

    // 1 kHz exactly on the "1k" line; nothing at or above Nyquist.
    SpectrumAnalyzer a;
    a.prepare (44100.0, freqs);
    const auto result = analyse (a, { sine (a.getSize(), 44100.0, 1000.0, 0.5) });
    const auto [level, x] = displayPeakNear (freqs, result.points, 1000.0, 12.0f);
    LQ_CHECK (std::abs (x - 896.5f) <= 1.5f && level > -7.0f, str ("1 kHz at x ", x));
    // Nothing at or above Nyquist, even with energy up to it (white noise).
    const auto noise = labq8::test::signals::makeNoise ((size_t) a.getSize(), 404, 0.5f);
    const auto full = analyse (a, { std::vector<float> (noise.begin(), noise.end()) });
    bool beyondNyquist = true;
    int checked = 0;
    for (size_t i = 0; i < freqs.size(); ++i)
        if (freqs[i] >= 0.497 * 44100.0)
        {
            beyondNyquist = beyondNyquist && full.points[i] == (float) SpectrumAnalyzer::restDb;
            ++checked;
        }
    LQ_CHECK (checked > 5 && beyondNyquist, str ("nothing at or above Nyquist (", checked, " points)"));
    LQ_CHECK (full.points[0] > -80.0f, "white noise is drawn below Nyquist");

    // Silence: nothing.
    const auto silent = analyse (a, { std::vector<float> ((size_t) a.getSize(), 0.0f) });
    LQ_CHECK (std::all_of (silent.points.begin(), silent.points.end(), [] (float v) { return v == (float) SpectrumAnalyzer::restDb; }), "silence reads nothing");
}

LQ_TEST ("spectrum analysis: dynamic range (-90 dBFS next to 0 dBFS) and frequency resolution, measured")
{
    const auto freqs = displayFrequencies();
    SpectrumAnalyzer a;
    a.prepare (48000.0, freqs);
    const auto n = a.getSize();
    auto loud = sine (n, 48000.0, 1000.0, 1.0), quiet = sine (n, 48000.0, 5000.0, std::pow (10.0, -90.0 / 20.0), 1.1);
    for (size_t i = 0; i < loud.size(); ++i)
        loud[i] += quiet[i];
    const auto result = analyse (a, { loud });
    const auto at5k = displayPeakNear (freqs, result.points, 5000.0, 6.0f).first;
    LQ_CHECK (std::abs (at5k + 90.0f) < 1.3f, str ("-90 dBFS tone beside a 0 dBFS one reads ", at5k));
    float between = -1000.0f;
    for (size_t i = 0; i < freqs.size(); ++i)
        if (freqs[i] > 2500.0 && freqs[i] < 3500.0)
            between = std::max (between, result.points[i]);
    LQ_CHECK (between < (float) SpectrumAnalyzer::floorDb, str ("leakage between them: ", between, " dB (below the floor)"));

    // Resolution: the smallest spacing at which two equal tones (50 Hz and
    // 50 + d Hz) show a dip of at least 3 dB between their peaks, per rate.
    // The window lasts about 170 ms at every rate, so the answer is the same.
    juce::String measured;
    double smallest48k = 0.0;
    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        SpectrumAnalyzer r;
        r.prepare (fs, freqs);
        double found = 0.0;
        for (double d = 2.0; d <= 60.0 && found == 0.0; d += 0.5)
        {
            auto two = sine (r.getSize(), fs, 50.0, 0.5);
            const auto other = sine (r.getSize(), fs, 50.0 + d, 0.5, 2.0);
            for (size_t i = 0; i < two.size(); ++i)
                two[i] += other[i];
            const auto res = analyse (r, { two });
            const auto x1 = labq8::ui::frequencyToX (50.0), x2 = labq8::ui::frequencyToX (50.0 + d);
            float p1 = -1000.0f, p2 = -1000.0f, dip = 1000.0f;
            for (size_t i = 0; i < freqs.size(); ++i)
            {
                const auto x = labq8::ui::frequencyToX (freqs[i]);
                if (std::abs (x - x1) <= 1.0f) p1 = std::max (p1, res.points[i]);
                if (std::abs (x - x2) <= 1.0f) p2 = std::max (p2, res.points[i]);
                if (x > x1 + 1.0f && x < x2 - 1.0f) dip = std::min (dip, res.points[i]);
            }
            if (dip < 1000.0f && std::min (p1, p2) - dip >= 3.0f)
                found = d;
        }
        measured << juce::String (fs / 1000.0, 1) << " kHz: " << found << " Hz;  ";
        if (fs == 48000.0)
            smallest48k = found;
        LQ_CHECK (found > 0.0, str ("two tones near 50 Hz never separated at ", fs));
    }
    ctx.note (("smallest spacing separated (3 dB dip) at 50 Hz: " + measured).toStdString());

    // For the record: twice the FFT at 48 kHz (16384, a 341 ms window).
    {
        SpectrumAnalyzer r;
        r.prepare (48000.0, freqs, 14);
        double found = 0.0;
        for (double d = 2.0; d <= 60.0 && found == 0.0; d += 0.5)
        {
            auto two = sine (r.getSize(), 48000.0, 50.0, 0.5);
            const auto other = sine (r.getSize(), 48000.0, 50.0 + d, 0.5, 2.0);
            for (size_t i = 0; i < two.size(); ++i)
                two[i] += other[i];
            const auto res = analyse (r, { two });
            const auto x1 = labq8::ui::frequencyToX (50.0), x2 = labq8::ui::frequencyToX (50.0 + d);
            float p1 = -1000.0f, p2 = -1000.0f, dip = 1000.0f;
            for (size_t i = 0; i < freqs.size(); ++i)
            {
                const auto x = labq8::ui::frequencyToX (freqs[i]);
                if (std::abs (x - x1) <= 1.0f) p1 = std::max (p1, res.points[i]);
                if (std::abs (x - x2) <= 1.0f) p2 = std::max (p2, res.points[i]);
                if (x > x1 + 1.0f && x < x2 - 1.0f) dip = std::min (dip, res.points[i]);
            }
            if (dip < 1000.0f && std::min (p1, p2) - dip >= 3.0f)
                found = d;
        }
        ctx.note (str ("with a 16384-point FFT at 48 kHz (not used): ", found, " Hz"));
    }
    LQ_CHECK (smallest48k > 10.0, "50 and 60 Hz are NOT separated (as measured): documented limit");
    LQ_CHECK (smallest48k <= 25.0, str ("tones 25 Hz apart near 50 Hz are separated: ", smallest48k));
}

LQ_TEST ("spectrum analysis: white noise at its theoretical level; a sweep is followed; channels combine in power (no cancellation)")
{
    const auto freqs = displayFrequencies();
    SpectrumAnalyzer a;
    a.prepare (48000.0, freqs);
    const auto n = a.getSize();

    // White noise, uniform in [-A, A]: variance A^2 / 3. Expected bin level:
    // 4 sigma^2 ENBW / N, ENBW of the window in bins.
    double sumW = 0.0, sumW2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const auto t = 2.0 * juce::MathConstants<double>::pi * i / n;
        const auto w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2.0 * t) - 0.01168 * std::cos (3.0 * t);
        sumW += w;
        sumW2 += w * w;
    }
    const auto enbw = n * sumW2 / (sumW * sumW);
    const double amplitude = 0.5;
    const auto expected = 10.0 * std::log10 (4.0 * amplitude * amplitude / 3.0 * enbw / n);
    double mean = 0.0;
    int count = 0;
    for (unsigned frame = 0; frame < 32; ++frame)
    {
        const auto noise = labq8::test::signals::makeNoise ((size_t) n, 700u + frame, (float) amplitude);
        const auto res = analyse (a, { std::vector<float> (noise.begin(), noise.end()) });
        for (int k = (int) (200.0 / (48000.0 / n)); k <= (int) (2000.0 / (48000.0 / n)); ++k)
        {
            mean += std::pow (10.0, res.bins[(size_t) k] / 10.0);
            ++count;
        }
    }
    const auto measuredDb = 10.0 * std::log10 (mean / count);
    LQ_CHECK (std::abs (measuredDb - expected) < 0.3, str ("white noise: ", measuredDb, " dB per bin, theory ", expected, " (ENBW ", enbw, " bins)"));

    // Exponential sweep 20 Hz -> 20 kHz in 4 s: the peak follows the
    // frequency at the window's centre within 1/6 octave.
    const double fs = 48000.0, seconds = 4.0;
    const auto total = (int) (fs * seconds);
    std::vector<float> sweep ((size_t) total);
    const auto k = std::log (1000.0);
    for (int i = 0; i < total; ++i)
    {
        const auto t = i / fs;
        sweep[(size_t) i] = (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 20.0 * seconds / k * (std::exp (t / seconds * k) - 1.0)));
    }
    int followed = 0, frames = 0;
    for (int end = n; end <= total; end += 2400)
    {
        const auto centreT = (end - n / 2) / fs;
        const auto f = 20.0 * std::exp (centreT / seconds * k);
        if (f < 60.0 || f > 15000.0)
            continue;
        const auto res = analyse (a, { std::vector<float> (sweep.begin() + end - n, sweep.begin() + end) });
        const auto peak = std::max_element (res.points.begin(), res.points.end()) - res.points.begin();
        ++frames;
        followed += std::abs (std::log2 (freqs[(size_t) peak] / f)) <= 1.0 / 6.0 ? 1 : 0;
    }
    LQ_CHECK (frames > 50 && followed == frames, str ("sweep followed in ", followed, " of ", frames, " frames"));

    // Channels: mean power.
    const auto s = sine (n, fs, 1000.0, 0.5);
    auto negative = s;
    for (auto& v : negative)
        v = -v;
    const std::vector<float> zero ((size_t) n, 0.0f);
    auto peakOf = [&] (const Analysis& r) { return displayPeakNear (freqs, r.points, 1000.0, 6.0f).first; };
    const auto mono = peakOf (analyse (a, { s }));
    const auto same = peakOf (analyse (a, { s, s }));
    const auto anti = peakOf (analyse (a, { s, negative }));
    const auto leftOnly = peakOf (analyse (a, { s, zero }));
    LQ_CHECK (std::abs (mono - same) < 1e-4f && std::abs (mono - anti) < 1e-4f, str ("L = R and L = -R read as mono: ", mono, " ", same, " ", anti));
    LQ_CHECK (std::abs (leftOnly - (mono - 3.0103f)) < 0.01f, str ("one channel only: 3 dB lower: ", leftOnly, " vs ", mono));
}

LQ_TEST ("spectrum ballistics: rises at once, falls 17.4 dB/s whatever the frame rate, only falls without new audio, then rests")
{
    SpectrumBallistics b;
    const std::vector<float> loud { -10.0f, -50.0f, -120.0f };
    LQ_CHECK (b.update (&loud, 3, 0.016) && b.levelDb == loud, "instant rise");
    LQ_CHECK (! b.atRest(), "not at rest");
    b.update (nullptr, 3, 1.0);
    LQ_CHECK (std::abs (b.levelDb[0] - (-10.0f - 17.371779f)) < 1e-3f, str ("falls 17.37 dB in one second: ", b.levelDb[0]));

    SpectrumBallistics fast, slow;
    fast.update (&loud, 3, 0.0);
    slow.update (&loud, 3, 0.0);
    for (int i = 0; i < 120; ++i)
        fast.update (nullptr, 3, 1.0 / 120.0);
    for (int i = 0; i < 30; ++i)
        slow.update (nullptr, 3, 1.0 / 30.0);
    LQ_CHECK (std::abs (fast.levelDb[0] - slow.levelDb[0]) < 1e-3f, "independent of the frame rate");

    // A lower new level during the fall does not pull it down.
    SpectrumBallistics h;
    h.update (&loud, 3, 0.0);
    const std::vector<float> lower { -60.0f, -60.0f, -60.0f };
    h.update (&lower, 3, 0.5);
    LQ_CHECK (std::abs (h.levelDb[0] - (-10.0f - 8.6859f)) < 1e-3f && h.levelDb[2] == -60.0f, "max of falling and new");

    // From -10 dB to below the floor (-96) in (86 / 17.37) s, then at rest;
    // below the floor nothing visible moves.
    SpectrumBallistics r;
    r.update (&loud, 3, 0.0);
    int frames = 0;
    while (! r.atRest() && frames < 1000)
    {
        r.update (nullptr, 3, 1.0 / 60.0);
        ++frames;
    }
    LQ_CHECK (std::abs (frames / 60.0 - 86.0 / 17.371779) < 2.0 / 60.0, str ("at rest after ", frames / 60.0, " s"));
    LQ_CHECK (! r.update (nullptr, 3, 1.0 / 60.0), "at rest: nothing visible moves");
}

LQ_TEST ("spectrum analysis: the result depends only on the latest samples, not on the host's block size")
{
    const auto freqs = displayFrequencies();
    std::vector<float> reference;
    for (const int blockSize : { 1, 64, 512, 4096 })
    {
        LabQ8AudioProcessor p;
        setLayout (p, 2);
        p.prepareToPlay (48000.0, blockSize);
        p.getPostTap().setActive (true);
        juce::MidiBuffer midi;
        const auto input = labq8::test::signals::makeNoise (20000, 77, 0.3f);
        for (int pos = 0; pos < 20000; pos += blockSize)
        {
            const auto m = std::min (blockSize, 20000 - pos);
            juce::AudioBuffer<float> buffer (2, m);
            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, input.data() + pos, m);
            p.processBlock (buffer, midi);
        }
        SpectrumAnalyzer a;
        a.prepare (48000.0, freqs);
        SpectrumTap::Frame frame;
        bool coherent = false;
        const auto latestSamples = latest (p.getPostTap(), 2, a.getSize(), frame, coherent);
        const auto res = analyse (a, latestSamples);
        if (reference.empty())
            reference = res.bins;
        LQ_CHECK (coherent && res.bins == reference, str ("block size ", blockSize, " changes the spectrum"));
    }
}

//==============================================================================
// UI-2b: the screen's views, the analyzer selector and the spectrum view.
namespace
{
using labq8::ui::AnalyzerSource;
using labq8::ui::LabQ8Editor;
using labq8::ui::PanelControl;

std::unique_ptr<LabQ8Editor> openEditor (LabQ8AudioProcessor& p)
{
    return std::unique_ptr<LabQ8Editor> (dynamic_cast<LabQ8Editor*> (p.createEditor()));
}

// Plays `seconds` of a sine (amplitude per channel) in blocks of 480 at the
// processor's rate, advancing the spectrum view 1/30 s per 1/30 s of audio.
void playSine (LabQ8AudioProcessor& p, LabQ8Editor& editor, int channels, double fs, double seconds, double hz, float amplitude)
{
    const auto block = (int) std::lround (fs / 30.0);
    juce::AudioBuffer<float> buffer (channels, block);
    juce::MidiBuffer midi;
    static long position = 0;
    for (int frame = 0; frame < (int) std::lround (seconds * 30.0); ++frame)
    {
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < block; ++i)
                buffer.setSample (ch, i, (float) (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) (position + i) / fs)));
        position += block;
        p.processBlock (buffer, midi);
        editor.getSpectrum().advance (1.0 / 30.0);
    }
}

float levelAt (LabQ8Editor& editor, bool pre, double hz)
{
    const auto& freqs = editor.getSpectrum().getFrequencies();
    const auto& levels = editor.getSpectrum().getLevels (pre);
    float best = -1000.0f;
    for (size_t i = 0; i < freqs.size(); ++i)
        if (std::abs (std::log2 (freqs[i] / hz)) < 0.05)
            best = std::max (best, levels[i]);
    return best;
}
} // namespace

LQ_TEST ("spectrum views: ALL + POST by default; EQ only the curve, SPECTRUM only the spectrum, ALL both; DYN unavailable; per instance, never in the state")
{
    LabQ8AudioProcessor p;
    setLayout (p, 2);
    p.prepareToPlay (48000.0, 480);
    juce::MemoryBlock stateBefore;
    p.getStateInformation (stateBefore);

    auto editor = openEditor (p);
    auto& spectrum = editor->getSpectrum();
    LQ_CHECK (editor->getView() == LabQ8Editor::viewAll && spectrum.getSource() == AnalyzerSource::post, "defaults: ALL, POST");
    LQ_CHECK (editor->getScreen().isVisible() && spectrum.isTraceShown (false) && ! spectrum.isTraceShown (true), "ALL: curve and POST");
    LQ_CHECK (p.getPostTap().isActive() && ! p.getPreTap().isActive(), "only the POST tap copies audio");

    editor->setView (LabQ8Editor::viewEq);
    LQ_CHECK (editor->getScreen().isVisible() && ! spectrum.isTraceShown (false) && ! p.getPostTap().isActive() && ! p.getPreTap().isActive(),
              "EQ: the curve only; no tap copies audio");
    editor->showQHandles (2);
    editor->setView (LabQ8Editor::viewSpectrum);
    LQ_CHECK (! editor->getScreen().isVisible() && ! editor->areQHandlesShown() && spectrum.isTraceShown (false), "SPECTRUM: no curve, nodes or handles");
    editor->setView (LabQ8Editor::viewDyn);
    LQ_CHECK (editor->getView() == LabQ8Editor::viewSpectrum, "DYN is not available");
    auto* dynTip = dynamic_cast<juce::TooltipClient*> (&editor->getViewTab (LabQ8Editor::viewDyn));
    LQ_CHECK (dynTip != nullptr && dynTip->getTooltip().contains ("not available"), "DYN says so");

    // The tabs select views with the mouse (DYN does nothing).
    for (const int tab : { LabQ8Editor::viewEq, LabQ8Editor::viewDyn, LabQ8Editor::viewAll })
    {
        auto& c = editor->getViewTab (tab);
        const auto centre = juce::Point<float> ((float) c.getWidth() / 2.0f, (float) c.getHeight() / 2.0f);
        juce::MouseEvent down (juce::Desktop::getInstance().getMainMouseSource(), centre, juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                               &c, &c, juce::Time::getCurrentTime(), centre, juce::Time::getCurrentTime(), 1, false);
        c.mouseDown (down);
        LQ_CHECK (editor->getView() == (tab == LabQ8Editor::viewDyn ? LabQ8Editor::viewEq : tab), str ("click on tab ", tab));
    }

    // Analyzer menu: three sources, the current one ticked; each switches the
    // taps that copy audio.
    const auto menu = editor->createAnalyzerMenu();
    juce::PopupMenu::MenuItemIterator items (menu);
    juce::StringArray names;
    int ticked = 0;
    while (items.next())
        if (items.getItem().itemID > 0)
        {
            names.add (items.getItem().text);
            ticked = items.getItem().isTicked ? items.getItem().itemID : ticked;
        }
    LQ_CHECK (names.size() == 3 && names[0].startsWith ("Pre") && names[1].startsWith ("Post") && names[2] == "Pre + Post" && ticked == 2,
              names.joinIntoString ("|").toStdString());
    editor->handleAnalyzerMenuResult (1);
    LQ_CHECK (p.getPreTap().isActive() && ! p.getPostTap().isActive(), "PRE");
    editor->handleAnalyzerMenuResult (3);
    LQ_CHECK (p.getPreTap().isActive() && p.getPostTap().isActive() && spectrum.isTraceShown (true) && spectrum.isTraceShown (false), "PRE + POST");

    // Per instance, in memory: reopening keeps them; a new instance starts at
    // the defaults; the plugin state never changes.
    editor->setView (LabQ8Editor::viewSpectrum);
    editor.reset();
    LQ_CHECK (! p.getPreTap().isActive() && ! p.getPostTap().isActive(), "closing the editor stops the copies");
    editor = openEditor (p);
    LQ_CHECK (editor->getView() == LabQ8Editor::viewSpectrum && editor->getSpectrum().getSource() == AnalyzerSource::both, "reopened as left");
    LabQ8AudioProcessor other;
    auto otherEditor = openEditor (other);
    LQ_CHECK (otherEditor->getView() == LabQ8Editor::viewAll && otherEditor->getSpectrum().getSource() == AnalyzerSource::post, "another instance: defaults");
    juce::MemoryBlock stateAfter;
    p.getStateInformation (stateAfter);
    LQ_CHECK (stateAfter == stateBefore, "views and analyzer source are not part of the plugin state");
}

LQ_TEST ("spectrum view: a real sine at its level; PRE vs POST through a +12 dB Bell; silence and a stopped transport fade to rest (never frozen)")
{
    LabQ8AudioProcessor p;
    setLayout (p, 2);
    setParam (p, params::bandIDs[3].frequency, 1000.0f);
    setParam (p, params::bandIDs[3].gain, 12.0f);
    setParam (p, params::bandIDs[3].q, 1.0f);
    p.prepareToPlay (48000.0, 1600);
    auto editor = openEditor (p);
    auto& spectrum = editor->getSpectrum();
    editor->setAnalyzerSource (AnalyzerSource::both);

    // Silence from the start: nothing drawn, nothing repainted.
    const auto before = spectrum.getRepaintRequests();
    playSine (p, *editor, 2, 48000.0, 1.0, 1000.0, 0.0f);
    LQ_CHECK (spectrum.isAtRest() && spectrum.getRepaintRequests() == before, str ("silence repainted ", spectrum.getRepaintRequests() - before, " times"));

    // -20 dBFS at 1 kHz: PRE reads -20, POST -8 (the Bell's +12 dB).
    playSine (p, *editor, 2, 48000.0, 1.0, 1000.0, 0.1f);
    const auto pre = levelAt (*editor, true, 1000.0), post = levelAt (*editor, false, 1000.0);
    LQ_CHECK (pre <= -19.9f && pre >= -21.0f, str ("PRE at 1 kHz: ", pre));
    LQ_CHECK (std::abs ((post - pre) - 12.0f) < 0.3f, str ("POST - PRE at 1 kHz: ", post - pre, " dB"));
    LQ_CHECK (levelAt (*editor, false, 3000.0) < -60.0f, "nothing where there is no signal");

    // Transport stops: no new audio. The spectrum falls (17.4 dB/s) to rest
    // instead of freezing, then stops repainting.
    int frames = 0;
    while (! spectrum.isAtRest() && frames < 30 * 20)
    {
        spectrum.advance (1.0 / 30.0);
        ++frames;
    }
    const auto expected = (post - (-96.0)) / 17.371779;
    LQ_CHECK (std::abs (frames / 30.0 - expected) < 0.1, str ("at rest after ", frames / 30.0, " s, expected ", expected));
    const auto atRest = spectrum.getRepaintRequests();
    for (int i = 0; i < 90; ++i)
        spectrum.advance (1.0 / 30.0);
    LQ_CHECK (spectrum.getRepaintRequests() == atRest, "no repaint at rest");

    // The same after silence instead of a stop.
    playSine (p, *editor, 2, 48000.0, 0.5, 1000.0, 0.1f);
    playSine (p, *editor, 2, 48000.0, 7.0, 1000.0, 0.0f);
    LQ_CHECK (spectrum.isAtRest(), "silence after a signal: at rest");
}

LQ_TEST ("spectrum view: a new sample rate re-sizes the FFT and starts afresh; mono; switching a trace on shows nothing from before")
{
    LabQ8AudioProcessor p;
    setLayout (p, 2);
    p.prepareToPlay (48000.0, 480);
    auto editor = openEditor (p);
    auto& spectrum = editor->getSpectrum();
    playSine (p, *editor, 2, 48000.0, 0.5, 2000.0, 0.5f);
    LQ_CHECK (spectrum.getFftSize() == 8192 && std::abs (levelAt (*editor, false, 2000.0) + 6.02f) < 0.9f, "48 kHz");

    p.prepareToPlay (96000.0, 480);
    playSine (p, *editor, 2, 96000.0, 0.5, 500.0, 0.25f);
    LQ_CHECK (spectrum.getFftSize() == 16384, str ("FFT at 96 kHz: ", spectrum.getFftSize()));
    LQ_CHECK (std::abs (levelAt (*editor, false, 500.0) + 12.04f) < 0.9f && levelAt (*editor, false, 2000.0) < -60.0f,
              str ("96 kHz: 500 Hz at ", levelAt (*editor, false, 500.0), ", the old 2 kHz gone: ", levelAt (*editor, false, 2000.0)));

    // PRE shown with a loud signal, then switched off (its ring keeps that
    // audio while the transport is silent), then on again: none of the old
    // audio is shown.
    editor->setAnalyzerSource (AnalyzerSource::pre);
    playSine (p, *editor, 2, 96000.0, 0.5, 3000.0, 0.5f);
    LQ_CHECK (levelAt (*editor, true, 3000.0) > -10.0f, "PRE shows the loud signal");
    editor->setAnalyzerSource (AnalyzerSource::post);
    playSine (p, *editor, 2, 96000.0, 0.2, 3000.0, 0.0f);
    editor->setAnalyzerSource (AnalyzerSource::pre);
    playSine (p, *editor, 2, 96000.0, 2.0 / 30.0, 3000.0, 0.0f); // new silence, shorter than the FFT window
    LQ_CHECK (spectrum.isAtRest(), str ("old audio shown after switching PRE on again: ", levelAt (*editor, true, 3000.0)));

    LabQ8AudioProcessor mono;
    setLayout (mono, 1);
    mono.prepareToPlay (44100.0, 441);
    auto monoEditor = openEditor (mono);
    playSine (mono, *monoEditor, 1, 44100.0, 0.5, 440.0, 0.5f);
    LQ_CHECK (std::abs (levelAt (*monoEditor, false, 440.0) + 6.02f) < 0.9f, str ("mono: ", levelAt (*monoEditor, false, 440.0)));
}

LQ_TEST ("spectrum view: the EQ curve is cached; spectrum frames never recompute it; a change of the curve does; header texts fit")
{
    LabQ8AudioProcessor p;
    setLayout (p, 2);
    p.prepareToPlay (48000.0, 480);
    auto editor = openEditor (p);
    editor->setVisible (true);
    editor->setZoom (1.0f);
    LQ_CHECK (editor->getScreen().getCachedComponentImage() != nullptr, "curve buffered to an image");
    auto snapshot = [&] { return editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f); };
    snapshot();
    const auto paints = editor->getScreenPaintCount();
    for (int i = 0; i < 10; ++i)
    {
        playSine (p, *editor, 2, 48000.0, 1.0 / 30.0, 300.0 + 100.0 * i, 0.3f);
        snapshot();
    }
    LQ_CHECK (editor->getScreenPaintCount() == paints, str ("spectrum frames recomputed the curve ", editor->getScreenPaintCount() - paints, " times"));
    editor->selectBand (5); // the curve changes (the selected band's fill)
    snapshot();
    LQ_CHECK (editor->getScreenPaintCount() > paints, "a change of the curve redraws it");

    // The analyzer's longest value fits its box, before the arrow.
    juce::Font font (juce::FontOptions ("Helvetica Neue", 16.0f, 0).withStyle ("Medium"));
    for (const char* text : { "Analyzer: Pre", "Analyzer: Post", "Pre + Post" })
        LQ_CHECK (geo::analyzerTextLeft + juce::GlyphArrangement::getStringWidth (font, text) <= geo::analyzerTextRight, str (text, " fits"));
    LQ_CHECK (labq8::ui::spectrumDbToY (0.0) == geo::plotGridRows[0] && labq8::ui::spectrumDbToY (-96.0) == geo::plotGridRows[6]
                  && labq8::ui::spectrumDbToY (-200.0) == geo::plotArea.y + geo::plotArea.h && labq8::ui::spectrumDbToY (12.0) == geo::plotArea.y,
              "spectrum scale: 0 dBFS on the top grid line, -96 on the bottom one, clamped to the plot");
}

LQ_TEST ("spectrum view: the cached curve follows Gain, Frequency and Q from the panel, the host and host automation, while the spectrum runs")
{
    LabQ8AudioProcessor p;
    setLayout (p, 2);
    p.prepareToPlay (48000.0, 480);
    auto editor = openEditor (p);
    editor->setVisible (true);
    editor->setZoom (1.0f);
    editor->selectBand (2);
    const auto plot = juce::Rectangle<float> (geo::plotArea.x, geo::plotArea.y, geo::plotArea.w, geo::plotArea.h).toNearestInt();

    // The plot of an editor in the EQ view (the curve only).
    auto curveOf = [&] (LabQ8Editor& e)
    {
        const auto view = e.getView();
        e.setView (LabQ8Editor::viewEq);
        auto image = e.createComponentSnapshot (plot, true, 1.0f);
        e.setView (view);
        return image;
    };
    auto differing = [] (const juce::Image& a, const juce::Image& b)
    {
        int n = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                n += a.getPixelAt (x, y) != b.getPixelAt (x, y) ? 1 : 0;
        return n;
    };
    // A freshly built editor draws the curve from the parameters as they are.
    auto fresh = [&]
    {
        auto e = openEditor (p);
        e->setVisible (true);
        e->setZoom (1.0f);
        e->selectBand (2);
        e->setView (LabQ8Editor::viewEq);
        auto image = e->createComponentSnapshot (plot, true, 1.0f);
        e.reset();
        p.getPostTap().setActive (true); // the other editor's view (ALL) keeps copying
        return image;
    };

    struct Change { const char* how; const char* id; float value; int path; };
    const Change changes[] = {
        { "panel GAIN knob", params::bandIDs[2].gain, 7.5f, 0 },
        { "host GAIN", params::bandIDs[2].gain, -9.0f, 1 },
        { "host automation GAIN", params::bandIDs[2].gain, 4.0f, 2 },
        { "host automation FREQUENCY", params::bandIDs[2].frequency, 2500.0f, 2 },
        { "host FREQUENCY", params::bandIDs[2].frequency, 180.0f, 1 },
        { "host automation Q", params::bandIDs[2].q, 6.0f, 2 },
        { "panel Q knob", params::bandIDs[2].q, 0.4f, 0 },
        { "host automation GAIN of another band", params::bandIDs[6].gain, -11.0f, 2 },
    };
    for (const auto& c : changes)
    {
        playSine (p, *editor, 2, 48000.0, 0.2, 700.0, 0.3f); // the spectrum keeps running
        curveOf (*editor);
        const auto paints = editor->getScreenPaintCount();
        auto* parameter = p.getValueTreeState().getParameter (c.id);
        const auto normalised = parameter->convertTo0to1 (c.value);
        if (c.path == 0)
            editor->getKnob (std::string (c.id).find ("_q") != std::string::npos ? PanelControl::q : PanelControl::gain).setValue (c.value, juce::sendNotificationSync);
        else if (c.path == 1)
            parameter->setValueNotifyingHost (normalised);
        else
        {
            // As the AU / VST3 wrappers apply host automation.
            parameter->setValue (normalised);
            parameter->sendValueChangedMessageToListeners (normalised);
        }
        if (c.path != 0)
            LQ_CHECK (differing (curveOf (*editor), fresh()) > 0, str (c.how, ": before the poll the cached curve is still the old one (the cache is real)"));
        editor->refreshFromParameters();
        playSine (p, *editor, 2, 48000.0, 0.1, 700.0, 0.3f);
        const auto shown = curveOf (*editor);
        LQ_CHECK (editor->getScreenPaintCount() > paints, str (c.how, ": the curve was redrawn"));
        LQ_CHECK (differing (shown, fresh()) == 0, str (c.how, ": the cached curve equals a freshly drawn one"));
    }
}

LQ_TEST ("spectrum scales: EQ +-24 dB, SPECTRUM dBFS on the left, ALL dBFS on the right; on the grid, no overlaps, the EQ labels hidden only in SPECTRUM")
{
    // The spectrum's scale lies on the grid: 16 dB per line, the same in
    // SPECTRUM and ALL (one mapping).
    bool onGrid = true;
    for (int line = 0; line < 7; ++line)
        onGrid = onGrid && std::abs (labq8::ui::spectrumDbToY (-16.0 * line) - geo::plotGridRows[(size_t) line]) < 0.01f;
    LQ_CHECK (onGrid, "every 16 dB exactly on a grid line");
    bool monotonic = true;
    for (double db = 6.0; db > -110.0; db -= 0.5)
        monotonic = monotonic && labq8::ui::spectrumDbToY (db - 0.5) >= labq8::ui::spectrumDbToY (db);
    LQ_CHECK (monotonic, "lower levels never higher on the screen");

    LabQ8AudioProcessor p;
    setLayout (p, 2);
    p.prepareToPlay (48000.0, 480);
    auto editor = openEditor (p);
    editor->setVisible (true);
    editor->setZoom (1.0f);
    auto& left = editor->getSpectrumScale (true);
    auto& right = editor->getSpectrumScale (false);
    const std::tuple<int, bool, bool> views[] = { { LabQ8Editor::viewEq, false, false }, { LabQ8Editor::viewSpectrum, true, false }, { LabQ8Editor::viewAll, false, true } };
    for (const auto& [view, leftShown, rightShown] : views)
    {
        editor->setView (view);
        LQ_CHECK (left.isVisible() == leftShown && right.isVisible() == rightShown, str ("scales in view ", view));
    }

    // Labels: on the EQ labels' centres, right-aligned, inside their areas,
    // never overlapping each other, the plot (left), the header or the
    // settings icon.
    for (const bool isLeft : { true, false })
    {
        const auto boxes = editor->getSpectrumScaleLabels (isLeft);
        const auto area = (isLeft ? left : right).getBounds().toFloat();
        const auto edge = isLeft ? geo::axisLabelRight : geo::rightScaleRight;
        bool placed = boxes.size() == 8, apart = true, inside = true;
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            const auto centre = i < 7 ? geo::axisLabelCentres[i] : geo::axisUnitCentreY;
            placed = placed && std::abs (boxes[i].getRight() - edge) <= 1.0f && std::abs (boxes[i].getCentreY() - centre) <= 1.0f;
            inside = inside && area.contains (boxes[i]);
            for (size_t j = i + 1; j < boxes.size(); ++j)
                apart = apart && ! boxes[i].expanded (1.0f).intersects (boxes[j]);
        }
        LQ_CHECK (placed, str (isLeft ? "left" : "right", " labels right-aligned on the EQ labels' centres"));
        LQ_CHECK (apart && inside, str (isLeft ? "left" : "right", " labels apart and inside their area"));
        if (isLeft)
            LQ_CHECK (boxes.back().getY() > geo::viewTabBottom && std::all_of (boxes.begin(), boxes.end(), [] (const juce::Rectangle<float>& b) { return b.getRight() < geo::plotArea.x; }),
                      "left scale below the header, left of the plot");
        else
            LQ_CHECK (boxes.back().getY() > geo::settingsIconCentre.y + geo::settingsHitSize / 2.0f
                          && std::all_of (boxes.begin(), boxes.begin() + 7, [] (const juce::Rectangle<float>& b) { return b.getX() > 1270.0f && b.getRight() <= geo::plotArea.x + geo::plotArea.w; }),
                      "right scale inside the plot's right edge, below the settings icon");
    }

    // What is drawn: in SPECTRUM the axis column holds exactly the seven dBFS
    // labels (no EQ label left), in EQ and ALL exactly the background's.
    auto inkRows = [&] (int view)
    {
        editor->setView (view);
        // From row 245: the "dBFS" unit (rows 234-242) is above the scale.
        const auto image = editor->createComponentSnapshot ({ 356, 245, 49, 240 }, true, 1.0f);
        std::vector<int> centres;
        int start = -1;
        for (int y = 0; y <= image.getHeight(); ++y)
        {
            bool ink = false;
            for (int x = 2; y < image.getHeight() && x < image.getWidth() - 2; ++x)
                ink = ink || image.getPixelAt (x, y).getBrightness() > 0.3f;
            if (ink && start < 0)
                start = y;
            if (! ink && start >= 0)
            {
                centres.push_back (245 + (start + y - 1) / 2);
                start = -1;
            }
        }
        return std::pair { centres, image };
    };
    const auto [spectrumRows, spectrumImage] = inkRows (LabQ8Editor::viewSpectrum);
    const auto [eqRows, eqImage] = inkRows (LabQ8Editor::viewEq);
    const auto [allRows, allImage] = inkRows (LabQ8Editor::viewAll);
    bool spectrumLabels = spectrumRows.size() == 7, eqLabels = eqRows.size() == 7;
    for (size_t i = 0; i < 7 && spectrumLabels && eqLabels; ++i)
    {
        spectrumLabels = std::abs ((float) spectrumRows[i] - geo::axisLabelCentres[i]) <= 1.5f;
        eqLabels = std::abs ((float) eqRows[i] - geo::axisLabelCentres[i]) <= 1.5f;
    }
    LQ_CHECK (spectrumLabels, str ("SPECTRUM: seven dBFS labels on the grid (", spectrumRows.size(), " found)"));
    LQ_CHECK (eqLabels, str ("EQ: the seven +-24 dB labels (", eqRows.size(), " found)"));
    // Rows 245-246 still carry the selected tab's halo (EQ vs ALL): compare
    // from row 248.
    bool allSame = true;
    for (int y = 3; y < eqImage.getHeight(); ++y)
        for (int x = 0; x < eqImage.getWidth(); ++x)
            allSame = allSame && eqImage.getPixelAt (x, y) == allImage.getPixelAt (x, y);
    LQ_CHECK (allSame, "ALL keeps the EQ's +-24 dB labels on the left, as in EQ");
}
