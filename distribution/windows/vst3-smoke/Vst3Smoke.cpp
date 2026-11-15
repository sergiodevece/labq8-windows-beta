// LABQ8 Windows beta: loads the REAL built LABQ8.vst3 through JUCE's VST3 host
// and checks identity, the 80 public parameters, stereo and mono processing
// (static Bell and dynamic Bell), state round trip, automation, and the
// editor (with audio running on another thread, as in a host), capturing
// the native Windows window. Adapted from HARMOHIT MASTER R2's VST3Smoke.
// This is a JUCE test host: NOT Pro Tools, NOT Blue Cat PatchWork, NOT the
// Steinberg validator.
#if defined(_WIN32) && ! defined(NOMINMAX)
 #define NOMINMAX
#endif
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
using Plugin = std::unique_ptr<juce::AudioPluginInstance>;
constexpr double pi = 3.14159265358979323846;
int checks = 0;

void check (bool value, const std::string& what)
{
    ++checks;
    if (! value)
        throw std::runtime_error (what);
}

juce::AudioProcessorParameter& param (Plugin& p, const juce::String& name)
{
    for (auto* parameter : p->getParameters())
        if (parameter->getName (128) == name)
            return *parameter;
    throw std::runtime_error ("missing parameter " + name.toStdString());
}

void setText (Plugin& p, const juce::String& name, const juce::String& text)
{
    auto& x = param (p, name);
    x.setValueNotifyingHost (x.getValueForText (text));
}

void setNormalised (Plugin& p, const juce::String& name, float value) { param (p, name).setValueNotifyingHost (value); }

// RMS gain (dB) of `seconds` of a sine through `p`, measured over the last half.
double sineGainDb (Plugin& p, int channels, double hz, float amplitude, double seconds = 1.0)
{
    juce::AudioBuffer<float> buffer (channels, 256);
    juce::MidiBuffer midi;
    const auto blocks = (int) (seconds * 48000.0 / 256.0);
    double in = 0.0, out = 0.0;
    for (int block = 0; block < blocks; ++block)
    {
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < 256; ++i)
                buffer.setSample (c, i, (float) (amplitude * std::sin (2.0 * pi * hz * (block * 256 + i) / 48000.0)));
        const auto measure = block >= blocks / 2;
        if (measure)
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < 256; ++i)
                    in += (double) buffer.getSample (c, i) * buffer.getSample (c, i);
        p->processBlock (buffer, midi);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < 256; ++i)
            {
                const auto v = buffer.getSample (c, i);
                check (std::isfinite (v), "finite output");
                if (measure)
                    out += (double) v * v;
            }
    }
    return 10.0 * std::log10 (out / in);
}

// Programme-like: 90 Hz bass, 1 kHz bursts and a little noise.
float programme (int c, long i)
{
    static juce::Random random (7);
    const auto t = (double) i / 48000.0;
    const auto burst = std::fmod (t, 0.5) < 0.2 ? 0.3 : 0.05;
    return (float) (0.2 * std::sin (2.0 * pi * 90.0 * t) + burst * std::sin (2.0 * pi * 1000.0 * t + c)
                    + 0.02 * (random.nextFloat() - 0.5f));
}
} // namespace

int main (int argc, char** argv)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI gui;
        check (argc >= 2, "pass the built LABQ8.vst3 bundle path");
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> types;
        format.findAllTypesForFile (types, juce::String (argv[1]));
        check (types.size() == 1, "one VST3 effect in the bundle");
        const auto& d = *types[0];
        std::cout << "Loaded: name=" << d.name << " manufacturer=" << d.manufacturerName << " version=" << d.version
                  << " uid=" << d.uniqueId << " category=" << d.category << " instrument=" << d.isInstrument
                  << " inputs=" << d.numInputChannels << " outputs=" << d.numOutputChannels << '\n';
        check (d.name == "LABQ8" && d.manufacturerName == "VitaPlug" && d.version == "0.0.1" && ! d.isInstrument, "LABQ8 identity");

        auto create = [&]
        {
            juce::String error;
            auto p = format.createInstanceFromDescription (d, 48000.0, 256, error);
            check (p != nullptr, ("instance: " + error).toStdString());
            return p;
        };

        // The 80 public parameters, by name.
        auto a = create();
        // The 80 public parameters, plus the bypass parameter JUCE's VST3
        // wrapper adds for the host (not one of the plugin's own).
        int own = 0;
        juce::StringArray others;
        for (auto* p : a->getParameters())
        {
            if (p->getName (128).startsWith ("Band "))
                ++own;
            else
                others.add (p->getName (128));
        }
        std::cout << "parameters: " << a->getParameters().size() << " (" << own << " Band n ..., others: " << others.joinIntoString (", ") << ")\n";
        check (own == 80 && others.size() <= 1, "80 public parameters (+ the VST3 host bypass)");
        for (int band = 1; band <= 8; ++band)
            for (const auto* control : { "Frequency", "Gain", "Q", "Bypass", "Type", "Dynamic EQ", "Threshold", "Range", "Attack", "Release" })
                param (a, "Band " + juce::String (band) + " " + control);
        std::cout << "PASS 80 parameters (Band 1..8: Frequency, Gain, Q, Bypass, Type, Dynamic EQ, Threshold, Range, Attack, Release)\n";

        // Stereo: a flat EQ passes the sine unchanged; a +6 dB Bell at 1 kHz
        // gives +6 dB at 1 kHz; with dynamics (threshold -40, range 6) the
        // reduction brings it back to about 0 dB.
        a->setPlayConfigDetails (2, 2, 48000.0, 256);
        a->prepareToPlay (48000.0, 256);
        const auto flat = sineGainDb (a, 2, 1000.0, 0.25f);
        setText (a, "Band 1 Type", "Bell");
        setText (a, "Band 1 Frequency", "1000");
        setText (a, "Band 1 Q", "1");
        setText (a, "Band 1 Gain", "6");
        const auto bell = sineGainDb (a, 2, 1000.0, 0.25f);
        setText (a, "Band 1 Threshold", "-40");
        setText (a, "Band 1 Range", "6");
        setNormalised (a, "Band 1 Dynamic EQ", 1.0f);
        const auto dynamic = sineGainDb (a, 2, 1000.0, 0.25f);
        std::cout << "stereo 1 kHz: flat " << flat << " dB, Bell +6 " << bell << " dB, dynamic Bell " << dynamic << " dB\n";
        check (std::abs (flat) < 1.0e-6 && std::abs (bell - 6.0) < 0.1 && std::abs (dynamic) < 0.3, "stereo static and dynamic Bell");

        // Mono, if the host can set a mono layout.
        auto m = create();
        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        const auto monoOk = m->setBusesLayout (mono);
        std::cout << "mono layout accepted: " << monoOk << '\n';
        check (monoOk, "mono layout");
        m->prepareToPlay (48000.0, 256);
        setText (m, "Band 1 Type", "Bell");
        setText (m, "Band 1 Frequency", "1000");
        setText (m, "Band 1 Q", "1");
        setText (m, "Band 1 Gain", "6");
        const auto monoBell = sineGainDb (m, 1, 1000.0, 0.25f);
        std::cout << "mono 1 kHz Bell +6: " << monoBell << " dB\n";
        check (std::abs (monoBell - 6.0) < 0.1, "mono Bell");

        // State: a's state into two new instances; same values, same audio, bit for bit.
        juce::MemoryBlock state;
        a->getStateInformation (state);
        auto b = create(), c = create();
        for (auto* x : { &b, &c })
        {
            (*x)->setStateInformation (state.getData(), (int) state.getSize());
            (*x)->setPlayConfigDetails (2, 2, 48000.0, 256);
            (*x)->prepareToPlay (48000.0, 256);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        for (int i = 0; i < a->getParameters().size(); ++i)
            check (std::abs (a->getParameters()[i]->getValue() - b->getParameters()[i]->getValue()) < 1.0e-6f, "state round trip, parameter " + std::to_string (i));
        juce::AudioBuffer<float> x (2, 256), y (2, 256);
        juce::MidiBuffer midi;
        double maxDifference = 0.0;
        for (int block = 0; block < 200; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    x.setSample (ch, i, programme (ch, block * 256 + i));
            y.makeCopyOf (x);
            b->processBlock (x, midi);
            c->processBlock (y, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    maxDifference = std::max (maxDifference, (double) std::abs (x.getSample (ch, i) - y.getSample (ch, i)));
        }
        std::cout << "state round trip: all values equal; two restored instances differ by " << maxDifference << '\n';
        check (maxDifference == 0.0, "restored instances process identically");

        // Automation: many parameters moving every block, all eight bands.
        juce::Random random (11);
        for (int block = 0; block < 400; ++block)
        {
            const auto band = juce::String (1 + block % 8);
            setNormalised (b, "Band " + band + " Gain", random.nextFloat());
            setNormalised (b, "Band " + band + " Frequency", random.nextFloat());
            setNormalised (b, "Band " + band + " Q", random.nextFloat());
            if (block % 13 == 0)
                setNormalised (b, "Band " + band + " Type", random.nextFloat());
            if (block % 7 == 0)
                setNormalised (b, "Band " + band + " Dynamic EQ", random.nextBool() ? 1.0f : 0.0f);
            setNormalised (b, "Band " + band + " Threshold", random.nextFloat());
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    x.setSample (ch, i, programme (ch, block * 256 + i));
            b->processBlock (x, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    check (std::isfinite (x.getSample (ch, i)) && std::abs (x.getSample (ch, i)) < 64.0f, "automated output finite and bounded");
        }
        std::cout << "PASS automation of gain / frequency / Q / type / dynamics on all bands (400 blocks)\n";

        // Editor, with audio running on another thread (meters, FFT, dynamics).
        setText (a, "Band 1 Threshold", "-36");
        setText (a, "Band 1 Range", "8");
        setText (a, "Band 2 Frequency", "90");
        setText (a, "Band 2 Gain", "3");
        std::unique_ptr<juce::AudioProcessorEditor> editor (a->createEditorAndMakeActive());
        check (editor != nullptr, "editor");
        std::cout << "editor size: " << editor->getWidth() << " x " << editor->getHeight() << '\n';
        check (editor->getWidth() == 1003 && editor->getHeight() == 565, "editor at the default zoom 60 % (1003 x 565)");
        juce::DocumentWindow window ("LABQ8 VST3 Windows validation", juce::Colours::black, 0);
        window.setUsingNativeTitleBar (true);
        window.setContentNonOwned (editor.get(), true);
        window.centreWithSize (editor->getWidth(), editor->getHeight());
        window.setVisible (true);
        window.toFront (false);
        std::atomic<bool> stop { false };
        std::thread audio ([&]
        {
            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer none;
            long position = 0;
            while (! stop.load())
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 256; ++i)
                        buffer.setSample (ch, i, programme (ch, position + i));
                position += 256;
                a->processBlock (buffer, none);
                std::this_thread::sleep_for (std::chrono::microseconds (5333)); // about real time
            }
        });
        juce::MessageManager::getInstance()->runDispatchLoopUntil (3000);
#if JUCE_WINDOWS
        if (argc >= 3)
        {
            const auto hwnd = static_cast<HWND> (window.getWindowHandle());
            RECT rect {};
            check (::GetClientRect (hwnd, &rect) != 0, "window client rect");
            const int width = rect.right, height = rect.bottom;
            auto dc = ::GetDC (hwnd);
            auto memory = ::CreateCompatibleDC (dc);
            BITMAPINFO info {};
            info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width;
            info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* pixels {};
            auto bitmap = ::CreateDIBSection (dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
            check (bitmap && pixels, "capture bitmap");
            auto previous = ::SelectObject (memory, bitmap);
            // Direct2D content: ask Windows to render the real client area.
            const bool printed = ::PrintWindow (hwnd, memory, 3 /* PW_CLIENTONLY | PW_RENDERFULLCONTENT */) != 0;
            juce::Image snapshot (juce::Image::RGB, width, height, true);
            auto* bytes = static_cast<unsigned char*> (pixels);
            int lit = 0;
            for (int j = 0; j < height; ++j)
                for (int i = 0; i < width; ++i)
                {
                    const int k = (j * width + i) * 4;
                    snapshot.setPixelAt (i, j, juce::Colour (bytes[k + 2], bytes[k + 1], bytes[k]));
                    lit += (bytes[k] + bytes[k + 1] + bytes[k + 2]) > 60 ? 1 : 0;
                }
            const juce::File file { juce::String (argv[2]) };
            file.deleteFile();
            {
                auto stream = file.createOutputStream();
                check (stream != nullptr, "capture output");
                juce::PNGImageFormat png;
                check (png.writeImageToStream (snapshot, *stream), "save the Windows capture");
            }
            ::SelectObject (memory, previous);
            ::DeleteObject (bitmap);
            ::DeleteDC (memory);
            ::ReleaseDC (hwnd, dc);
            std::cout << "Windows editor capture " << width << "x" << height << " PrintWindow=" << printed << " lit_pixels=" << lit << '\n';
            check (lit > width * height / 4, "the editor is drawn (not black or missing)");
        }
#endif
        stop = true;
        audio.join();
        window.clearContentComponent();
        editor.reset();
        for (auto* p : { &a, &b, &c, &m })
            (*p)->releaseResources();
        std::cout << "PASS LABQ8 VST3: identity, 80 parameters, stereo/mono static and dynamic Bell, state, automation, editor with audio; "
                  << checks << " checks\n";
        std::cout << "This is a JUCE test host: NOT Pro Tools, NOT Blue Cat PatchWork, NOT the Steinberg validator.\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL LABQ8 VST3 smoke: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
