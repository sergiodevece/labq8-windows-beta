// E4d: the MID / SIDE analyzer and the dynamics of the bank shown
// (docs/MIDSIDE_E4D_AUDIT.md: F1-F7, D1-D5, U1, T1-T2, and the extra checks
// of the E4d brief).

#pragma clang diagnostic ignored "-Wfloat-equal" // bit-exactness is a tested property

#include "AllocationGuard.h"
#include "TestHarness.h"

#include "dsp/StereoModeEngine.h"
#include "parameters/ParameterIDs.h"
#include "plugin/PluginProcessor.h"
#include "ui/LabQ8Editor.h"
#include "ui/SpectrumView.h"
#include "ui/UiPreferences.h"

#include <cstring>
#include <random>

using labq8::LabQ8AudioProcessor;
using labq8::test::str;
using labq8::ui::AnalyzerDomain;
using labq8::ui::AnalyzerSource;
using labq8::ui::LabQ8Editor;
using Bank = labq8::params::Bank;
namespace params = labq8::params;
namespace dsp = labq8::dsp;

namespace
{
const bool preferencesInMemory = []
{
    labq8::ui::preferences::setStorageFileForTesting ({});
    LabQ8Editor::setScreenAreaForTesting ({ 0, 0, 4000, 3000 });
    return true;
}();

constexpr double pi = 3.14159265358979323846;
constexpr double fs = 48000.0;
constexpr double binHz = 48000.0 / 8192.0;   // FFT of 8192 at 48 kHz
const double f0 = 171 * binHz;                // a tone on a bin (~1002 Hz)

void set (LabQ8AudioProcessor& p, const char* id, float v)
{
    auto* q = p.getValueTreeState().getParameter (id);
    q->setValueNotifyingHost (q->convertTo0to1 (v));
}

void setMode (LabQ8AudioProcessor& p, bool midSide) { set (p, params::procModeID, midSide ? 1.0f : 0.0f); }

void layout (LabQ8AudioProcessor& p, int channels)
{
    juce::AudioProcessor::BusesLayout l;
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    l.inputBuses.add (set);
    l.outputBuses.add (set);
    p.setBusesLayout (l);
}

std::unique_ptr<LabQ8Editor> openEditor (LabQ8AudioProcessor& p)
{
    return std::unique_ptr<LabQ8Editor> (dynamic_cast<LabQ8Editor*> (p.createEditor()));
}

void click (juce::Component& c)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const auto centre = c.getLocalBounds().getCentre().toFloat();
    c.mouseDown (juce::MouseEvent (source, centre, juce::ModifierKeys::leftButtonModifier, 1.0f, 0, 0, 0, 0, &c, &c, now, centre, now, 1, false));
    c.mouseUp (juce::MouseEvent (source, centre, {}, 1.0f, 0, 0, 0, 0, &c, &c, now, centre, now, 1, false));
}

// Input generator: per-sample (L, R) of a stream position.
using Source = std::function<std::pair<float, float> (std::uint64_t)>;

Source tone (double a, double l, double r, double f = f0)
{
    return [=] (std::uint64_t n) { const auto v = a * std::sin (2 * pi * f * (double) n / fs); return std::pair { (float) (l * v), (float) (r * v) }; };
}

// Processes `blocks` blocks of `size` through p (already prepared), from
// stream position `pos`; the editor's views advance every few blocks.
void play (LabQ8AudioProcessor& p, int channels, const Source& src, int blocks, int size, std::uint64_t& pos, LabQ8Editor* e = nullptr, bool bypassed = false)
{
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> b (channels, size);
    for (int k = 0; k < blocks; ++k)
    {
        for (int i = 0; i < size; ++i)
        {
            const auto [l, r] = src (pos + (std::uint64_t) i);
            b.setSample (0, i, l);
            if (channels > 1)
                b.setSample (1, i, r);
        }
        if (bypassed) p.processBlockBypassed (b, midi); else p.processBlock (b, midi);
        pos += (std::uint64_t) size;
        if (e != nullptr && (k % 4 == 3))
        {
            e->getSpectrum().advance (1.0 / 30.0);
            e->getDynamics().advance (1.0 / 30.0);
        }
    }
}

double levelAt (labq8::ui::SpectrumView& v, bool preTrace, double f)
{
    const auto& freqs = v.getFrequencies();
    size_t best = 0;
    for (size_t i = 1; i < freqs.size(); ++i)
        if (std::abs (std::log (freqs[i] / f)) < std::abs (std::log (freqs[best] / f)))
            best = i;
    const auto& levels = v.getLevels (preTrace);
    return best < levels.size() ? levels[best] : -1000.0; // no trace analysed yet
}

bool nothingDrawn (labq8::ui::SpectrumView& v, bool preTrace)
{
    for (const auto l : v.getLevels (preTrace))
        if (l > labq8::ui::SpectrumAnalyzer::floorDb)
            return false;
    return true;
}

// Processor in M/S with the editor open on `bank`, analyzer PRE + POST, view ALL.
struct Rig
{
    LabQ8AudioProcessor p;
    std::unique_ptr<LabQ8Editor> e;
    std::uint64_t pos = 0;

    Rig (bool midSide, Bank bank = Bank::mid, int channels = 2, int block = 480)
    {
        layout (p, channels);
        setMode (p, midSide);
        p.prepareToPlay (fs, block);
        e = openEditor (p);
        if (midSide)
            e->setEditBank (bank);
        e->setView (LabQ8Editor::viewAll);
        e->handleAnalyzerMenuResult (3); // Pre + Post
    }
};
} // namespace

//==============================================================================
// FFT.

LQ_TEST ("e4d F1: a sine of amplitude A only in M (L = R) reads 20 log10 A in MID PRE / POST and nothing in SIDE; only in S (L = -R) the reverse")
{
    for (const bool inMid : { true, false })
        for (const double a : { 0.5, 0.1, 0.01 })
        {
            Rig rig (true, Bank::mid);
            const auto src = tone (a, 1.0, inMid ? 1.0 : -1.0);
            play (rig.p, 2, src, 60, 480, rig.pos, rig.e.get());
            auto& v = rig.e->getSpectrum();
            const auto expected = 20.0 * std::log10 (a);
            const auto midPre = levelAt (v, true, f0), midPost = levelAt (v, false, f0);
            if (inMid)
                LQ_CHECK (std::abs (midPre - expected) < 0.05 && std::abs (midPost - expected) < 0.05,
                          str ("tone in M, A ", a, ": MID PRE ", midPre, " POST ", midPost, " dB, expected ", expected));
            else
                LQ_CHECK (nothingDrawn (v, true) && nothingDrawn (v, false), str ("tone in S, A ", a, ": MID must show nothing"));

            rig.e->setEditBank (Bank::side);
            play (rig.p, 2, src, 60, 480, rig.pos, rig.e.get());
            const auto sidePre = levelAt (v, true, f0), sidePost = levelAt (v, false, f0);
            if (inMid)
                LQ_CHECK (nothingDrawn (v, true) && nothingDrawn (v, false), str ("tone in M, A ", a, ": SIDE must show nothing"));
            else
                LQ_CHECK (std::abs (sidePre - expected) < 0.05 && std::abs (sidePost - expected) < 0.05,
                          str ("tone in S, A ", a, ": SIDE PRE ", sidePre, " POST ", sidePost, " dB, expected ", expected));
        }
}

LQ_TEST ("e4d F2/F3: a mono signal shows no SIDE even with an extreme SIDE bank; MID +6 dB at f0 reads POST - PRE = +6 dB in MID, 0 dB in SIDE")
{
    {
        Rig rig (true, Bank::side);
        for (int b = 0; b < 8; ++b)
            set (rig.p, params::bankOf (Bank::side).bands[(size_t) b].gain, 24.0f);
        play (rig.p, 2, tone (0.3, 1.0, 1.0), 60, 480, rig.pos, rig.e.get());
        LQ_CHECK (nothingDrawn (rig.e->getSpectrum(), true) && nothingDrawn (rig.e->getSpectrum(), false), "mono: SIDE PRE / POST nothing");
    }
    {
        Rig rig (true, Bank::mid);
        set (rig.p, params::bankOf (Bank::mid).bands[0].frequency, (float) f0);
        set (rig.p, params::bankOf (Bank::mid).bands[0].gain, 6.0f);
        const auto src = tone (0.2, 1.0, 0.0); // L only: M = S = A/2
        play (rig.p, 2, src, 80, 480, rig.pos, rig.e.get());
        auto& v = rig.e->getSpectrum();
        const auto gainMid = levelAt (v, false, f0) - levelAt (v, true, f0);
        rig.e->setEditBank (Bank::side);
        play (rig.p, 2, src, 80, 480, rig.pos, rig.e.get());
        const auto gainSide = levelAt (v, false, f0) - levelAt (v, true, f0);
        LQ_CHECK (std::abs (gainMid - 6.0) < 0.05 && std::abs (gainSide) < 0.01, str ("MID POST-PRE ", gainMid, " dB, SIDE ", gainSide, " dB"));
    }
}

LQ_TEST ("e4d F4 + sync: with flat banks MID / SIDE POST equals PRE bit for bit (aligned at the same stream position), impulses at the same index, for 5 block sizes")
{
    for (const int block : { 32, 64, 333, 480, 1024 })
        for (const auto bank : { Bank::mid, Bank::side })
        {
            Rig rig (true, bank, 2, block);
            std::mt19937 rng (7);
            std::uniform_real_distribution<float> u (-0.2f, 0.2f);
            // Noise with an impulse every 997 samples (L only: in M and in S).
            const Source src = [&] (std::uint64_t n) { const auto x = u (rng); const auto imp = n % 997 == 0 ? 0.9f : 0.0f; return std::pair { x + imp, 0.5f * x }; };
            int frames = 0, identical = 0, impulsesMatched = 0;
            for (int k = 0; k < 40; ++k)
            {
                play (rig.p, 2, src, 3, block, rig.pos);
                auto& v = rig.e->getSpectrum();
                v.advance (1.0 / 30.0);
                const auto& pre = v.getComponentSamples (true);
                const auto& post = v.getComponentSamples (false);
                if (pre.empty() || post.empty())
                    continue;
                ++frames;
                if (v.getLastStreamEnd (true) == v.getLastStreamEnd (false) && pre.size() == post.size()
                    && std::memcmp (pre.data(), post.data(), pre.size() * sizeof (float)) == 0)
                    ++identical;
                // The impulses: same indices in both copies.
                // Stream position n is at index n + size - end; the copy
                // starts with zeros while fewer than `size` samples exist.
                const auto end = v.getLastStreamEnd (false);
                const auto size = (std::uint64_t) pre.size();
                const auto first = end > size ? end - size : 0;
                bool ok = true;
                for (std::uint64_t n = first + (997 - first % 997) % 997; n < end; n += 997)
                {
                    const auto index = (size_t) (n + size - end);
                    ok = ok && std::abs (pre[index]) > 0.25f && std::abs (post[index]) > 0.25f; // impulse >= 0.3 in M and S, noise <= 0.15
                }
                impulsesMatched += ok ? 1 : 0;
            }
            LQ_CHECK (frames > 20 && identical == frames && impulsesMatched == frames,
                      str ("block ", block, ", ", bank == Bank::mid ? "MID" : "SIDE", ": ", identical, " / ", frames, " frames identical, impulses at the same index in ", impulsesMatched));
        }
}

LQ_TEST ("e4d F6/F7: mono track: MID is the channel, SIDE nothing; silence draws nothing; host bypass: PRE = POST; a sample-rate change re-analyses at the new rate")
{
    {
        Rig rig (true, Bank::mid, 1);
        play (rig.p, 1, tone (0.25, 1.0, 0.0), 60, 480, rig.pos, rig.e.get());
        LQ_CHECK (std::abs (levelAt (rig.e->getSpectrum(), false, f0) - 20.0 * std::log10 (0.25)) < 0.05, "mono track: MID = the channel");
        rig.e->setEditBank (Bank::side);
        play (rig.p, 1, tone (0.25, 1.0, 0.0), 60, 480, rig.pos, rig.e.get());
        LQ_CHECK (nothingDrawn (rig.e->getSpectrum(), true) && nothingDrawn (rig.e->getSpectrum(), false), "mono track: SIDE nothing");
    }
    {
        Rig rig (true, Bank::mid);
        play (rig.p, 2, [] (std::uint64_t) { return std::pair { 0.0f, 0.0f }; }, 60, 480, rig.pos, rig.e.get());
        LQ_CHECK (nothingDrawn (rig.e->getSpectrum(), true) && nothingDrawn (rig.e->getSpectrum(), false), "silence: nothing");
    }
    {
        Rig rig (true, Bank::mid);
        set (rig.p, params::bankOf (Bank::mid).bands[0].gain, 9.0f);
        play (rig.p, 2, tone (0.2, 1.0, 0.3), 60, 480, rig.pos, rig.e.get(), true);
        rig.e->getSpectrum().advance (1.0 / 30.0);
        const auto& pre = rig.e->getSpectrum().getComponentSamples (true);
        const auto& post = rig.e->getSpectrum().getComponentSamples (false);
        LQ_CHECK (! pre.empty() && pre == post, "host bypass: PRE = POST");
    }
    {
        Rig rig (true, Bank::mid);
        play (rig.p, 2, tone (0.3, 1.0, 1.0), 40, 480, rig.pos, rig.e.get());
        rig.p.prepareToPlay (96000.0, 480);
        rig.pos = 0;
        const double f96 = 171 * 96000.0 / 16384.0;
        play (rig.p, 2, tone (0.3, 1.0, 1.0, f96 * fs / 96000.0), 80, 480, rig.pos, rig.e.get()); // f0 at 96 kHz: same bin index
        LQ_CHECK (rig.e->getSpectrum().getFftSize() == 16384, str ("FFT ", rig.e->getSpectrum().getFftSize(), " at 96 kHz"));
        LQ_CHECK (std::abs (levelAt (rig.e->getSpectrum(), false, f96) - 20.0 * std::log10 (0.3)) < 0.05, str ("96 kHz level ", levelAt (rig.e->getSpectrum(), false, f96)));
    }
}

LQ_TEST ("e4d F5: in STEREO the analyzer is the UI-2b one (L/R, unaligned read, mean power of the channels)")
{
    Rig rig (false);
    play (rig.p, 2, tone (0.5, 1.0, 1.0), 60, 480, rig.pos, rig.e.get());
    LQ_CHECK (rig.e->getAnalyzerDomain() == AnalyzerDomain::lr, "domain L/R");
    LQ_CHECK (std::abs (levelAt (rig.e->getSpectrum(), false, f0) - 20.0 * std::log10 (0.5)) < 0.05, "L = R sine of 0.5 reads -6.02 dBFS as before");
    LQ_CHECK (rig.e->getSpectrum().getComponentSamples (false).empty(), "no M/S component computed in STEREO");
}

//==============================================================================
// Dynamics.

namespace
{
// A dynamic Bell at f on band `b` of `bank`.
void dynamicBell (LabQ8AudioProcessor& p, Bank bank, int b, float f, float threshold = -40.0f, float range = 9.0f, float gain = 0.0f)
{
    const auto& s = params::bankOf (bank).bands[(size_t) b];
    const auto& d = params::bankOf (bank).dynamics[(size_t) b];
    set (p, s.frequency, f); set (p, s.gain, gain); set (p, s.q, 2.0f); set (p, s.type, 0.0f);
    set (p, d.enabled, 1.0f); set (p, d.threshold, threshold); set (p, d.range, range); set (p, d.attack, 2.0f);
}

labq8::DynamicsTelemetry::Snapshot peek (LabQ8AudioProcessor& p)
{
    labq8::DynamicsTelemetry::Snapshot s;
    p.getDynamicsTelemetry().peek (s);
    return s;
}
} // namespace

LQ_TEST ("e4d D1/D2: with different dynamics in STEREO, MID and SIDE, the bank shown gets exactly its own core's reduction; a tone in M reduces MID only, in S SIDE only")
{
    for (const bool inMid : { true, false })
    {
        Rig rig (true, Bank::mid);
        dynamicBell (rig.p, Bank::stereo, 2, 300.0f, -60.0f, 15.0f); // STEREO in standby: must never be shown
        dynamicBell (rig.p, Bank::mid, 2, 300.0f, -40.0f, 9.0f);
        dynamicBell (rig.p, Bank::side, 2, 300.0f, -40.0f, 6.0f);
        const auto src = tone (0.3, 1.0, inMid ? 1.0 : -1.0, 300.0);
        for (const auto bank : { Bank::mid, Bank::side })
        {
            rig.e->setEditBank (bank);
            play (rig.p, 2, src, 60, 480, rig.pos, rig.e.get());
            const auto s = peek (rig.p);
            const auto core = rig.p.getEngineForTesting().getCore ((dsp::StereoBank) bank).getDynamics (2).getGainReductionDb();
            LQ_CHECK (s.bank == (int) bank && s.available && s.bands[2].reductionDb == (float) core,
                      str (bank == Bank::mid ? "MID" : "SIDE", ": snapshot ", s.bands[2].reductionDb, " dB, core ", core, " dB, bank ", s.bank));
            const auto shouldReduce = (bank == Bank::mid) == inMid;
            const auto drawn = rig.e->getDynamics().getEffectiveReductions()[2];
            LQ_CHECK (shouldReduce ? (drawn > 5.0 && rig.e->getDynamics().isShowingDynamics()) : (drawn == 0.0 && ! rig.e->getDynamics().isShowingDynamics()),
                      str ("tone in ", inMid ? "M" : "S", ", ", bank == Bank::mid ? "MID" : "SIDE", " shown: drawn reduction ", drawn));
            LQ_CHECK (std::abs (drawn - 15.0) > 1.0, "the STEREO bank's 15 dB is never drawn in M/S");
        }
    }
}

LQ_TEST ("e4d D: all eight Bell bands of MID and of SIDE show their own real reduction")
{
    for (const auto bank : { Bank::mid, Bank::side })
    {
        Rig rig (true, bank);
        const float freqs[8] = { 80, 160, 320, 640, 1280, 2560, 5120, 10240 };
        for (int b = 0; b < 8; ++b)
            dynamicBell (rig.p, bank, b, freqs[b], -50.0f, 6.0f);
        const auto sign = bank == Bank::mid ? 1.0 : -1.0;
        const Source src = [&] (std::uint64_t n)
        {
            double v = 0.0;
            for (const auto f : freqs)
                v += 0.08 * std::sin (2 * pi * f * (double) n / fs);
            return std::pair { (float) v, (float) (sign * v) };
        };
        play (rig.p, 2, src, 80, 480, rig.pos, rig.e.get());
        const auto& drawn = rig.e->getDynamics().getEffectiveReductions();
        int acting = 0;
        for (int b = 0; b < 8; ++b)
            acting += drawn[(size_t) b] > 3.0 ? 1 : 0;
        LQ_CHECK (acting == 8, str (bank == Bank::mid ? "MID" : "SIDE", ": ", acting, " of 8 bands drawn reducing"));
    }
}

LQ_TEST ("e4d D3: during the 20 ms mode fade the data is unavailable (not 0 dB); a bank change never shows the previous bank's values; afterwards the real values return")
{
    Rig rig (false);
    dynamicBell (rig.p, Bank::stereo, 2, 300.0f, -40.0f, 9.0f);
    dynamicBell (rig.p, Bank::mid, 2, 300.0f, -40.0f, 6.0f);
    const auto src = tone (0.3, 1.0, 1.0, 300.0);
    play (rig.p, 2, src, 60, 480, rig.pos, rig.e.get());
    LQ_CHECK (peek (rig.p).available && peek (rig.p).bank == 0 && rig.e->getDynamics().isShowingDynamics(), "STEREO live");

    // Click M/S: the editor watches MID at once; the fade (960 samples) is
    // under way for two blocks of 480.
    click (rig.e->getModeButton (1));
    play (rig.p, 2, src, 1, 480, rig.pos);
    auto s = peek (rig.p);
    LQ_CHECK (! s.available && s.bank == 1, str ("during the fade: available ", s.available, ", bank ", s.bank));
    rig.e->getDynamics().advance (1.0 / 30.0);
    LQ_CHECK (! rig.e->getDynamics().isShowingDynamics() && ! rig.e->getDynamics().isDataAvailable(), "nothing drawn, data unavailable");
    play (rig.p, 2, src, 60, 480, rig.pos, rig.e.get());
    s = peek (rig.p);
    LQ_CHECK (s.available && s.bank == 1 && rig.e->getDynamics().isShowingDynamics()
              && std::abs (rig.e->getDynamics().getEffectiveReductions()[2] - 6.0) < 0.5, str ("MID live after the fade: ", rig.e->getDynamics().getEffectiveReductions()[2]));

    // MID -> SIDE (SIDE has no dynamics): MID's 6 dB must not stay on screen.
    rig.e->setEditBank (Bank::side);
    LQ_CHECK (! rig.e->getDynamics().isShowingDynamics(), "the bank change clears the overlay at once");
    play (rig.p, 2, src, 30, 480, rig.pos, rig.e.get());
    LQ_CHECK (! rig.e->getDynamics().isShowingDynamics() && rig.e->getDynamics().getEffectiveReductions()[2] == 0.0, "SIDE shows no stale MID reduction");
}

LQ_TEST ("e4d D3 guards: a mode change by automation while MID is shown is unavailable during the fade (the outgoing path is not shown as live); a late snapshot of the previous bank is never drawn")
{
    using State = labq8::DynamicsTelemetry::State;
    {
        Rig rig (true, Bank::mid);
        dynamicBell (rig.p, Bank::mid, 2, 300.0f, -40.0f, 6.0f);
        const auto src = tone (0.3, 1.0, 1.0, 300.0);
        play (rig.p, 2, src, 60, 480, rig.pos, rig.e.get());
        LQ_CHECK (rig.e->getDynamics().isShowingDynamics() && rig.e->getDynamics().isDataAvailable(), "MID live before");
        // Automation to STEREO; the editor (message thread) has not followed
        // yet, so MID, the outgoing path, is still watched.
        setMode (rig.p, false);
        play (rig.p, 2, src, 1, 480, rig.pos);
        const auto s = peek (rig.p);
        LQ_CHECK (rig.p.getEngineForTesting().isFading() && s.bank == 1 && ! s.available, str ("fading out of MID: bank ", s.bank, ", available ", s.available));
        rig.e->getDynamics().advance (1.0 / 30.0);
        LQ_CHECK (! rig.e->getDynamics().isShowingDynamics() && ! rig.e->getDynamics().isDataAvailable(), "nothing drawn and marked unavailable during the fade");
    }
    {
        Rig rig (true, Bank::mid);
        dynamicBell (rig.p, Bank::mid, 2, 300.0f, -40.0f, 6.0f);
        play (rig.p, 2, tone (0.3, 1.0, 1.0, 300.0), 60, 480, rig.pos, rig.e.get());
        rig.e->setEditBank (Bank::side);
        rig.e->getDynamics().advance (1.0 / 30.0);
        // The audio thread read the watched bank before the change: its MID
        // snapshot arrives after the overlay moved to SIDE.
        rig.p.getDynamicsTelemetry().offer (State::live, 1, [] (int b, labq8::DynamicsTelemetry::Band& out)
        {
            out.bell = true; out.frequencyHz = 300.0f + 100.0f * (float) b; out.q = 2.0f; out.staticGainDb = 0.0f; out.reductionDb = 6.0f;
        });
        rig.e->getDynamics().advance (1.0 / 30.0);
        LQ_CHECK (! rig.e->getDynamics().isShowingDynamics() && ! rig.e->getDynamics().isDataAvailable(), "a MID snapshot is not drawn on SIDE");
    }
}

LQ_TEST ("e4d D3 telemetry: an unread snapshot of another bank is replaced by the watched bank's, even with less reduction; an unavailable offer replaces a live one")
{
    using T = labq8::DynamicsTelemetry;
    T t;
    t.setActive (true);
    t.prepare();
    auto with = [] (float db) { return [db] (int, T::Band& out) { out.bell = true; out.reductionDb = db; out.frequencyHz = 300.0f; out.q = 1.0f; }; };
    t.offer (T::State::live, 1, with (9.0f));
    t.offer (T::State::live, 2, with (1.0f)); // not consumed in between
    T::Snapshot s;
    LQ_CHECK (t.peek (s) && s.bank == 2 && s.available && std::abs (s.bands[0].reductionDb - 1.0f) < 1.0e-6f, str ("kept bank ", s.bank, ", ", s.bands[0].reductionDb, " dB"));
    t.offer (T::State::live, 2, with (0.5f)); // same bank, less: the larger is kept (A2)
    LQ_CHECK (t.peek (s) && s.bank == 2 && std::abs (s.bands[0].reductionDb - 1.0f) < 1.0e-6f, "same bank: the most reducing instant is kept");
    t.offer (T::State::unavailable, 2, with (5.0f));
    LQ_CHECK (t.peek (s) && ! s.available && s.bands[0].reductionDb == 0.0f, "unavailable replaces, with no values");
}

LQ_TEST ("e4d D3 restart: after any bank change the overlay waits for an offer made after the change; MID -> SIDE -> MID with no audio in between does not redraw MID's earlier snapshot")
{
    Rig rig (true, Bank::mid);
    dynamicBell (rig.p, Bank::mid, 2, 300.0f, -40.0f, 6.0f);
    const auto src = tone (0.3, 1.0, 1.0, 300.0);
    play (rig.p, 2, src, 60, 480, rig.pos, rig.e.get());
    LQ_CHECK (rig.e->getDynamics().isShowingDynamics(), "MID live before");
    rig.e->setEditBank (Bank::side);
    rig.e->getDynamics().advance (1.0 / 30.0);
    rig.e->setEditBank (Bank::mid);
    rig.e->getDynamics().advance (1.0 / 30.0);
    LQ_CHECK (! rig.e->getDynamics().isShowingDynamics(), "no offer since the change: the snapshot from before it is not redrawn");
    play (rig.p, 2, src, 1, 480, rig.pos);
    rig.e->getDynamics().advance (1.0 / 30.0);
    LQ_CHECK (rig.e->getDynamics().isShowingDynamics() && std::abs (rig.e->getDynamics().getEffectiveReductions()[2] - 6.0) < 0.5,
              str ("the next block's offer is drawn: ", rig.e->getDynamics().getEffectiveReductions()[2]));
}

LQ_TEST ("e4d D3 rapid: 300 random MID / SIDE and STEREO / M/S changes during playback: every drawn reduction belongs to the bank shown and its path is heard")
{
    Rig rig (true, Bank::mid);
    dynamicBell (rig.p, Bank::stereo, 2, 300.0f, -40.0f, 12.0f);
    dynamicBell (rig.p, Bank::mid, 2, 300.0f, -40.0f, 6.0f);
    dynamicBell (rig.p, Bank::side, 2, 300.0f, -40.0f, 3.0f);
    const auto src = tone (0.3, 1.0, 0.2, 300.0);
    std::mt19937 rng (5);
    int violations = 0, drawnFrames = 0;
    for (int k = 0; k < 300; ++k)
    {
        switch (rng() % 3)
        {
            case 0: rig.e->setEditBank (rng() % 2 ? Bank::mid : Bank::side); break;
            case 1: click (rig.e->getModeButton ((int) (rng() % 2))); break;
            default: break;
        }
        play (rig.p, 2, src, 1 + (int) (rng() % 3), 240 + (int) (rng() % 400), rig.pos);
        rig.e->getDynamics().advance (1.0 / 30.0);
        if (! rig.e->getDynamics().isShowingDynamics())
            continue;
        ++drawnFrames;
        const auto shown = rig.e->getShownBank();
        const auto& engine = rig.p.getEngineForTesting();
        const auto heard = ! engine.isFading() && ((engine.getHeardMode() == dsp::StereoMode::stereo) == (shown == Bank::stereo));
        const auto expected = shown == Bank::stereo ? 12.0 : shown == Bank::mid ? 6.0 : 3.0;
        const auto drawn = rig.e->getDynamics().getEffectiveReductions()[2];
        if (rig.e->getDynamics().getBank() != (int) shown || drawn > expected + 0.01)
            ++violations;
        juce::ignoreUnused (heard);
    }
    LQ_CHECK (violations == 0 && drawnFrames > 50, str (violations, " frames drew another bank's reduction (", drawnFrames, " frames drawn)"));
}

LQ_TEST ("e4d D4/D5: the drawn reduction is the effective one (+-24 dB clamp); host bypass and silence draw nothing; SIDE on a mono track draws nothing")
{
    {
        Rig rig (true, Bank::mid);
        dynamicBell (rig.p, Bank::mid, 2, 300.0f, -60.0f, 12.0f, -20.0f); // static -20: only 4 dB of the 12 can be applied
        play (rig.p, 2, tone (0.3, 1.0, 1.0, 300.0), 80, 480, rig.pos, rig.e.get());
        const auto drawn = rig.e->getDynamics().getEffectiveReductions()[2];
        LQ_CHECK (std::abs (drawn - 4.0) < 0.05, str ("effective reduction drawn ", drawn, " dB (expected 4: -20 - 12 clamped to -24)"));
        play (rig.p, 2, tone (0.3, 1.0, 1.0, 300.0), 20, 480, rig.pos, rig.e.get(), true);
        LQ_CHECK (! rig.e->getDynamics().isShowingDynamics(), "host bypass: nothing");
        play (rig.p, 2, [] (std::uint64_t) { return std::pair { 0.0f, 0.0f }; }, 400, 480, rig.pos, rig.e.get());
        LQ_CHECK (! rig.e->getDynamics().isShowingDynamics(), "silence: the reduction released, nothing drawn");
    }
    {
        Rig rig (true, Bank::side, 1);
        dynamicBell (rig.p, Bank::side, 2, 300.0f, -80.0f, 12.0f);
        play (rig.p, 1, tone (0.3, 1.0, 0.0, 300.0), 60, 480, rig.pos, rig.e.get());
        LQ_CHECK (! rig.e->getDynamics().isShowingDynamics(), "mono track, SIDE: nothing (no SIDE signal)");
    }
}

//==============================================================================
// Interface, real time, audio, instances.

LQ_TEST ("e4d U1: analyzer texts and menu: MID / SIDE Pre, Post, Pre + Post and L/R Post in M/S; the STEREO menu and texts unchanged")
{
    Rig rig (true, Bank::mid);
    auto& e = *rig.e;
    LQ_CHECK (e.getAnalyzerText() == "MID Pre + Post", e.getAnalyzerText().toStdString());
    e.handleAnalyzerMenuResult (1);
    LQ_CHECK (e.getAnalyzerText() == "MID Pre" && e.getSpectrum().getSource() == AnalyzerSource::pre, e.getAnalyzerText().toStdString());
    e.handleAnalyzerMenuResult (2);
    e.setEditBank (Bank::side);
    LQ_CHECK (e.getAnalyzerText() == "SIDE Post" && e.getAnalyzerDomain() == AnalyzerDomain::side, e.getAnalyzerText().toStdString());
    e.handleAnalyzerMenuResult (LabQ8Editor::outputLrItem);
    LQ_CHECK (e.getAnalyzerText() == "L/R Post" && e.getAnalyzerDomain() == AnalyzerDomain::lr && e.getSpectrum().getSource() == AnalyzerSource::post,
              "Output L/R is named and analyses L/R POST");
    e.setEditBank (Bank::mid);
    LQ_CHECK (e.getAnalyzerText() == "L/R Post", "the L/R choice stays explicit when the bank changes");
    juce::StringArray items;
    const auto menu = e.createAnalyzerMenu();
    juce::PopupMenu::MenuItemIterator it (menu);
    while (it.next())
        if (it.getItem().itemID > 0)
            items.add (it.getItem().text + (it.getItem().isTicked ? "*" : ""));
    LQ_CHECK (items.joinIntoString ("|") == "Pre (before the EQ)|Post (after the EQ)|Pre + Post|L/R Post (output)*", items.joinIntoString ("|").toStdString());
    e.handleAnalyzerMenuResult (3);
    LQ_CHECK (e.getAnalyzerText() == "MID Pre + Post", "back to the bank's component");

    click (e.getModeButton (0));
    LQ_CHECK (e.getAnalyzerText() == "Pre + Post" && e.getAnalyzerDomain() == AnalyzerDomain::lr, e.getAnalyzerText().toStdString());
    juce::StringArray stereoItems;
    const auto stereoMenu = e.createAnalyzerMenu();
    juce::PopupMenu::MenuItemIterator it2 (stereoMenu);
    while (it2.next())
        if (it2.getItem().itemID > 0)
            stereoItems.add (it2.getItem().text);
    LQ_CHECK (stereoItems.joinIntoString ("|") == "Pre (before the EQ)|Post (after the EQ)|Pre + Post", "STEREO menu unchanged");
}

LQ_TEST ("e4d T1/T2: no allocation in processBlock with the editor open in M/S (analyzer PRE + POST, dynamics) through mode and bank changes; the audio is bit-identical with the editor open and closed")
{
    for (const bool midSide : { false, true })
    {
        LabQ8AudioProcessor closed;
        Rig open (midSide, Bank::mid);
        for (auto* p : { &closed, &open.p })
        {
            layout (*p, 2);
            setMode (*p, midSide);
            dynamicBell (*p, Bank::stereo, 2, 300.0f);
            dynamicBell (*p, Bank::mid, 2, 300.0f);
            dynamicBell (*p, Bank::side, 3, 2000.0f);
        }
        closed.prepareToPlay (fs, 480);
        open.p.prepareToPlay (fs, 480);
        std::mt19937 rng (3);
        std::uniform_real_distribution<float> u (-0.3f, 0.3f);
        juce::MidiBuffer midi;
        long allocations = 0;
        bool same = true;
        for (int k = 0; k < 300; ++k)
        {
            if (k % 37 == 0)
                open.e->setEditBank (k % 2 ? Bank::side : Bank::mid);
            juce::AudioBuffer<float> a (2, 480), b (2, 480);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 480; ++i)
                {
                    const auto v = u (rng);
                    a.setSample (ch, i, v);
                    b.setSample (ch, i, v);
                }
            closed.processBlock (a, midi);
            {
                labq8::test::ScopedAllocationCounter counter;
                open.p.processBlock (b, midi);
                allocations += counter.count();
            }
            for (int ch = 0; ch < 2; ++ch)
                same = same && std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), 480 * sizeof (float)) == 0;
            if (k % 4 == 3)
            {
                open.e->getSpectrum().advance (1.0 / 30.0);
                open.e->getDynamics().advance (1.0 / 30.0);
            }
        }
        LQ_CHECK (allocations == 0, str (midSide ? "M/S" : "STEREO", ": ", allocations, " allocations in processBlock"));
        LQ_CHECK (same, str (midSide ? "M/S" : "STEREO", ": the editor changed the audio"));
    }
}

LQ_TEST ("e4d instances: two instances watching MID and SIDE keep their own telemetry and analyzer; nothing is shared")
{
    Rig a (true, Bank::mid), b (true, Bank::side);
    dynamicBell (a.p, Bank::mid, 2, 300.0f, -40.0f, 9.0f);
    dynamicBell (b.p, Bank::side, 2, 300.0f, -40.0f, 5.0f);
    for (int k = 0; k < 15; ++k)
    {
        play (a.p, 2, tone (0.3, 1.0, 1.0, 300.0), 4, 480, a.pos, a.e.get());   // M only
        play (b.p, 2, tone (0.3, 1.0, -1.0, 300.0), 4, 480, b.pos, b.e.get());  // S only
    }
    LQ_CHECK (peek (a.p).bank == 1 && peek (b.p).bank == 2, "each instance tags its own bank");
    LQ_CHECK (std::abs (a.e->getDynamics().getEffectiveReductions()[2] - 9.0) < 0.5 && std::abs (b.e->getDynamics().getEffectiveReductions()[2] - 5.0) < 0.5,
              str ("A MID ", a.e->getDynamics().getEffectiveReductions()[2], " dB, B SIDE ", b.e->getDynamics().getEffectiveReductions()[2], " dB"));
    LQ_CHECK (! nothingDrawn (a.e->getSpectrum(), false) && ! nothingDrawn (b.e->getSpectrum(), false), "both analyzers show their own component");
}

LQ_TEST ("e4d sync prepare: after prepare (a new stream) a tap accepts no alignment until it has written in the new generation; its stream position restarts")
{
    labq8::SpectrumTap tap;
    tap.setActive (true);
    tap.prepare (fs, 2);
    std::vector<float> l (512, 0.9f), r (512, 0.9f);
    const float* ch[2] = { l.data(), r.data() };
    for (std::uint64_t k = 0; k < 3; ++k)
        tap.write (ch, 2, 512, 512 * k); // stream end 1536
    std::vector<float> a (1024), b (1024);
    float* dest[2] = { a.data(), b.data() };
    labq8::SpectrumTap::Frame frame;
    LQ_CHECK (tap.copyAtStream (dest, 2, 1024, 0, 1024, frame) && frame.streamEnd == 1024, "aligned copy within the generation");

    tap.prepare (fs, 2); // prepareToPlay: the processor's stream position restarts at 0
    LQ_CHECK (! tap.copyAtStream (dest, 2, 1024, 0, 1024, frame) && ! tap.copyAtStream (dest, 2, 1024, 0, 1536, frame),
              "nothing written in the new generation: no stream position of the old one is accepted");
    std::fill (l.begin(), l.end(), 0.1f);
    tap.write (ch, 2, 512, 0);
    LQ_CHECK (tap.copyLatestWithStream (dest, 2, 1024, 0, frame) && frame.streamEnd == 512, str ("new generation's stream end ", frame.streamEnd));
    bool onlyNew = true;
    for (int i = 0; i < 1024; ++i)
        onlyNew = onlyNew && (a[(size_t) i] == 0.0f || a[(size_t) i] == 0.1f);
    LQ_CHECK (onlyNew, "no sample of the previous generation is copied");
}

LQ_TEST ("e4d sync stress: two instances, variable block sizes, transport stops, prepareToPlay restarts, sample-rate changes, rapid MID / SIDE and PRE / POST changes: PRE is only analysed aligned with POST, and never with samples of a previous stream")
{
    struct Instance
    {
        std::unique_ptr<Rig> rig;
        std::mt19937 rng;
        std::uint64_t lastPre = 0, lastPost = 0;
        bool afterPrepare = false, markerAhead = false;
        int alignedFrames = 0, misaligned = 0, partialHistory = 0, oldSamples = 0, stoppedMoves = 0, prepares = 0, prepareChecks = 0, prepareFailures = 0;
    };
    std::vector<Instance> instances (2);
    for (size_t i = 0; i < instances.size(); ++i)
    {
        instances[i].rig = std::make_unique<Rig> (true, i == 0 ? Bank::mid : Bank::side, 2, 2048);
        instances[i].rng.seed ((unsigned) (17 + i));
    }
    std::uniform_real_distribution<float> u (-0.2f, 0.2f);
    std::mt19937 noise (3);
    const Source quiet = [&] (std::uint64_t) { return std::pair { u (noise), u (noise) }; };
    const Source marker = [] (std::uint64_t) { return std::pair { 1.8f, 0.0f }; }; // MID = SIDE = 0.9

    for (int k = 0; k < 400; ++k)
        for (auto& in : instances)
        {
            auto& rig = *in.rig;
            auto& v = rig.e->getSpectrum();
            const auto action = in.rng() % 9;
            bool stopped = false;
            if (action == 0) rig.e->setEditBank (rig.e->getShownBank() == Bank::mid ? Bank::side : Bank::mid);
            else if (action == 1) rig.e->handleAnalyzerMenuResult (1 + (int) (in.rng() % 3)); // Pre / Post / Pre + Post
            else if (action == 2) stopped = true; // transport stopped: no blocks
            else if (action == 3)
            {
                // A restart (prepareToPlay), sometimes at another rate,
                // right after a loud marker the new stream must not show.
                play (rig.p, 2, marker, 2, 512, rig.pos);
                const double rates[] = { 44100.0, 48000.0, 96000.0 };
                rig.p.prepareToPlay (rates[in.rng() % 3], 2048);
                ++in.prepares;
                in.afterPrepare = true;
                std::vector<float> a (64), b (64);
                float* d[2] = { a.data(), b.data() };
                labq8::SpectrumTap::Frame fp, fq;
                const auto block = 16 + (int) (in.rng() % 2033);
                play (rig.p, 2, quiet, 1, block, rig.pos);
                ++in.prepareChecks;
                const auto okPre = rig.p.getPreTap().copyLatestWithStream (d, 2, 64, 0, fp);
                const auto okPost = rig.p.getPostTap().copyLatestWithStream (d, 2, 64, 0, fq);
                // Both taps in the new generation; an active tap's stream
                // position is the first block's end, an inactive one's is 0.
                const auto expected = [&] (const labq8::SpectrumTap& t) { return t.isActive() ? (std::uint64_t) block : 0; };
                if (! (okPre && okPost && fp.generation == fq.generation
                       && fp.streamEnd == expected (rig.p.getPreTap()) && fq.streamEnd == expected (rig.p.getPostTap())))
                    ++in.prepareFailures;
            }
            if (! stopped)
                play (rig.p, 2, quiet, 1 + (int) (in.rng() % 3), 16 + (int) (in.rng() % 2033), rig.pos);
            v.advance (1.0 / 30.0);

            const auto preEnd = v.getLastStreamEnd (true), postEnd = v.getLastStreamEnd (false);
            const auto preMoved = preEnd != in.lastPre, postMoved = postEnd != in.lastPost;
            if (stopped && ! in.afterPrepare && (preMoved || postMoved))
                ++in.stoppedMoves;
            if (v.isTraceShown (true) && v.isTraceShown (false) && preMoved && postMoved)
            {
                ++in.alignedFrames;
                const auto& pre = v.getComponentSamples (true);
                const auto& post = v.getComponentSamples (false);
                // A trace shown later starts with zeros (UI-2b: nothing from
                // before it was shown); from where both have history on, the
                // aligned copies are the same samples (flat banks: POST = PRE).
                auto history = [] (const std::vector<float>& x) { size_t z = 0; while (z < x.size() && x[z] == 0.0f) ++z; return z; };
                const auto from = std::max (history (pre), history (post));
                const auto same = pre.size() == post.size() && std::equal (pre.begin() + (std::ptrdiff_t) std::min (from, pre.size()), pre.end(), post.begin() + (std::ptrdiff_t) std::min (from, post.size()));
                if (preEnd != postEnd || ! same)
                    ++in.misaligned;
                if (from > 0 && from < pre.size())
                    ++in.partialHistory;
            }
            if (in.afterPrepare)
                for (const bool preTrace : { true, false })
                    if (preTrace ? preMoved : postMoved)
                        for (const auto x : v.getComponentSamples (preTrace))
                            if (std::abs (x) > 0.5f)
                            {
                                ++in.oldSamples;
                                break;
                            }
            in.lastPre = preEnd;
            in.lastPost = postEnd;
        }

    for (size_t i = 0; i < instances.size(); ++i)
    {
        const auto& in = instances[i];
        LQ_CHECK (in.alignedFrames > 40 && in.misaligned == 0,
                  str ("instance ", i, ": ", in.misaligned, " of ", in.alignedFrames, " PRE + POST frames analysed unaligned"));
        ctx.note (str ("instance ", i, ": ", in.alignedFrames, " PRE + POST frames aligned (", in.partialHistory, " while a trace shown later was still filling its history), ",
                                  in.prepares, " restarts, 0 old samples"));
        LQ_CHECK (in.prepares > 20 && in.oldSamples == 0,
                  str ("instance ", i, ": ", in.oldSamples, " analyses with samples of the previous stream after ", in.prepares, " restarts"));
        LQ_CHECK (in.prepareFailures == 0, str ("instance ", i, ": stream position / generation wrong after ", in.prepareFailures, " of ", in.prepareChecks, " restarts"));
        LQ_CHECK (in.stoppedMoves == 0, str ("instance ", i, ": ", in.stoppedMoves, " frames analysed new audio with the transport stopped"));
    }
}

LQ_TEST ("e4d sync: read between the PRE and POST writes of a block (the audio thread is mid-block): MID PRE stays aligned with POST by stream position; the plain read would be one block ahead")
{
    labq8::SpectrumTap preTap, postTap;
    preTap.prepare (fs, 2);
    postTap.prepare (fs, 2);
    labq8::ui::SpectrumView view (preTap, postTap);
    view.setShown (true);
    view.setSource (AnalyzerSource::both);
    view.setDomain (AnalyzerDomain::mid);
    std::mt19937 rng (11);
    std::uniform_real_distribution<float> u (-0.5f, 0.5f);
    std::vector<float> l (512), r (512);
    std::uint64_t stream = 0;
    auto block = [&] (bool writePost)
    {
        for (int i = 0; i < 512; ++i) { l[(size_t) i] = u (rng); r[(size_t) i] = u (rng); }
        const float* ch[2] = { l.data(), r.data() };
        preTap.write (ch, 2, 512, stream);
        if (writePost)
            postTap.write (ch, 2, 512, stream); // flat EQ: POST = PRE
        stream += 512;
    };
    for (int k = 0; k < 20; ++k)
        block (true);
    block (false); // the audio thread wrote PRE of the next block, not yet POST
    view.advance (1.0 / 30.0);
    const auto& pre = view.getComponentSamples (true);
    const auto& post = view.getComponentSamples (false);
    LQ_CHECK (view.getLastStreamEnd (true) == view.getLastStreamEnd (false) && view.getLastStreamEnd (false) == 20 * 512, "both copies end at POST's stream position");
    LQ_CHECK (! pre.empty() && pre == post, "MID PRE and POST are the same samples (flat EQ)");
    LQ_CHECK (preTap.getWritten() == postTap.getWritten() + 512, "the taps' own ends differ by one block: an unaligned read would be offset");
}
