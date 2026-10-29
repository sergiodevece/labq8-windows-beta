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
// Usage: labq8_ui_snapshot <master.png> <output_dir> [--final]
//        --final writes the final UI-1 captures instead: an active band, a
//        band in bypass and a non-Bell band with Dynamic EQ disabled.

#include "parameters/ParameterIDs.h"
#include "plugin/PluginProcessor.h"
#include "ui/LabQ8Editor.h"
#include "ui/UiGeometry.h"

#include <cstdio>

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
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: labq8_ui_snapshot <master.png> <output_dir>\n");
        return 2;
    }
    if (juce::String (argv[1]) == "--fonts")
    {
        // Font study: rendered size of the master's sample texts. Master:
        // "820 Hz" 54 x 13 px (cap 12), "BAND 4" 76 x 15 (cap 14),
        // "Bell" 23 x 12 (cap 11). Printed: height used, rendered w x h of
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
                for (const auto& [text, cap] : { std::pair { "820 Hz", 12 }, std::pair { "BAND 4", 14 }, std::pair { "Bell", 11 } })
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
