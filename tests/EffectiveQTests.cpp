// E1b.1 effective-Q tests: per-type Q limits (shelves 1, cuts 2), response
// and headroom at the limits, smoothing of effective-Q changes, type changes,
// stored vs effective Q in the processor and genuine E1b states.

#pragma clang diagnostic ignored "-Wfloat-equal" // bit-exactness is a tested property

#include "FilterReference.h"
#include "TestHarness.h"
#include "TestSignals.h"

#include "dsp/BandLimits.h"
#include "dsp/EqBand.h"
#include "dsp/EqCore.h"
#include "parameters/ParameterIDs.h"
#include "plugin/PluginProcessor.h"

#include <random>

using namespace labq8::dsp;
using namespace labq8::test::signals;
using namespace labq8::test::reference;
using labq8::LabQ8AudioProcessor;
using labq8::test::str;
namespace params = labq8::params;

namespace
{
constexpr FilterType limitedTypes[] = { FilterType::lowShelf, FilterType::highShelf, FilterType::lowCut, FilterType::highCut };

// Effective Q of a stored Q of 18 (the nominal limit plus the 1 ppm tolerance).
double effectiveMax (FilterType t) { return effectiveQ (t, limits::maxQ); }

double db (double linear) { return 20.0 * std::log10 (linear); }

void run (EqBand& band, Signal& x, int blockSize = 512)
{
    for (size_t pos = 0; pos < x.size(); pos += (size_t) blockSize)
    {
        float* ch[] = { x.data() + pos };
        band.process (ch, 1, (int) std::min<size_t> ((size_t) blockSize, x.size() - pos));
    }
}

EqBand makeBand (double fs, const BandSettings& s)
{
    EqBand band;
    band.setTargets (s);
    band.prepare (fs, 1);
    return band;
}

bool sameCoefficients (const BiquadCoefficients& a, const BiquadCoefficients& b)
{
    return a.b0 == b.b0 && a.b1 == b.b1 && a.b2 == b.b2 && a.a1 == b.a1 && a.a2 == b.a2;
}

// The band (smoothing in log2 units, so Q and f round-trip through exp2/log2)
// designs with Q e: current Q within 1e-12 and coefficients bit-identical to
// the designer at the band's current values.
bool designsWithQ (const EqBand& band, FilterType type, double gain, double e)
{
    return std::abs (band.getCurrentQ() - e) <= 1.0e-12 * e
        && sameCoefficients (band.getCoefficients(),
                             designFilter (type, band.getSampleRate(), band.getCurrentFrequencyHz(), gain, band.getCurrentQ()));
}

// Extremes of |H| (dB) on a dense log grid, denser around f0.
std::pair<double, double> extremesDb (const BiquadCoefficients& c, double f0, double fs)
{
    double lo = 1.0e9, hi = -1.0e9;
    auto visit = [&] (double f)
    {
        const auto m = db (magnitudeAt (c, f, fs));
        lo = std::min (lo, m);
        hi = std::max (hi, m);
    };
    for (int i = 0; i < 4000; ++i)
        visit (1.0 * std::pow (0.4999 * fs / 1.0, i / 3999.0));
    for (int i = 0; i < 4000; ++i)
        visit (std::min (0.4999 * fs, f0 * 0.2 * std::pow (25.0, i / 3999.0)));
    return { lo, hi };
}

// Impulse response of the band (float I/O), until it has decayed.
Signal impulseResponse (EqBand& band)
{
    const auto length = (size_t) std::min (decaySamples (band.getCoefficients(), 1.0e-9), 4.0e6);
    Signal h (length, 0.0f);
    h[0] = 1.0f;
    run (band, h, 4096);
    return h;
}

void setParam (LabQ8AudioProcessor& p, const char* id, float value)
{
    auto* parameter = p.getValueTreeState().getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

float getParam (LabQ8AudioProcessor& p, const char* id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}

// What the DSP receives for a band (mirrors the adapter; stored Q, not effective).
BandSettings expectedSettings (LabQ8AudioProcessor& p, int band)
{
    const auto& ids = params::bandIDs[(size_t) band];
    double gain = getParam (p, ids.gain);
    if (std::abs (gain) < 0.005)
        gain = 0.0;
    return { getParam (p, ids.frequency), gain, getParam (p, ids.q), getParam (p, ids.bypass) >= 0.5f,
             filterTypeFromIndex ((int) std::lround (getParam (p, ids.type))) };
}

juce::MemoryBlock loadFixture (const char* name)
{
    juce::MemoryBlock block;
    juce::File (LABQ8_TEST_FIXTURES_DIR).getChildFile (name).loadFileAsData (block);
    return block;
}

void setStereo (LabQ8AudioProcessor& p)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    p.setBusesLayout (layout);
}

// Processor output equals an EqCore fed with the given settings, block by block.
bool processorMatchesCore (LabQ8AudioProcessor& p, EqCore& core, int blocks, unsigned seed)
{
    juce::MidiBuffer midi;
    bool identical = true;
    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> buffer (2, 480);
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto n = makeNoise (480, seed + (unsigned) (block * 2 + ch));
            buffer.copyFrom (ch, 0, n.data(), 480);
        }
        auto expected = buffer;
        p.processBlock (buffer, midi);
        core.process (expected.getArrayOfWritePointers(), 2, 480);
        for (int ch = 0; ch < 2; ++ch)
            identical = identical && std::equal (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + 480, expected.getReadPointer (ch));
    }
    return identical;
}
} // namespace

//==============================================================================
LQ_TEST ("q limit: effective Q per type; inside the limit the stored Q is used bit for bit")
{
    LQ_CHECK (maxEffectiveQ (FilterType::bell) == limits::maxQ && maxEffectiveQ (FilterType::notch) == limits::maxQ,
              "Bell and Notch keep the full Q range");
    LQ_CHECK (maxEffectiveQ (FilterType::lowShelf) == 1.0 && maxEffectiveQ (FilterType::highShelf) == 1.0, "shelf limit 1");
    LQ_CHECK (maxEffectiveQ (FilterType::lowCut) == 2.0 && maxEffectiveQ (FilterType::highCut) == 2.0, "cut limit 2");

    // The mapping: identity up to the limit (+1 ppm), constant above, monotonic
    // and continuous (never moves more than the stored Q does).
    for (const auto type : allTypes)
    {
        const auto bound = maxEffectiveQ (type) * (1.0 + effectiveQTolerance);
        bool identity = true, clamped = true, monotonic = true, continuous = true;
        double last = 0.0, lastQ = 0.0;
        for (int i = 0; i <= 2000; ++i)
        {
            const auto q = limits::minQ * std::pow (limits::maxQ / limits::minQ, i / 2000.0);
            const auto e = effectiveQ (type, q);
            identity = identity && (q > bound || e == q);
            clamped = clamped && (q <= bound || e == bound);
            if (i > 0)
            {
                monotonic = monotonic && e >= last;
                continuous = continuous && e - last <= q - lastQ;
            }
            last = e;
            lastQ = q;
        }
        LQ_CHECK (identity && clamped && monotonic && continuous, str (name (type), " effective Q mapping"));
    }

    // Values the host actually stores for "1.00" and "2.00" are inside the limit.
    LQ_CHECK (effectiveQ (FilterType::lowCut, (double) 2.000000238418579f) == (double) 2.000000238418579f,
              "a stored 2.00 (float) must be used unchanged by the cuts");
    LQ_CHECK (effectiveQ (FilterType::lowShelf, (double) std::nextafter (1.0f, 2.0f)) == (double) std::nextafter (1.0f, 2.0f),
              "the float just above 1.00 must be used unchanged by the shelves");

    // EqBand designs with the effective Q.
    for (const auto fs : sampleRates)
        for (const auto type : allTypes)
            for (const auto q : { 0.1, 0.7071, maxEffectiveQ (type), 1.5, 2.5, 18.0 })
            {
                auto band = makeBand (fs, { 700.0, 6.0, q, false, type });
                const auto e = effectiveQ (type, q);
                LQ_CHECK (designsWithQ (band, type, 6.0, e), str (name (type), " stored Q ", q, " must design with ", e, ", fs=", fs));
            }
}

LQ_TEST ("q limit: shelf overshoot and cut resonance with stored Q 18, every rate and extreme frequency (1, 2)")
{
    double worstShelf24 = 0.0, worstShelf6 = 0.0, worstCut = 0.0, minCut = 1.0e9;
    double e1bShelf24 = 0.0, e1bCut = 0.0;

    for (const auto fs : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0, 768000.0 })
        for (const auto f0 : { limits::minFrequencyHz, 1000.0, limits::maxFrequencyHz })
            for (const auto type : limitedTypes)
            {
                const auto f = clampFrequency (f0, fs);
                if (usesGain (type))
                {
                    for (const auto g : { -24.0, -6.0, 6.0, 24.0 })
                    {
                        auto band = makeBand (fs, { f0, g, limits::maxQ, false, type });
                        const auto [lo, hi] = extremesDb (band.getCoefficients(), f, fs);
                        const auto overshoot = std::max (hi - std::max (0.0, g), std::min (0.0, g) - lo);
                        auto& worst = std::abs (g) == 24.0 ? worstShelf24 : worstShelf6;
                        worst = std::max (worst, overshoot);
                        LQ_CHECK (isStable (band.getCoefficients()) && poleRadius (band.getCoefficients()) < 1.0, "shelf stability");

                        if (std::abs (g) == 24.0)
                        {
                            const auto [elo, ehi] = extremesDb (designFilter (type, fs, f0, g, limits::maxQ), f, fs);
                            e1bShelf24 = std::max (e1bShelf24, std::max (ehi - std::max (0.0, g), std::min (0.0, g) - elo));
                        }
                    }
                }
                else
                {
                    auto band = makeBand (fs, { f0, 0.0, limits::maxQ, false, type });
                    const auto peak = extremesDb (band.getCoefficients(), f, fs).second;
                    worstCut = std::max (worstCut, peak);
                    minCut = std::min (minCut, peak);
                    LQ_CHECK (isStable (band.getCoefficients()) && poleRadius (band.getCoefficients()) < 1.0, "cut stability");
                    e1bCut = std::max (e1bCut, extremesDb (designFilter (type, fs, f0, 0.0, limits::maxQ), f, fs).second);
                }
            }

    // Analog bounds (labq8_sweep): shelf Q 1: 1.118 dB at +/-24 dB, 0.443 dB at
    // +/-6 dB; cut Q 2: Q^2 / sqrt(Q^2 - 1/4) = 6.301 dB.
    const auto cutPeak = db (4.0 / std::sqrt (3.75));
    LQ_CHECK (worstShelf24 <= 1.119, str ("shelf overshoot at +/-24 dB ", worstShelf24, " dB"));
    LQ_CHECK (worstShelf6 <= 0.444, str ("shelf overshoot at +/-6 dB ", worstShelf6, " dB"));
    LQ_CHECK (worstCut <= cutPeak + 1.0e-4 && minCut > cutPeak - 0.05, str ("cut peak ", minCut, " .. ", worstCut, " dB, expected ", cutPeak));

    ctx.note (str ("shelf overshoot with stored Q 18: ", worstShelf24, " dB at +/-24 dB (E1b: ", e1bShelf24, " dB), ",
                   worstShelf6, " dB at +/-6 dB"));
    ctx.note (str ("cut resonance with stored Q 18: ", worstCut, " dB (E1b: ", e1bCut, " dB)"));
}

LQ_TEST ("q limit: worst-case headroom at extreme settings is that of the limit, not of Q 18 (3)")
{
    struct Bound
    {
        FilterType type;
        double gain, maxL1Db; // labq8_sweep section 4, plus 0.05 dB
    };
    const Bound bounds[] = {
        { FilterType::lowShelf, 24.0, 31.62 },  { FilterType::lowShelf, -24.0, 7.96 },
        { FilterType::highShelf, 24.0, 31.96 }, { FilterType::highShelf, -24.0, 7.62 },
        { FilterType::lowCut, 0.0, 11.70 },     { FilterType::highCut, 0.0, 11.62 },
    };

    for (const auto& b : bounds)
    {
        double worst = 0.0, worstE1b = 0.0;
        for (const auto fs : { 44100.0, 192000.0 })
            for (const auto f0 : { limits::minFrequencyHz, 1000.0, 0.49 * fs })
            {
                auto band = makeBand (fs, { f0, b.gain, limits::maxQ, false, b.type });
                const auto h = impulseResponse (band);
                double l1 = 0.0;
                for (const auto v : h)
                    l1 += std::abs ((double) v);
                LQ_CHECK (allFinite (h) && l1 > 0.0, "impulse response must be finite");

                // The full-scale input that reaches the L1 bound: the sign of the
                // time-reversed impulse response. The output peak equals L1.
                auto band2 = makeBand (fs, { f0, b.gain, limits::maxQ, false, b.type });
                Signal x (h.size());
                for (size_t i = 0; i < h.size(); ++i)
                    x[i] = h[h.size() - 1 - i] >= 0.0f ? 1.0f : -1.0f;
                run (band2, x, 4096);
                LQ_CHECK (allFinite (x) && std::abs (x.back() - l1) < 1.0e-3 * l1,
                          str (name (b.type), " worst-case output ", x.back(), " vs L1 ", l1));

                worst = std::max (worst, db (l1));

                {
                    // Same measurement for the E1b design at Q 18 (double, no limit).
                    const auto c = designFilter (b.type, fs, f0, b.gain, limits::maxQ);
                    double s1 = 0, s2 = 0, e1b = 0;
                    const auto n = (long) std::min (decaySamples (c, 1.0e-9), 4.0e6);
                    for (long i = 0; i < n; ++i)
                    {
                        const double in = i == 0 ? 1.0 : 0.0;
                        const auto y = c.b0 * in + s1;
                        s1 = c.b1 * in - c.a1 * y + s2;
                        s2 = c.b2 * in - c.a2 * y;
                        e1b += std::abs (y);
                    }
                    worstE1b = std::max (worstE1b, db (e1b));
                }
            }

        LQ_CHECK (worst <= b.maxL1Db, str (name (b.type), " ", b.gain, " dB: worst-case gain ", worst, " dB > ", b.maxL1Db));
        ctx.note (str (name (b.type), " ", b.gain, " dB, stored Q 18: worst-case gain ", worst, " dB (E1b: ", worstE1b, " dB)"));
    }
}

LQ_TEST ("q limit: stored Q above the limit changes nothing; crossing it ramps like any Q change (4)")
{
    for (const auto fs : { 44100.0, 96000.0 })
        for (const auto type : limitedTypes)
        {
            const auto gain = usesGain (type) ? 12.0 : 0.0;
            const auto top = effectiveMax (type);
            const auto input = makeNoise (samples (0.6, fs), 4401);

            // (a) Random automation of the stored Q above the limit vs static Q 18.
            {
                auto fixed = makeBand (fs, { 300.0, gain, limits::maxQ, false, type });
                auto automated = makeBand (fs, { 300.0, gain, limits::maxQ, false, type });
                auto y1 = input, y2 = input;
                run (fixed, y1, 64);
                std::mt19937 rng (77);
                std::uniform_real_distribution<double> u (0.0, 1.0);
                for (size_t pos = 0; pos < y2.size(); pos += 64)
                {
                    automated.setTargets ({ 300.0, gain, top * 1.0001 * std::pow (limits::maxQ / (top * 1.0001), u (rng)), false, type });
                    float* ch[] = { y2.data() + pos };
                    automated.process (ch, 1, (int) std::min<size_t> (64, y2.size() - pos));
                }
                LQ_CHECK (y1 == y2, str (name (type), " automation above the limit must be inaudible (bit-identical), fs=", fs));
            }

            // (b) Stored Q 18 -> 0.3 is exactly the ramp limit -> 0.3 (and back).
            for (const auto& [from, to] : { std::pair { limits::maxQ, 0.3 }, std::pair { 0.3, limits::maxQ } })
            {
                const auto fromE = effectiveQ (type, from), toE = effectiveQ (type, to);
                auto stored = makeBand (fs, { 300.0, gain, from, false, type });
                auto direct = makeBand (fs, { 300.0, gain, fromE, false, type });
                auto y1 = input, y2 = input;
                const auto change = samples (0.2, fs);
                bool monotonic = true;
                long arrived = -1;
                double lastQ = stored.getCurrentQ();
                for (size_t pos = 0, tick = 0; pos < y1.size(); pos += EqBand::updateIntervalSamples)
                {
                    if (pos == change - change % EqBand::updateIntervalSamples)
                    {
                        stored.setTargets ({ 300.0, gain, to, false, type });
                        direct.setTargets ({ 300.0, gain, toE, false, type });
                        tick = 1;
                    }
                    const auto n = (int) std::min<size_t> (EqBand::updateIntervalSamples, y1.size() - pos);
                    float* c1[] = { y1.data() + pos };
                    float* c2[] = { y2.data() + pos };
                    stored.process (c1, 1, n);
                    direct.process (c2, 1, n);
                    if (tick > 0)
                    {
                        const auto q = stored.getCurrentQ();
                        monotonic = monotonic && (to < from ? q <= lastQ : q >= lastQ);
                        lastQ = q;
                        if (arrived < 0 && q == toE)
                            arrived = (long) tick;
                        ++tick;
                    }
                }
                const auto ticks = std::lround (EqBand::rampTimeSeconds * fs / EqBand::updateIntervalSamples);
                LQ_CHECK (y1 == y2, str (name (type), " stored ", from, " -> ", to, " must equal ", fromE, " -> ", toE, ", fs=", fs));
                LQ_CHECK (monotonic && arrived == ticks, str (name (type), " effective Q ramp arrived after ", arrived, " ticks (expected ", ticks, ")"));
            }
        }
}

LQ_TEST ("q limit: a type change uses the effective Q of each path")
{
    constexpr double fs = 48000.0;
    auto band = makeBand (fs, { 400.0, 6.0, 18.0, false, FilterType::bell });
    const FilterType sequence[] = { FilterType::lowShelf, FilterType::bell, FilterType::highCut, FilterType::notch, FilterType::highShelf, FilterType::lowCut };

    for (const auto type : sequence)
    {
        band.setTargets ({ 400.0, 6.0, 18.0, false, type });
        auto x = makeNoise (samples (0.05, fs), 12);
        run (band, x);
        const auto e = effectiveQ (type, 18.0);
        LQ_CHECK (! band.isCrossfading() && band.getActiveFilter() == type && designsWithQ (band, type, 6.0, e),
                  str ("after switching to ", name (type), " the band must design with Q ", e));
    }
}

LQ_TEST ("q limit: processor keeps the stored Q in parameters and state; only the DSP uses the effective Q (5)")
{
    constexpr double fs = 48000.0;
    LabQ8AudioProcessor p;
    setStereo (p);
    setParam (p, "b1_type", 1.0f); // Low Shelf
    setParam (p, "b1_freq", 120.0f);
    setParam (p, "b1_gain", 9.0f);
    setParam (p, "b1_q", 18.0f);
    setParam (p, "b2_type", 4.0f); // High Cut
    setParam (p, "b2_freq", 9000.0f);
    setParam (p, "b2_q", 12.0f);
    p.prepareToPlay (fs, 480);

    // The adapter passes the stored Q; feeding the core the effective Q instead
    // gives the same output: the limit lives in the DSP only.
    EqCore stored, effective;
    for (int b = 0; b < params::numBands; ++b)
    {
        auto s = expectedSettings (p, b);
        stored.setBandTargets (b, s);
        s.q = effectiveQ (s.type, clampQ (s.q));
        effective.setBandTargets (b, s);
    }
    stored.prepare (fs, 2);
    effective.prepare (fs, 2);

    LabQ8AudioProcessor p2;
    setStereo (p2);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    p2.setStateInformation (state.getData(), (int) state.getSize());
    p2.prepareToPlay (fs, 480);

    LQ_CHECK (processorMatchesCore (p, stored, 40, 500), "processor must equal the core fed with the stored Q");
    LQ_CHECK (processorMatchesCore (p2, effective, 40, 500), "and the core fed with the effective Q");
    LQ_CHECK (std::abs (stored.getBand (0).getCurrentQ() - effectiveMax (FilterType::lowShelf)) < 1.0e-12
                  && std::abs (stored.getBand (1).getCurrentQ() - effectiveMax (FilterType::highCut)) < 1.0e-12,
              "the bands report the effective Q");

    // Parameters and state keep the stored values.
    const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    double savedQ1 = 0.0, savedQ2 = 0.0;
    for (auto* param : xml->getChildWithTagNameIterator ("PARAM"))
    {
        if (param->getStringAttribute ("id") == "b1_q") savedQ1 = param->getDoubleAttribute ("value");
        if (param->getStringAttribute ("id") == "b2_q") savedQ2 = param->getDoubleAttribute ("value");
    }
    LQ_CHECK (savedQ1 == (double) getParam (p, "b1_q") && std::abs (savedQ1 - 18.0) < 1.0e-5, str ("saved b1_q ", savedQ1));
    LQ_CHECK (savedQ2 == (double) getParam (p, "b2_q") && std::abs (savedQ2 - 12.0) < 1.0e-5, str ("saved b2_q ", savedQ2));
    LQ_CHECK (getParam (p2, "b1_q") == getParam (p, "b1_q") && getParam (p2, "b2_q") == getParam (p, "b2_q"), "restored stored Q");

    // Switching band 1 back to Bell reuses the stored Q 18.
    setParam (p, "b1_type", 0.0f);
    stored.setBandTargets (0, expectedSettings (p, 0));
    LQ_CHECK (processorMatchesCore (p, stored, 40, 900), "processor after the type change");
    LQ_CHECK (std::abs (stored.getBand (0).getCurrentQ() - (double) getParam (p, "b1_q")) < 1.0e-12 * 18.0, "Bell uses the stored Q again");
}

LQ_TEST ("state: genuine E1b sessions keep their stored Q; inside the limits nothing changes (N)")
{
    for (const auto* fixture : { "e1b_high_q", "e1b_within_limits" })
    {
        const auto xml = juce::XmlDocument::parse (juce::File (LABQ8_TEST_FIXTURES_DIR).getChildFile (juce::String (fixture) + ".xml"));
        LQ_CHECK (xml != nullptr, str ("missing ", fixture, ".xml"));
        if (xml == nullptr)
            continue;

        LabQ8AudioProcessor p;
        const auto block = loadFixture ((juce::String (fixture) + ".bin").toRawUTF8());
        p.setStateInformation (block.getData(), (int) block.getSize());

        int restored = 0, limited = 0;
        double worstDeltaDb = 0.0;
        for (auto* param : xml->getChildWithTagNameIterator ("PARAM"))
        {
            const auto id = param->getStringAttribute ("id");
            // Same tolerance as the E1a fixture test: on x86_64 (no FMA) the
            // host's normalise / denormalise round trip can move a value by an ulp.
            const auto stored = (float) param->getDoubleAttribute ("value");
            restored += std::abs (getParam (p, id.toRawUTF8()) - stored) <= 5.0e-7f * std::max (24.0f, std::abs (stored)) ? 1 : 0;
        }
        LQ_CHECK (restored == 40, str (fixture, ": ", restored, " of the 40 E1b parameters restored"));

        for (int b = 0; b < params::numBands; ++b)
        {
            const auto s = expectedSettings (p, b);
            const auto q = clampQ (s.q), e = effectiveQ (s.type, q);
            if (e == q)
                continue;
            ++limited;

            // Response change of this band vs E1b (design at the stored Q).
            const auto fs = 48000.0;
            const auto gain = s.gainDb;
            for (int i = 0; i < 2000; ++i)
            {
                const auto f = 20.0 * std::pow (1000.0, i / 1999.0);
                const auto before = magnitudeAt (designFilter (s.type, fs, s.frequencyHz, gain, q), f, fs);
                const auto after = magnitudeAt (designFilter (s.type, fs, s.frequencyHz, gain, e), f, fs);
                worstDeltaDb = std::max (worstDeltaDb, std::abs (db (after) - db (before)));
            }
            ctx.note (str (fixture, " b", b + 1, " ", name (s.type), ": stored Q ", s.q, " -> effective ", e));
        }

        if (juce::String (fixture) == "e1b_within_limits")
            LQ_CHECK (limited == 0, "a session inside the limits must use every stored Q unchanged (sounds exactly like E1b)");
        else
        {
            LQ_CHECK (limited == 4, str ("e1b_high_q has 4 shelves/cuts above the limit, found ", limited));
            ctx.note (str ("e1b_high_q: largest response change vs E1b at 48 kHz: ", worstDeltaDb, " dB"));
        }

        // Saving again keeps the stored values (state is not rewritten by the limit).
        LabQ8AudioProcessor p2;
        juce::MemoryBlock again;
        p.getStateInformation (again);
        p2.setStateInformation (again.getData(), (int) again.getSize());
        bool same = true;
        for (const auto& ids : params::bandIDs)
            same = same && getParam (p2, ids.q) == getParam (p, ids.q) && getParam (p2, ids.type) == getParam (p, ids.type);
        LQ_CHECK (same, str (fixture, ": stored Q and type survive a save / load cycle"));
    }
}
