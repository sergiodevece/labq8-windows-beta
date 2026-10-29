// Audio compatibility harness: an older plugin version ("base", e.g. E0 at
// dfcf25b, E1a at 9095401, E1b at 1e8db0b or E1b.1 at 5e889ec) against the
// current sources ("new").
//
// Compiled twice (LABQ8_COMPAT_SIDE "base" / "new"). Every case is a session
// of the base version:
//
//   base mode: builds each session's state with the base plugin (or takes a
//              genuine fixture), saves it, reopens it in a fresh base
//              instance and renders audio with scripted host automation.
//   new mode : opens the same saved state in a fresh current instance and
//              renders with the identical automation.
//   compare  : sample-by-sample comparison of the two renders. Without
//              --strict, differences are allowed in configurations that
//              passed through nominal 0 dB on band 1 (the documented E0
//              -> E1a change). With --qlimit, also in configurations where a
//              shelf had Q > 1 or a cut Q > 2 (the documented E1b -> E1b.1
//              effective-Q limit). Any other difference fails.
//
// Cases that need eight bands are skipped when the base has only one, and
// cases that need filter types when the base has none (before E1b).
//
// Usage: compat_<side> render <base_dir> <out_dir> [fixtures_dir]
//        compat_<side> compare <base_out> <new_out> [--strict] [--qlimit]

#include "plugin/PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

#ifndef LABQ8_COMPAT_SIDE
 #error "define LABQ8_COMPAT_SIDE"
#endif

namespace
{
struct Settings
{
    float freq = 1000.0f, gain = 0.0f, q = 1.0f;
    bool bypass = false;
};

using Automation = std::function<void (labq8::LabQ8AudioProcessor&, double t)>;

struct Case
{
    std::string name;
    std::string fixture; // genuine state file, or empty
    Settings initial;
    Automation automate; // called before every block, t = block start (s)
    bool needsEightBands = false;
    bool needsTypes = false;
};

void setParam (labq8::LabQ8AudioProcessor& p, const char* id, float value)
{
    auto* parameter = p.getValueTreeState().getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

float getParam (labq8::LabQ8AudioProcessor& p, const char* id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}

void apply (labq8::LabQ8AudioProcessor& p, const Settings& s)
{
    setParam (p, "b1_freq", s.freq);
    setParam (p, "b1_gain", s.gain);
    setParam (p, "b1_q", s.q);
    setParam (p, "b1_bypass", s.bypass ? 1.0f : 0.0f);
}

std::vector<Case> cases()
{
    std::vector<Case> c;
    auto add = [&] (std::string name, Settings s, Automation a = {}) { c.push_back ({ std::move (name), {}, s, std::move (a) }); };

    c.push_back ({ "fixture_e0_default", "e0_default.bin", {}, {} });
    c.push_back ({ "fixture_e0_custom", "e0_custom.bin", {}, {} });
    c.push_back ({ "fixture_e0_bypassed", "e0_bypassed.bin", {}, {} });

    // Nominal 0 dB and values that the 0.01 dB grid snaps to (or near) 0.
    add ("gain_0_nominal", { 1000.0f, 0.0f, 1.0f, false });
    add ("gain_0_q18_20Hz", { 20.0f, 0.0f, 18.0f, false });
    add ("gain_typed_+0.004", { 1000.0f, 0.004f, 1.0f, false });
    add ("gain_typed_-0.004", { 1000.0f, -0.004f, 1.0f, false });
    add ("gain_typed_+0.0049", { 1000.0f, 0.0049f, 1.0f, false });
    add ("gain_typed_+0.006", { 1000.0f, 0.006f, 1.0f, false });
    add ("gain_+0.01", { 1000.0f, 0.01f, 1.0f, false });
    add ("gain_-0.01", { 1000.0f, -0.01f, 1.0f, false });

    // Ordinary and extreme sessions.
    add ("gain_+6_1k", { 1000.0f, 6.0f, 1.0f, false });
    add ("gain_-24_20k_q0.1", { 20000.0f, -24.0f, 0.1f, false });
    add ("gain_+24_20Hz_q18", { 20.0f, 24.0f, 18.0f, false });
    add ("bypass_on_gain_+12", { 1000.0f, 12.0f, 2.0f, true });

    // Host automation.
    add ("auto_gain_steps_through_0", { 1000.0f, -0.05f, 1.0f, false },
         [] (labq8::LabQ8AudioProcessor& p, double t)
         { setParam (p, "b1_gain", (float) std::min (0.05, -0.05 + 0.01 * std::floor (t / 0.1))); });

    add ("auto_gain_toggle_0_6", { 1000.0f, 0.0f, 1.0f, false },
         [] (labq8::LabQ8AudioProcessor& p, double t)
         { setParam (p, "b1_gain", ((int) std::floor (t / 0.25) % 2) ? 6.0f : 0.0f); });

    add ("auto_bypass_toggle_gain_6", { 1000.0f, 6.0f, 1.0f, false },
         [] (labq8::LabQ8AudioProcessor& p, double t)
         { setParam (p, "b1_bypass", ((int) std::floor (t / 0.3) % 2) ? 1.0f : 0.0f); });

    add ("auto_freq_sweep_gain_0", { 100.0f, 0.0f, 2.0f, false },
         [] (labq8::LabQ8AudioProcessor& p, double t)
         { setParam (p, "b1_freq", (float) (100.0 * std::pow (100.0, t / 2.0))); });

    add ("auto_freq_sweep_gain_6", { 100.0f, 6.0f, 2.0f, false },
         [] (labq8::LabQ8AudioProcessor& p, double t)
         { setParam (p, "b1_freq", (float) (100.0 * std::pow (100.0, t / 2.0))); });

    add ("auto_all_params_random", { 1000.0f, 3.0f, 1.0f, false },
         [] (labq8::LabQ8AudioProcessor& p, double t)
         {
             std::mt19937 rng ((unsigned) std::llround (t * 1.0e6)); // depends only on time
             std::uniform_real_distribution<float> u (0.0f, 1.0f);
             setParam (p, "b1_freq", 20.0f * std::pow (1000.0f, u (rng)));
             setParam (p, "b1_gain", -24.0f + 48.0f * u (rng));
             setParam (p, "b1_q", 0.1f * std::pow (180.0f, u (rng)));
             setParam (p, "b1_bypass", u (rng) < 0.1f ? 1.0f : 0.0f);
         });

    // Eight-band sessions (E1a and later bases only).
    auto add8 = [&] (std::string name, std::string fixture, Automation a = {})
    { c.push_back ({ std::move (name), std::move (fixture), {}, std::move (a), true }); };

    add8 ("fixture_e1a_default", "e1a_default.bin");
    add8 ("fixture_e1a_custom", "e1a_custom.bin");
    add8 ("fixture_e1a_from_e0_custom", "e1a_from_e0_custom.bin");

    add8 ("auto_8band_random", "e1a_custom.bin",
          [] (labq8::LabQ8AudioProcessor& p, double t)
          {
              std::mt19937 rng ((unsigned) std::llround (t * 1.0e6) + 7u);
              std::uniform_real_distribution<float> u (0.0f, 1.0f);
              const int band = (int) (u (rng) * 7.999f) + 1;
              const auto prefix = "b" + std::to_string (band) + "_";
              setParam (p, (prefix + "freq").c_str(), 20.0f * std::pow (1000.0f, u (rng)));
              setParam (p, (prefix + "gain").c_str(), -24.0f + 48.0f * u (rng));
              setParam (p, (prefix + "q").c_str(), 0.1f * std::pow (180.0f, u (rng)));
              setParam (p, (prefix + "bypass").c_str(), u (rng) < 0.15f ? 1.0f : 0.0f);
          });

    add8 ("auto_8band_bypass_toggles", "e1a_custom.bin",
          [] (labq8::LabQ8AudioProcessor& p, double t)
          {
              for (int band = 1; band <= 8; ++band)
                  setParam (p, ("b" + std::to_string (band) + "_bypass").c_str(),
                            ((int) std::floor (t / (0.1 + 0.02 * band)) % 2) ? 1.0f : 0.0f);
          });

    // Typed sessions (E1b and later bases only): genuine E1b states and
    // automation of types and Q, inside and across the E1b.1 Q limits.
    auto addTyped = [&] (std::string name, std::string fixture, Automation a = {})
    { c.push_back ({ std::move (name), std::move (fixture), {}, std::move (a), true, true }); };

    addTyped ("fixture_e1b_within_limits", "e1b_within_limits.bin");
    addTyped ("fixture_e1b_high_q", "e1b_high_q.bin");

    addTyped ("auto_types_random_q_le_limit", "e1b_within_limits.bin",
              [] (labq8::LabQ8AudioProcessor& p, double t)
              {
                  // Random types and settings, Q never above the shelf limit.
                  std::mt19937 rng ((unsigned) std::llround (t * 1.0e6) + 11u);
                  std::uniform_real_distribution<float> u (0.0f, 1.0f);
                  const auto prefix = "b" + std::to_string ((int) (u (rng) * 7.999f) + 1) + "_";
                  setParam (p, (prefix + "type").c_str(), (float) (int) (u (rng) * 5.999f));
                  setParam (p, (prefix + "freq").c_str(), 20.0f * std::pow (1000.0f, u (rng)));
                  setParam (p, (prefix + "gain").c_str(), -24.0f + 48.0f * u (rng));
                  setParam (p, (prefix + "q").c_str(), 0.1f * std::pow (10.0f, u (rng)));
                  setParam (p, (prefix + "bypass").c_str(), u (rng) < 0.1f ? 1.0f : 0.0f);
              });

    addTyped ("auto_types_random_any_q", "e1b_high_q.bin",
              [] (labq8::LabQ8AudioProcessor& p, double t)
              {
                  std::mt19937 rng ((unsigned) std::llround (t * 1.0e6) + 13u);
                  std::uniform_real_distribution<float> u (0.0f, 1.0f);
                  const auto prefix = "b" + std::to_string ((int) (u (rng) * 7.999f) + 1) + "_";
                  setParam (p, (prefix + "type").c_str(), (float) (int) (u (rng) * 5.999f));
                  setParam (p, (prefix + "freq").c_str(), 20.0f * std::pow (1000.0f, u (rng)));
                  setParam (p, (prefix + "gain").c_str(), -24.0f + 48.0f * u (rng));
                  setParam (p, (prefix + "q").c_str(), 0.1f * std::pow (180.0f, u (rng)));
                  setParam (p, (prefix + "bypass").c_str(), u (rng) < 0.1f ? 1.0f : 0.0f);
              });

    addTyped ("auto_q_sweep_shelf_and_cut", "e1b_within_limits.bin",
              [] (labq8::LabQ8AudioProcessor& p, double t)
              {
                  // Q 0.1 -> 18 over the render on a low shelf (b1) and a high cut (b4).
                  const auto q = (float) (0.1 * std::pow (180.0, t / 2.0));
                  setParam (p, "b1_q", q);
                  setParam (p, "b4_q", q);
              });

    return c;
}

bool hasEightBands (labq8::LabQ8AudioProcessor& p)
{
    return p.getValueTreeState().getParameter ("b8_freq") != nullptr;
}

bool hasTypes (labq8::LabQ8AudioProcessor& p)
{
    return p.getValueTreeState().getParameter ("b1_type") != nullptr;
}

// True when some band is a shelf with Q above 1 or a cut with Q above 2 (the
// E1b.1 limits, + 1 ppm). Written out here: the base sources do not have them.
bool aboveQLimit (labq8::LabQ8AudioProcessor& p)
{
    if (! hasTypes (p))
        return false;

    for (int band = 1; band <= 8; ++band)
    {
        const auto prefix = "b" + std::to_string (band) + "_";
        const auto type = (int) std::lround (getParam (p, (prefix + "type").c_str()));
        const auto q = (double) getParam (p, (prefix + "q").c_str());
        if (((type == 1 || type == 2) && q > 1.000001) || ((type == 3 || type == 4) && q > 2.000002))
            return true;
    }
    return false;
}

struct Config
{
    double fs;
    int channels, block;
};

std::vector<Config> configs()
{
    std::vector<Config> v;
    for (const double fs : { 44100.0, 48000.0, 96000.0 })
        for (const int ch : { 1, 2 })
            for (const int block : { 64, 512 })
                v.push_back ({ fs, ch, block });
    return v;
}

std::string tag (const Config& c)
{
    return std::to_string ((int) c.fs) + "_" + std::to_string (c.channels) + "ch_" + std::to_string (c.block);
}

constexpr double renderSeconds = 2.0;

juce::AudioBuffer<float> makeInput (const Config& c)
{
    const auto n = (int) std::llround (renderSeconds * c.fs);
    juce::AudioBuffer<float> in (c.channels, n);
    for (int ch = 0; ch < c.channels; ++ch)
    {
        std::mt19937 rng (1000u + (unsigned) ch);
        std::uniform_real_distribution<float> d (-0.25f, 0.25f);
        for (int i = 0; i < n; ++i)
        {
            const auto t = i / c.fs;
            in.setSample (ch, i, d (rng) + 0.25f * (float) std::sin (2.0 * M_PI * 997.0 * t)
                                       + 0.1f * (float) std::sin (2.0 * M_PI * 55.0 * t));
        }
    }
    return in;
}

bool loadState (labq8::LabQ8AudioProcessor& p, const juce::File& f)
{
    juce::MemoryBlock block;
    if (! f.loadFileAsData (block) || block.getSize() == 0)
        return false;
    p.setStateInformation (block.getData(), (int) block.getSize());
    return true;
}

int render (const juce::File& baseDir, const juce::File& outDir, const juce::File& fixtures)
{
    const std::string side = LABQ8_COMPAT_SIDE;
    outDir.createDirectory();

    for (const auto& k : cases())
    {
        const auto stateFile = baseDir.getChildFile (k.name + ".state");

        if (side == "base") // the base plugin creates the session
        {
            labq8::LabQ8AudioProcessor author;
            if ((k.needsEightBands && ! hasEightBands (author)) || (k.needsTypes && ! hasTypes (author)))
            {
                stateFile.deleteFile();
                continue;
            }

            if (! k.fixture.empty())
            {
                if (! loadState (author, fixtures.getChildFile (k.fixture)))
                    return std::fprintf (stderr, "missing fixture %s\n", k.fixture.c_str()), 1;
            }
            else
            {
                apply (author, k.initial);
            }

            juce::MemoryBlock state;
            author.getStateInformation (state);
            stateFile.replaceWithData (state.getData(), state.getSize());
        }

        if (! stateFile.existsAsFile()) // case not supported by the base version
            continue;

        for (const auto& c : configs())
        {
            labq8::LabQ8AudioProcessor p; // reopen the session in a fresh instance
            if (! loadState (p, stateFile))
                return std::fprintf (stderr, "missing state %s\n", stateFile.getFullPathName().toRawUTF8()), 1;

            const auto set = c.channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (set);
            layout.outputBuses.add (set);
            p.setBusesLayout (layout);

            p.prepareToPlay (c.fs, c.block);
            auto audio = makeInput (c);
            juce::MidiBuffer midi;
            int nominalZeroBlocks = 0, qLimitBlocks = 0;
            double lastNominalZeroEnd = 0.0;

            for (int pos = 0; pos < audio.getNumSamples(); pos += c.block)
            {
                if (k.automate)
                    k.automate (p, pos / c.fs);

                const auto n = std::min (c.block, audio.getNumSamples() - pos);

                // Active band at the nominal 0 dB grid step (what E1a treats as exact 0).
                if (std::abs (getParam (p, "b1_gain")) < 0.005f && getParam (p, "b1_bypass") < 0.5f)
                {
                    ++nominalZeroBlocks;
                    lastNominalZeroEnd = (pos + n) / c.fs;
                }

                qLimitBlocks += aboveQLimit (p) ? 1 : 0;

                juce::AudioBuffer<float> view (audio.getArrayOfWritePointers(), c.channels, pos, n);
                p.processBlock (view, midi);
            }

            outDir.getChildFile (k.name + "__" + tag (c) + ".zero")
                .replaceWithText (juce::String (nominalZeroBlocks) + " " + juce::String (lastNominalZeroEnd, 9));
            outDir.getChildFile (k.name + "__" + tag (c) + ".qlim").replaceWithText (juce::String (qLimitBlocks));

            juce::FileOutputStream out (outDir.getChildFile (k.name + "__" + tag (c) + ".f32"));
            out.setPosition (0);
            out.truncate();
            for (int ch = 0; ch < c.channels; ++ch)
                out.write (audio.getReadPointer (ch), (size_t) audio.getNumSamples() * sizeof (float));

            // Restored b1 gain as the host/DSP sees it (first config only).
            if (c.fs == 44100.0 && c.channels == 1 && c.block == 64)
                outDir.getChildFile (k.name + ".gain").replaceWithText (juce::String (getParam (p, "b1_gain"), 12));
        }
    }
    return 0;
}

std::vector<float> readFloats (const juce::File& f)
{
    juce::MemoryBlock b;
    f.loadFileAsData (b);
    std::vector<float> v (b.getSize() / sizeof (float));
    std::memcpy (v.data(), b.getData(), v.size() * sizeof (float));
    return v;
}

// Every difference must come from a config that passed through nominal 0 dB
// (unless strict) or, with qLimit, from one where a shelf or cut was above
// its Q limit; anything else is reported as UNEXPECTED and fails the run.
int compare (const juce::File& a, const juce::File& b, bool strict, bool qLimit)
{
    std::printf ("%-29s %6s %6s %6s %9s %10s %8s %9s %9s  %s\n",
                 "case", "ident", "0dB", "Qlim", "diff smp", "max|diff|", "dBFS", "lastDiff", "settle", "verdict");
    int failures = 0;

    for (const auto& k : cases())
    {
        if (! a.getChildFile (k.name + ".state").existsAsFile())
            continue; // not available in this base version

        int identical = 0, total = 0, zeroConfigs = 0, qLimitConfigs = 0, unexpected = 0;
        bool qLimitDiff = false;
        long long differing = 0;
        double maxDiff = 0.0, lastDiffSeconds = 0.0, maxSettle = 0.0;

        for (const auto& c : configs())
        {
            const auto name = k.name + "__" + tag (c);
            const auto x = readFloats (a.getChildFile (name + ".f32")), y = readFloats (b.getChildFile (name + ".f32"));
            ++total;

            const auto zero = juce::StringArray::fromTokens (a.getChildFile (name + ".zero").loadFileAsString(), false);
            const bool passedZero = zero.size() == 2 && zero[0].getIntValue() > 0;
            const auto lastZeroEnd = zero.size() == 2 ? zero[1].getDoubleValue() : 0.0;
            zeroConfigs += passedZero ? 1 : 0;
            const bool passedQLimit = a.getChildFile (name + ".qlim").loadFileAsString().getIntValue() > 0;
            qLimitConfigs += passedQLimit ? 1 : 0;

            if (x.empty() || x.size() != y.size())
            {
                ++failures;
                continue;
            }

            const auto perChannel = x.size() / (size_t) c.channels;
            long long d = 0;
            double configLastDiff = 0.0;

            for (size_t i = 0; i < x.size(); ++i)
            {
                if (! std::isfinite (x[i]) || ! std::isfinite (y[i]))
                {
                    ++failures;
                    continue;
                }

                if (x[i] != y[i])
                {
                    ++d;
                    maxDiff = std::max (maxDiff, (double) std::abs (x[i] - y[i]));
                    configLastDiff = std::max (configLastDiff, (double) (i % perChannel) / c.fs);
                }
            }

            if (d > 0)
            {
                lastDiffSeconds = std::max (lastDiffSeconds, configLastDiff);
                const bool allowedZero = ! strict && passedZero;
                const bool allowedQLimit = qLimit && passedQLimit;
                if (allowedZero)
                    maxSettle = std::max (maxSettle, configLastDiff - lastZeroEnd); // time to converge after leaving 0 dB
                qLimitDiff = qLimitDiff || allowedQLimit;
                if (! allowedZero && ! allowedQLimit)
                    ++unexpected;
            }

            differing += d;
            identical += d == 0 ? 1 : 0;
        }

        failures += unexpected;
        std::printf ("%-29s %2d/%-3d %2d/%-3d %2d/%-3d %9lld %10.3g %8s %8.3fs %8.3fs  %s\n", k.name.c_str(), identical, total,
                     zeroConfigs, total, qLimitConfigs, total, differing, maxDiff,
                     maxDiff > 0.0 ? juce::String (20.0 * std::log10 (maxDiff), 1).toRawUTF8() : "-",
                     lastDiffSeconds, std::max (0.0, maxSettle),
                     unexpected > 0 ? "UNEXPECTED" : (differing == 0 ? "bit-identical" : (qLimitDiff ? "deliberate (Q limit)" : "deliberate (0 dB)")));
    }

    const char* ok = qLimit ? (strict ? "RESULT: OK (strict: differences only where a shelf/cut was above its Q limit)"
                                      : "RESULT: OK (all differences explained by nominal 0 dB or the Q limit)")
                            : (strict ? "RESULT: OK (strict: no differences)" : "RESULT: OK (all differences explained by nominal 0 dB)");
    std::printf ("%s\n", failures == 0 ? ok : "RESULT: FAIL");
    return failures == 0 ? 0 : 1;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String mode = argc > 1 ? argv[1] : "";

    if (mode == "render" && argc >= 4)
        return render (juce::File (juce::String (argv[2])), juce::File (juce::String (argv[3])),
                       argc > 4 ? juce::File (juce::String (argv[4])) : juce::File());

    if (mode == "compare" && argc >= 4)
    {
        bool strict = false, qLimit = false;
        for (int i = 4; i < argc; ++i)
        {
            strict = strict || juce::String (argv[i]) == "--strict";
            qLimit = qLimit || juce::String (argv[i]) == "--qlimit";
        }
        return compare (juce::File (juce::String (argv[2])), juce::File (juce::String (argv[3])), strict, qLimit);
    }

    std::fprintf (stderr, "usage: render <base_dir> <out_dir> [fixtures] | compare <base_out> <new_out> [--strict] [--qlimit]\n");
    return 2;
}
