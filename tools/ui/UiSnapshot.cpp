// labq8_ui_snapshot: renders the real UI-1 editor (offscreen) with the
// parameter values shown in the approved design and compares it with the
// master, for visual review.
//
// Writes into <output_dir>:
//   editor_100.png      the editor at 100 % (1672 x 941)
//   editor_75.png       the editor at its default 75 % size
//   compare.png         master (top) over editor (bottom)
//   difference.png      |editor - master| x 4, per pixel
//   report.txt          mean absolute difference per region
//
// Usage: labq8_ui_snapshot <master.png> <output_dir> [--final | --ui11]
//        --final writes the final UI-1 captures instead: an active band, a
//        band in bypass and a non-Bell band with Dynamic EQ disabled.
//        --ui11 writes the UI-1.1 captures: band ON / OFF buttons, Q
//        handles per filter type and the editor at 100 / 60 / 40 / 140 %.
//        --ui2a writes the UI-2a meter captures: real audio through the
//        processor at known levels (silence, the master's level, over
//        level in amber and red, mono, independent channels) and the
//        difference with the master's meters.
//        --ui2b writes the UI-2b spectrum captures: a synthetic programme
//        through the processor (the master's EQ settings) in each view and
//        analyzer source, silence, 40 / 140 % zoom, the header against the
//        master, and the analysis / painting times.
// The user's zoom preference is never read or written (kept in memory), and
// every zoom is allowed whatever the screens of the machine.

#include "parameters/ParameterIDs.h"
#include "plugin/PluginProcessor.h"
#include "ui/LabQ8Editor.h"
#include "ui/UiGeometry.h"
#include "ui/UiPreferences.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <vector>

namespace
{
void setParam (labq8::LabQ8AudioProcessor& p, const char* id, float value)
{
    auto* parameter = p.getValueTreeState().getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

bool writePng (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat png;
    return stream.openedOk() && png.writeImageToStream (image, stream);
}

double meanDifference (const juce::Image& a, const juce::Image& b, juce::Rectangle<int> r)
{
    double sum = 0;
    for (int y = r.getY(); y < r.getBottom(); ++y)
        for (int x = r.getX(); x < r.getRight(); ++x)
        {
            const auto p = a.getPixelAt (x, y), q = b.getPixelAt (x, y);
            sum += std::abs (p.getRed() - q.getRed()) + std::abs (p.getGreen() - q.getGreen()) + std::abs (p.getBlue() - q.getBlue());
        }
    return sum / (3.0 * r.getWidth() * r.getHeight());
}
// The state of the master's panel (band 4) and an approximation of its curve.
void setMasterState (labq8::LabQ8AudioProcessor& processor)
{
    struct Band { float freq, gain, q; int type; };
    const Band bands[8] = { { 40, 1.0f, 0.9f, 0 }, { 90, 2.5f, 1.0f, 0 }, { 210, 0.5f, 1.4f, 0 }, { 820, -4.2f, 1.2f, 0 },
                            { 1200, 6.0f, 1.2f, 0 }, { 3000, 0.4f, 1.0f, 0 }, { 7000, -0.4f, 1.5f, 0 }, { 18000, -4.0f, 0.7f, 2 } };
    for (int b = 0; b < 8; ++b)
    {
        const auto& ids = labq8::params::bandIDs[(size_t) b];
        setParam (processor, ids.frequency, bands[b].freq);
        setParam (processor, ids.gain, bands[b].gain);
        setParam (processor, ids.q, bands[b].q);
        setParam (processor, ids.type, (float) bands[b].type);
    }
    const auto& dyn = labq8::params::dynamicIDs[3];
    setParam (processor, dyn.enabled, 1.0f);
    setParam (processor, dyn.threshold, -20.0f);
    setParam (processor, dyn.range, 8.0f);
    setParam (processor, dyn.attack, 10.0f);
    setParam (processor, dyn.release, 120.0f);
    processor.setUiSelectedBand (3);
}

// The real editor at 100 %, after `configure`.
juce::Image render (const std::function<void (labq8::LabQ8AudioProcessor&)>& configure)
{
    labq8::LabQ8AudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    setMasterState (processor);
    configure (processor);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1672, 941);
    return editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f).convertedToFormat (juce::Image::RGB);
}

// Final UI-1 captures (approved review items integrated).
int finalCaptures (const juce::File& outDir)
{
    bool ok = true;
    auto save = [&] (const juce::Image& image, const char* name) { ok = writePng (image, outDir.getChildFile (name)) && ok; };

    // An active band (band 4, Bell, Dynamic EQ on), with a band beyond
    // +-18 dB to show the +-24 dB screen.
    save (render ([] (labq8::LabQ8AudioProcessor& p) { setParam (p, "b2_gain", 21.0f); }), "final_1_band_active.png");

    // A band in bypass (band 5, selected): indicator dimmed, node at 35 %.
    save (render ([] (labq8::LabQ8AudioProcessor& p)
    {
        setParam (p, "b5_bypass", 1.0f);
        p.setUiSelectedBand (4);
    }), "final_2_band_bypassed.png");

    // A non-Bell filter (band 4 Low Shelf) with b4_dyn_on stored ON: the
    // switch is shown disabled.
    save (render ([] (labq8::LabQ8AudioProcessor& p) { setParam (p, "b4_type", 1.0f); }), "final_3_low_shelf_dynamic_disabled.png");

    std::printf ("%s\n", ok ? "final captures written" : "FAILED");
    return ok ? 0 : 1;
}

//==============================================================================
// UI-1.1 captures of the real editor: the ON / OFF band buttons and the Q
// handles approved from the proposal preview (now drawn by the editor), and
// the editor at its zoom sizes.
namespace ui11
{
// The real editor at `zoom` after `configure`, Q handles shown on
// `handlesBand` (-1: none).
juce::Image render (const std::function<void (labq8::LabQ8AudioProcessor&)>& configure, int handlesBand, float zoom = 1.0f)
{
    labq8::LabQ8AudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    setMasterState (processor);
    configure (processor);
    std::unique_ptr<juce::AudioProcessorEditor> base (processor.createEditor());
    auto* editor = dynamic_cast<labq8::ui::LabQ8Editor*> (base.get());
    editor->setZoom (zoom);
    if (handlesBand >= 0)
        editor->showQHandles (handlesBand);
    return editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f).convertedToFormat (juce::Image::RGB);
}

juce::Image stack (const std::vector<juce::Image>& images)
{
    int height = 0;
    for (const auto& i : images)
        height += i.getHeight();
    juce::Image sheet (juce::Image::RGB, images.front().getWidth(), height, true);
    juce::Graphics gr (sheet);
    int y = 0;
    for (const auto& i : images)
    {
        gr.drawImageAt (i, 0, y);
        y += i.getHeight();
    }
    return sheet;
}

int captures (const juce::File& outDir)
{
    bool ok = true;
    auto save = [&] (const juce::Image& image, const char* name) { ok = writePng (image, outDir.getChildFile (name)) && ok; };
    const juce::Rectangle<int> buttons (340, 522, 990, 74), screen (360, 236, 970, 252);

    auto mixed = [] (labq8::LabQ8AudioProcessor& p) { setParam (p, "b2_bypass", 1.0f); setParam (p, "b5_bypass", 1.0f); setParam (p, "b7_bypass", 1.0f); };
    auto none = [] (labq8::LabQ8AudioProcessor&) {};
    const auto full = render (mixed, 3);
    save (full, "ui11_1_editor_100.png");
    save (render (mixed, 3, 0.6f), "ui11_2_editor_60_default.png");
    save (render (mixed, 3, 0.4f), "ui11_3_editor_40.png");
    save (render (mixed, 3, 1.4f), "ui11_6_editor_140.png");

    save (stack ({ full.getClippedImage (buttons), render (none, -1).getClippedImage (buttons),
                   render ([] (labq8::LabQ8AudioProcessor& p) { for (const auto& ids : labq8::params::bandIDs) setParam (p, ids.bypass, 1.0f); }, -1).getClippedImage (buttons) }),
          "ui11_4_buttons_mixed_on_off.png");

    save (stack ({
              render (none, 3).getClippedImage (screen),
              render ([] (labq8::LabQ8AudioProcessor& p) { setParam (p, "b4_q", 4.0f); setParam (p, "b4_gain", 8.0f); }, 3).getClippedImage (screen),
              render ([] (labq8::LabQ8AudioProcessor& p) { setParam (p, "b2_type", 1.0f); setParam (p, "b2_gain", 6.0f); setParam (p, "b2_q", 0.7f); }, 1).getClippedImage (screen),
              render ([] (labq8::LabQ8AudioProcessor& p) { setParam (p, "b8_type", 4.0f); setParam (p, "b8_freq", 12000.0f); setParam (p, "b8_q", 1.5f); }, 7).getClippedImage (screen),
              render ([] (labq8::LabQ8AudioProcessor& p) { setParam (p, "b8_type", 4.0f); setParam (p, "b8_freq", 12000.0f); setParam (p, "b8_q", 6.0f); }, 7).getClippedImage (screen),
              render ([] (labq8::LabQ8AudioProcessor& p) { setParam (p, "b6_type", 5.0f); setParam (p, "b6_q", 6.0f); }, 5).getClippedImage (screen),
              render ([] (labq8::LabQ8AudioProcessor& p) { setParam (p, "b5_bypass", 1.0f); }, 4).getClippedImage (screen) }),
          "ui11_5_q_handles.png");

    std::printf ("%s\n", ok ? "UI-1.1 captures written" : "FAILED");
    return ok ? 0 : 1;
}
} // namespace ui11

//==============================================================================
// UI-2a captures: the IN / OUT meters after real audio at known levels.
namespace ui2a
{
// The editor after one frame of the meters, the processor having played a
// 1 kHz sine whose crest is `inDb` dBFS on L and `inDbRight` on R, with a
// Bell of `bellDb` at 1 kHz (OUT = IN when 0).
juce::Image render (double inDb, double inDbRight, float bellDb = 0.0f, int channels = 2, float zoom = 1.0f,
                    std::function<void (labq8::ui::LabQ8Editor&)> measure = {})
{
    labq8::LabQ8AudioProcessor processor;
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    processor.setBusesLayout (layout);
    setMasterState (processor);
    for (const auto& ids : labq8::params::bandIDs)
        setParam (processor, ids.gain, 0.0f);
    for (const auto& ids : labq8::params::dynamicIDs)
        setParam (processor, ids.enabled, 0.0f);
    setParam (processor, labq8::params::bandIDs[3].frequency, 1000.0f);
    setParam (processor, labq8::params::bandIDs[3].gain, bellDb);
    setParam (processor, labq8::params::bandIDs[3].q, 1.0f);
    processor.prepareToPlay (48000.0, 480);
    std::unique_ptr<juce::AudioProcessorEditor> base (processor.createEditor());
    auto* editor = dynamic_cast<labq8::ui::LabQ8Editor*> (base.get());
    editor->setZoom (zoom);

    const double amplitude[] = { inDb < -200.0 ? 0.0 : std::pow (10.0, inDb / 20.0),
                                 inDbRight < -200.0 ? 0.0 : std::pow (10.0, inDbRight / 20.0) };
    juce::AudioBuffer<float> buffer (channels, 480);
    juce::MidiBuffer midi;
    for (int block = 0; block < 100; ++block) // 1 s: the Bell has settled
    {
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < 480; ++i)
                buffer.setSample (ch, i, (float) (amplitude[ch] * std::sin (2.0 * juce::MathConstants<double>::pi * (block * 480 + i) / 48.0)));
        if (block == 99)
            editor->getMeters().advance (0.0); // discard the ramp, keep the last block
        processor.processBlock (buffer, midi);
    }
    editor->getMeters().advance (100.0); // the earlier level has fallen away: only the last block shows
    if (measure)
        measure (*editor);
    return editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f).convertedToFormat (juce::Image::RGB);
}

juce::Image meters (const juce::Image& editor)
{
    return editor.getClippedImage ({ 1380, 196, 180, 300 }).rescaled (360, 600, juce::Graphics::highResamplingQuality);
}

juce::Image row (const std::vector<juce::Image>& images)
{
    juce::Image sheet (juce::Image::RGB, (int) images.size() * images.front().getWidth(), images.front().getHeight(), true);
    juce::Graphics gr (sheet);
    for (size_t i = 0; i < images.size(); ++i)
        gr.drawImageAt (images[i], (int) i * images.front().getWidth(), 0);
    return sheet;
}

int captures (const juce::Image& master, const juce::File& outDir)
{
    bool ok = true;
    auto save = [&] (const juce::Image& image, const char* name) { ok = writePng (image, outDir.getChildFile (name)) && ok; };

    const auto silence = render (-300.0, -300.0);
    const auto masterLevel = render (-1.0, -1.0);   // 17 cells, as drawn in the master
    const auto amber = render (3.0, 3.0);            // up to +2.8 dB: three amber cells
    const auto red = render (9.0, 9.0);              // up to +8.9 dB: amber and red
    const auto full = render (13.0, 13.0);           // every cell
    save (masterLevel, "ui2a_1_editor_master_level.png");
    save (row ({ meters (silence), meters (masterLevel), meters (amber), meters (red), meters (full) }),
          "ui2a_2_states_silence_-1_+3_+9_+13.png");
    save (row ({ meters (render (-12.0, -30.0)), meters (render (-6.0, -6.0, 6.0f)), meters (render (-6.0, -300.0, 0.0f, 1)) }),
          "ui2a_3_independent_eq_mono.png");

    // The meters at the smallest and largest zoom (red state).
    const auto small = render (9.0, 3.0, 0.0f, 2, 0.4f), large = render (9.0, 3.0, 0.0f, 2, 1.4f);
    save (small.getClippedImage ({ 552, 78, 76, 124 }).rescaled (228, 372, juce::Graphics::highResamplingQuality), "ui2a_4_zoom_40.png");
    save (large.getClippedImage ({ 1932, 274, 252, 420 }), "ui2a_5_zoom_140.png");

    // Cost of repainting what a meter change repaints (one column with its
    // glow, through the editor: background and lit cells), software renderer.
    juce::String timing;
    for (const auto zoom : { 0.6f, 1.0f, 1.4f })
        render (9.0, 9.0, 0.0f, 2, zoom, [&] (labq8::ui::LabQ8Editor& editor)
        {
            const auto s = editor.getScale();
            const auto column = juce::Rectangle<float> (1389.0f, 220.0f, 26.0f, 256.0f).transformedBy (juce::AffineTransform::scale (s)).getSmallestIntegerContainer();
            std::vector<double> times;
            for (int i = 0; i < 200; ++i)
            {
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                const auto image = editor.createComponentSnapshot (column, true, 1.0f);
                times.push_back (juce::Time::getMillisecondCounterHiRes() - t0);
                juce::ignoreUnused (image);
            }
            std::sort (times.begin(), times.end());
            timing << "repaint of one column at " << juce::roundToInt (zoom * 100.0f) << " %: median "
                   << juce::String (times[times.size() / 2], 3) << " ms, 95th percentile " << juce::String (times[times.size() * 95 / 100], 3) << " ms\n";
        });
    std::printf ("%s", timing.toRawUTF8());

    // The master's level against the master, in the meters' area.
    const juce::Rectangle<int> area (1385, 300, 170, 180);
    double sum = 0.0;
    int worst = 0, count = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            const auto a = masterLevel.getPixelAt (x, y), b = master.getPixelAt (x, y);
            for (const auto d : { std::abs (a.getRed() - b.getRed()), std::abs (a.getGreen() - b.getGreen()), std::abs (a.getBlue() - b.getBlue()) })
            {
                sum += d;
                worst = std::max (worst, d);
                ++count;
            }
        }
    const auto report = juce::String ("meters at -1 dBFS vs the master, x 1385-1554, y 300-479: mean |difference| ")
                        + juce::String (sum / count, 3) + ", max " + juce::String (worst) + " (of 255)\n";
    ok = outDir.getChildFile ("ui2a_report.txt").replaceWithText (report + timing) && ok;
    std::printf ("%s", report.toRawUTF8());
    std::printf ("%s\n", ok ? "UI-2a captures written" : "FAILED");
    return ok ? 0 : 1;
}
} // namespace ui2a

//==============================================================================
// UI-2b captures: the spectrum after real audio through the processor.
namespace ui2b
{
// A programme-like signal: a bass line with harmonics, a chord, pink noise and
// a kick, about -10 dBFS peak. Deterministic.
float programme (int channel, long i, double fs)
{
    static thread_local float b0 = 0, b1 = 0, b2 = 0;
    static thread_local juce::Random random (17);
    const auto t = (double) i / fs;
    const auto twoPi = juce::MathConstants<double>::twoPi;
    double v = 0.0;
    for (int h = 1; h <= 6; ++h)
        v += 0.12 / h * std::sin (twoPi * 55.0 * h * t + h);
    for (const double f : { 220.0, 277.18, 329.63, 440.0, 1318.5 })
        v += 0.035 * std::sin (twoPi * f * t + f + channel * 0.3);
    const auto white = random.nextFloat() * 2.0f - 1.0f; // Paul Kellet's economy pink filter
    b0 = 0.99765f * b0 + white * 0.0990460f;
    b1 = 0.96300f * b1 + white * 0.2965164f;
    b2 = 0.57000f * b2 + white * 1.0526913f;
    v += 0.035 * (b0 + b1 + b2 + white * 0.1848f);
    const auto beat = std::fmod (t, 0.5);
    v += 0.25 * std::exp (-beat * 30.0) * std::sin (twoPi * (50.0 + 80.0 * std::exp (-beat * 40.0)) * beat);
    return (float) v;
}

struct Shot
{
    juce::Image image;
    double analysisMs = 0.0;
    int fftSize = 0;
};

// The editor (view, source, zoom) after `seconds` of the programme (or of
// silence) at `fs`, with the master's EQ settings.
Shot render (int view, labq8::ui::AnalyzerSource source, float zoom = 1.0f, bool silent = false, double fs = 48000.0,
             std::function<void (labq8::ui::LabQ8Editor&, labq8::LabQ8AudioProcessor&, long&)> measure = {})
{
    labq8::LabQ8AudioProcessor processor;
    setMasterState (processor);
    processor.prepareToPlay (fs, 512);
    std::unique_ptr<juce::AudioProcessorEditor> base (processor.createEditor());
    auto* editor = dynamic_cast<labq8::ui::LabQ8Editor*> (base.get());
    editor->setZoom (zoom);
    editor->setView (view);
    editor->setAnalyzerSource (source);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    long pos = 0;
    const auto total = (long) (1.5 * fs);
    while (pos < total)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (ch, i, silent ? 0.0f : programme (ch, pos + i, fs));
        processor.processBlock (buffer, midi);
        pos += 512;
        editor->getSpectrum().advance (512.0 / fs);
    }
    if (measure)
        measure (*editor, processor, pos);
    Shot shot;
    shot.analysisMs = editor->getSpectrum().getLastAnalysisMs();
    shot.fftSize = editor->getSpectrum().getFftSize();
    shot.image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f).convertedToFormat (juce::Image::RGB);
    return shot;
}

juce::Image screen (const juce::Image& editor)
{
    return editor.getClippedImage ({ 355, 186, 975, 330 });
}

int captures (const juce::Image& master, const juce::File& outDir)
{
    using labq8::ui::AnalyzerSource;
    using labq8::ui::LabQ8Editor;
    bool ok = true;
    auto save = [&] (const juce::Image& image, const char* name) { ok = writePng (image, outDir.getChildFile (name)) && ok; };

    const auto allPost = render (LabQ8Editor::viewAll, AnalyzerSource::post);
    save (allPost.image, "ui2b_1_editor_all_post.png");
    save (screen (render (LabQ8Editor::viewAll, AnalyzerSource::pre).image), "ui2b_2_all_pre.png");
    save (screen (render (LabQ8Editor::viewAll, AnalyzerSource::both).image), "ui2b_3_all_pre_post.png");
    save (screen (render (LabQ8Editor::viewSpectrum, AnalyzerSource::post).image), "ui2b_4_spectrum_post.png");
    const auto eqView = render (LabQ8Editor::viewEq, AnalyzerSource::post);
    save (screen (eqView.image), "ui2b_5_eq.png");
    save (screen (render (LabQ8Editor::viewAll, AnalyzerSource::post, 1.0f, true).image), "ui2b_6_all_silence.png");
    save (render (LabQ8Editor::viewEq, AnalyzerSource::post, 0.6f).image, "ui2b_9_editor_60_eq.png");
    save (render (LabQ8Editor::viewSpectrum, AnalyzerSource::post, 0.6f).image, "ui2b_10_editor_60_spectrum.png");
    save (render (LabQ8Editor::viewAll, AnalyzerSource::post, 0.6f).image, "ui2b_11_editor_60_all.png");
    save (render (LabQ8Editor::viewAll, AnalyzerSource::both, 0.4f).image, "ui2b_7_editor_40.png");
    save (render (LabQ8Editor::viewAll, AnalyzerSource::both, 1.4f).image, "ui2b_8_editor_140.png");

    // The header in the master's state (EQ selected, Analyzer: Post) against
    // the master: tabs and analyzer box (the removed View box excluded).
    juce::String report;
    for (const auto& [label, area] : { std::pair { "tabs (x 362-690, y 188-246)", juce::Rectangle<int> (362, 188, 329, 59) },
                                       std::pair { "analyzer box (x 948-1088, y 198-234)", juce::Rectangle<int> (948, 198, 141, 37) } })
    {
        double sum = 0.0;
        int worst = 0, count = 0, over16 = 0;
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
            {
                const auto a = eqView.image.getPixelAt (x, y), b = master.getPixelAt (x, y);
                const auto d = std::max ({ std::abs (a.getRed() - b.getRed()), std::abs (a.getGreen() - b.getGreen()), std::abs (a.getBlue() - b.getBlue()) });
                sum += d;
                worst = std::max (worst, d);
                over16 += d > 16 ? 1 : 0;
                ++count;
            }
        report << "header vs master, " << label << ": mean |difference| " << juce::String (sum / count, 2) << ", max " << worst
               << ", pixels over 16: " << over16 << " of " << count << "\n";
    }

    // Times: analysis of one frame (both traces) and painting the plot (as a
    // spectrum frame repaints it: background, spectrum, cached curve).
    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const auto zoom : { 0.6f, 1.0f, 1.4f })
        {
            if (fs != 48000.0 && zoom != 1.0f)
                continue;
            double analysis = 0.0;
            std::vector<double> paints, retina;
            int fft = 0;
            // The same plot without the spectrum (EQ view), for comparison.
            double withoutSpectrum = 0.0;
            render (LabQ8Editor::viewEq, AnalyzerSource::post, zoom, false, fs, [&] (LabQ8Editor& editor, labq8::LabQ8AudioProcessor&, long&)
            {
                const auto plot = editor.getSpectrum().getBounds().toFloat().transformedBy (juce::AffineTransform::scale (editor.getScale())).getSmallestIntegerContainer();
                std::vector<double> t;
                for (int i = 0; i < 60; ++i)
                {
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    const auto image = editor.createComponentSnapshot (plot, true, 1.0f);
                    t.push_back (juce::Time::getMillisecondCounterHiRes() - t0);
                    juce::ignoreUnused (image);
                }
                std::sort (t.begin(), t.end());
                withoutSpectrum = t[t.size() / 2];
            });
            render (LabQ8Editor::viewAll, AnalyzerSource::both, zoom, false, fs, [&] (LabQ8Editor& editor, labq8::LabQ8AudioProcessor& processor, long& pos)
            {
                // Each frame with a display frame's worth of new audio, so
                // both traces copy and analyse (one FFT per channel each).
                const auto block = (int) std::lround (fs / 60.0);
                juce::AudioBuffer<float> audio (2, block);
                juce::MidiBuffer none;
                std::vector<double> frames;
                for (int i = 0; i < 60; ++i)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        for (int k = 0; k < block; ++k)
                            audio.setSample (ch, k, programme (ch, pos + k, fs));
                    pos += block;
                    processor.processBlock (audio, none);
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    editor.getSpectrum().advance (1.0 / 60.0);
                    frames.push_back (juce::Time::getMillisecondCounterHiRes() - t0);
                }
                std::sort (frames.begin(), frames.end());
                analysis = frames[frames.size() / 2];
                fft = editor.getSpectrum().getFftSize();
                const auto plot = editor.getSpectrum().getBounds().toFloat().transformedBy (juce::AffineTransform::scale (editor.getScale())).getSmallestIntegerContainer();
                for (int i = 0; i < 60; ++i)
                {
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    const auto image = editor.createComponentSnapshot (plot, true, 1.0f);
                    paints.push_back (juce::Time::getMillisecondCounterHiRes() - t0);
                    juce::ignoreUnused (image);
                }
                // Retina (2 x the pixels per point).
                for (int i = 0; i < 30; ++i)
                {
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    const auto image = editor.createComponentSnapshot (plot, true, 2.0f);
                    retina.push_back (juce::Time::getMillisecondCounterHiRes() - t0);
                    juce::ignoreUnused (image);
                }
            });
            std::sort (retina.begin(), retina.end());
            std::sort (paints.begin(), paints.end());
            report << juce::String (fs / 1000.0, 1) << " kHz, zoom " << juce::roundToInt (zoom * 100.0f) << " %, FFT " << fft
                   << ": frame (2 copies + 4 FFTs + mapping) median " << juce::String (analysis, 3)
                   << " ms; plot repaint median " << juce::String (paints[paints.size() / 2], 3) << " ms, 95th "
                   << juce::String (paints[paints.size() * 95 / 100], 3) << " ms (without the spectrum: " << juce::String (withoutSpectrum, 3) << " ms); Retina 2x median "
                   << juce::String (retina[retina.size() / 2], 3) << " ms\n";
        }
    // Several instances at the usual 60 % (and 100 %): N independent
    // processors and editors, each with its own audio; per display frame of
    // the spectrum (1/30 s): every instance processes its audio, advances its
    // spectrum and repaints its plot. Totals for all instances.
    for (const auto zoom : { 0.6f, 1.0f })
        for (const auto source : { AnalyzerSource::post, AnalyzerSource::both })
            for (const int n : { 1, 2, 4, 8 })
            {
                const double fs = 48000.0;
                std::vector<std::unique_ptr<labq8::LabQ8AudioProcessor>> processors;
                std::vector<std::unique_ptr<juce::AudioProcessorEditor>> editors;
                for (int i = 0; i < n; ++i)
                {
                    processors.push_back (std::make_unique<labq8::LabQ8AudioProcessor>());
                    setMasterState (*processors.back());
                    processors.back()->prepareToPlay (fs, 1600);
                    editors.emplace_back (processors.back()->createEditor());
                    auto* e = dynamic_cast<LabQ8Editor*> (editors.back().get());
                    e->setZoom (zoom);
                    e->setView (LabQ8Editor::viewAll);
                    e->setAnalyzerSource (source);
                }
                juce::AudioBuffer<float> audio (2, 1600);
                juce::MidiBuffer none;
                long pos = 0;
                std::vector<double> frames1x, frames2x, analysisOnly;
                // One pass per display density (a window always draws at one:
                // alternating them would rebuild the cached curve every frame).
                for (const float density : { 1.0f, 2.0f })
                    for (int frame = 0; frame < 75; ++frame)
                    {
                        for (int ch = 0; ch < 2; ++ch)
                            for (int k = 0; k < 1600; ++k)
                                audio.setSample (ch, k, programme (ch, pos + k, fs));
                        pos += 1600;
                        double analysis = 0.0, paint = 0.0;
                        for (int i = 0; i < n; ++i)
                        {
                            auto copy = audio;
                            processors[(size_t) i]->processBlock (copy, none);
                            auto* e = dynamic_cast<LabQ8Editor*> (editors[(size_t) i].get());
                            auto t0 = juce::Time::getMillisecondCounterHiRes();
                            e->getSpectrum().advance (1.0 / 30.0);
                            analysis += juce::Time::getMillisecondCounterHiRes() - t0;
                            const auto plot = e->getSpectrum().getBounds().toFloat().transformedBy (juce::AffineTransform::scale (e->getScale())).getSmallestIntegerContainer();
                            t0 = juce::Time::getMillisecondCounterHiRes();
                            juce::ignoreUnused (e->createComponentSnapshot (plot, true, density));
                            paint += juce::Time::getMillisecondCounterHiRes() - t0;
                        }
                        if (frame >= 15) // settled
                        {
                            if (density == 1.0f)
                            {
                                analysisOnly.push_back (analysis);
                                frames1x.push_back (analysis + paint);
                            }
                            else
                                frames2x.push_back (analysis + paint);
                        }
                    }
                for (auto* v : { &analysisOnly, &frames1x, &frames2x })
                    std::sort (v->begin(), v->end());
                auto median = [] (const std::vector<double>& v) { return v[v.size() / 2]; };
                report << "instances: " << n << " at " << juce::roundToInt (zoom * 100.0f) << " %, "
                       << (source == AnalyzerSource::post ? "POST" : "PRE + POST") << ": per spectrum frame, all instances: analysis "
                       << juce::String (median (analysisOnly), 3) << " ms; analysis + paint "
                       << juce::String (median (frames1x), 3) << " ms (1x), " << juce::String (median (frames2x), 3)
                       << " ms (Retina 2x) = " << juce::String (median (frames2x) * 30.0 / 10.0, 1) << " % of one core at 30 frames/s (Retina)\n";
            }

    ok = outDir.getChildFile ("ui2b_report.txt").replaceWithText (report) && ok;
    std::printf ("%s", report.toRawUTF8());
    std::printf ("%s\n", ok ? "UI-2b captures written" : "FAILED");
    return ok ? 0 : 1;
}
} // namespace ui2b
} // namespace


int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    labq8::ui::preferences::setStorageFileForTesting ({});
    labq8::ui::LabQ8Editor::setScreenAreaForTesting ({ 0, 0, 4000, 3000 });
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: labq8_ui_snapshot <master.png> <output_dir>\n");
        return 2;
    }
    if (juce::String (argv[1]) == "--fonts")
    {
        // Font study: rendered size of the master's sample texts. Master:
        // "820 Hz" 54 x 13 px (cap 12), "BAND 4" 76 x 15 (cap 14),
        // "Bell" 23 x 12 (cap 11); UI-2b: "SPECTRUM" 65 x 11 (cap 11),
        // "Analyzer: Post" 90 x 14 (cap 11), selected "EQ" 18 x 14 (cap 12). Printed: height used, rendered w x h of
        // the ink (cap = ink height of "H").
        auto ink = [] (const juce::Font& font, const juce::String& text)
        {
            juce::Image image (juce::Image::RGB, 300, 80, true);
            juce::Graphics gr (image);
            gr.setColour (juce::Colours::white);
            gr.setFont (font);
            gr.drawText (text, 10, 10, 280, 60, juce::Justification::centredLeft, false);
            int x0 = 999, y0 = 999, x1 = -1, y1 = -1;
            for (int y = 0; y < 80; ++y)
                for (int x = 0; x < 300; ++x)
                    if (image.getPixelAt (x, y).getBrightness() > 0.45f)
                    {
                        x0 = std::min (x0, x); y0 = std::min (y0, y); x1 = std::max (x1, x); y1 = std::max (y1, y);
                    }
            return juce::Point<int> (x1 - x0 + 1, y1 - y0 + 1);
        };
        for (const auto* family : { "Helvetica Neue", "Avenir Next", "DIN Alternate", "Arial", "Lucida Grande" })
            for (const auto* style : { "Regular", "Medium", "Bold" })
            {
                juce::String line;
                line << family << " " << style << ":";
                for (const auto& [text, cap] : { std::pair { "820 Hz", 12 }, std::pair { "BAND 4", 14 }, std::pair { "Bell", 11 },
                                                std::pair { "SPECTRUM", 11 }, std::pair { "Analyzer: Post", 11 }, std::pair { "EQ", 12 } })
                {
                    // Height that gives the wanted cap height (ink of "H").
                    float height = 10.0f;
                    while (height < 40.0f && ink (juce::Font (juce::FontOptions (family, height, 0).withStyle (style)), "H").y < cap)
                        height += 0.25f;
                    const auto size = ink (juce::Font (juce::FontOptions (family, height, 0).withStyle (style)), text);
                    line << "  " << text << " h" << juce::String (height, 2) << " -> " << size.x << "x" << size.y;
                }
                std::printf ("%s\n", line.toRawUTF8());
            }
        return 0;
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto master = juce::ImageFileFormat::loadFrom (cwd.getChildFile (juce::String (argv[1]))).convertedToFormat (juce::Image::RGB);
    const auto outDir = cwd.getChildFile (juce::String (argv[2]));
    outDir.createDirectory();
    if (argc > 3 && juce::String (argv[3]) == "--final")
        return finalCaptures (outDir);
    if (argc > 3 && juce::String (argv[3]) == "--ui11")
        return ui11::captures (outDir);
    if (argc > 3 && juce::String (argv[3]) == "--ui2a")
        return ui2a::captures (master, outDir);
    if (argc > 3 && juce::String (argv[3]) == "--ui2b")
        return ui2b::captures (master, outDir);

    labq8::LabQ8AudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    // The state shown in the master's panel (band 4) and an approximation of
    // its curve for the other bands (the master is illustrative).
    struct Band { float freq, gain, q; int type; };
    const Band bands[8] = { { 40, 1.0f, 0.9f, 0 }, { 90, 2.5f, 1.0f, 0 }, { 210, 0.5f, 1.4f, 0 }, { 820, -4.2f, 1.2f, 0 },
                            { 1200, 6.0f, 1.2f, 0 }, { 3000, 0.4f, 1.0f, 0 }, { 7000, -0.4f, 1.5f, 0 }, { 18000, -4.0f, 0.7f, 2 } };
    for (int b = 0; b < 8; ++b)
    {
        const auto& ids = labq8::params::bandIDs[(size_t) b];
        setParam (processor, ids.frequency, bands[b].freq);
        setParam (processor, ids.gain, bands[b].gain);
        setParam (processor, ids.q, bands[b].q);
        setParam (processor, ids.type, (float) bands[b].type);
    }
    const auto& dyn = labq8::params::dynamicIDs[3];
    setParam (processor, dyn.enabled, 1.0f);
    setParam (processor, dyn.threshold, -20.0f);
    setParam (processor, dyn.range, 8.0f);
    setParam (processor, dyn.attack, 10.0f);
    setParam (processor, dyn.release, 120.0f);
    processor.setUiSelectedBand (3);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (labq8::ui::geometry::referenceWidth, labq8::ui::geometry::referenceHeight);
    const auto full = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f).convertedToFormat (juce::Image::RGB);
    editor->setSize (1254, 706);
    const auto small = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);

    // Paint cost: whole editor at 75 % and 100 %, software renderer (a
    // host's window is GPU-composited; this is an upper bound).
    juce::String timing;
    for (const int width : { 1254, 1672 })
    {
        editor->setSize (width, (int) std::lround (width * 941.0 / 1672.0));
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 20; ++i)
            editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        timing << "full repaint at " << width << " px: " << juce::String ((juce::Time::getMillisecondCounterHiRes() - start) / 20.0, 1) << " ms\n";
    }
    editor.reset();

    juce::Image compare (juce::Image::RGB, master.getWidth(), master.getHeight() * 2, true);
    juce::Image difference (juce::Image::RGB, master.getWidth(), master.getHeight(), true);
    {
        juce::Graphics gr (compare);
        gr.drawImageAt (master, 0, 0);
        gr.drawImageAt (full, 0, master.getHeight());
    }
    for (int y = 0; y < master.getHeight(); ++y)
        for (int x = 0; x < master.getWidth(); ++x)
        {
            const auto p = master.getPixelAt (x, y), q = full.getPixelAt (x, y);
            auto d = [] (int a, int b) { return (juce::uint8) std::min (255, 4 * std::abs (a - b)); };
            difference.setPixelAt (x, y, juce::Colour (d (p.getRed(), q.getRed()), d (p.getGreen(), q.getGreen()), d (p.getBlue(), q.getBlue())));
        }

    juce::String report;
    report << "UI-1 snapshot vs LABQ8_UI_DESIGN_APPROVED_01: mean |difference| per channel (0..255)\n";
    const std::pair<const char*, juce::Rectangle<int>> regions[] = {
        { "whole image", { 0, 0, 1672, 941 } },
        { "chassis top (logo, globals, signature)", { 0, 0, 1672, 170 } },
        { "screen (plot: illustrative curve/spectrum removed)", { 407, 247, 905, 230 } },
        { "meters (illustrative levels removed)", { 1372, 176, 196, 338 } },
        { "band selector", { 355, 530, 965, 58 } },
        { "panel header", { 120, 615, 1440, 50 } },
        { "knobs and displays", { 120, 680, 1440, 200 } },
    };
    for (const auto& [name, r] : regions)
        report << juce::String (meanDifference (master, full, r), 2) << "  " << name << "\n";
    report << timing;

    const bool ok = writePng (full, outDir.getChildFile ("editor_100.png")) && writePng (small, outDir.getChildFile ("editor_75.png"))
                 && writePng (compare, outDir.getChildFile ("compare.png")) && writePng (difference, outDir.getChildFile ("difference.png"))
                 && outDir.getChildFile ("report.txt").replaceWithText (report);
    std::printf ("%s%s\n", report.toRawUTF8(), ok ? "written" : "FAILED");
    return ok ? 0 : 1;
}
