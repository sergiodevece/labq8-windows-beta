#include "ui/DynamicsOverlay.h"

#include "ui/UiGeometry.h"

#include <algorithm>
#include <cmath>

namespace labq8::ui
{
namespace g = geometry;

namespace
{
juce::Colour bandColour (int band, float alpha = 1.0f)
{
    const auto& c = g::bandColours[(size_t) band];
    return juce::Colour (c.r, c.g, c.b).withAlpha (alpha);
}

float clampY (float y) { return juce::jlimit (g::plotArea.y + 1.0f, g::plotArea.y + g::plotArea.h - 1.0f, y); }

constexpr double refreshSeconds = 1.0 / 30.0 - 0.004; // display frames are not exactly periodic
constexpr double visibleDb = 0.02;                    // below this a delta is not drawn (0.1 px)
constexpr float fillAlpha = 0.28f, lineAlpha = 0.95f;
constexpr float readoutWidth = 34.0f, readoutHeight = 14.0f, readoutFontHeight = 12.0f;
} // namespace

DynamicsOverlay::DynamicsOverlay (DynamicsTelemetry& t, BandReader bandReader, RateReader rateReader)
    : telemetry (t), readBands (std::move (bandReader)), sampleRate (std::move (rateReader)),
      vblank (this, [this] (double timestamp)
      {
          if (lastFrame < 0.0)
              lastFrame = timestamp;
          if (timestamp - lastFrame >= refreshSeconds)
          {
              advance (timestamp - lastFrame);
              lastFrame = timestamp;
          }
      })
{
    setBounds (juce::Rectangle<float> (g::plotArea.x, g::plotArea.y, g::plotArea.w, g::plotArea.h).toNearestInt());
    setInterceptsMouseClicks (false, false);
    setOpaque (false);
}

DynamicsOverlay::~DynamicsOverlay()
{
    telemetry.setActive (false);
}

void DynamicsOverlay::setShown (bool shouldBeShown, bool withReadouts)
{
    if (shouldBeShown != shown)
    {
        shown = shouldBeShown;
        telemetry.setActive (shown);
        started = false; // whatever the telemetry kept from before is not shown
        clear();
    }
    if (withReadouts != readouts)
    {
        readouts = withReadouts;
        requestRepaint();
    }
}

void DynamicsOverlay::parametersChanged()
{
    staticDirty = true;
    if (showing)
    {
        compute();
        requestRepaint();
    }
}

void DynamicsOverlay::clear()
{
    const auto was = showing;
    showing = false;
    drawn = {};
    for (auto& d : deltas)
        d.clear();
    dynamicDb.clear();
    if (was)
        requestRepaint();
}

void DynamicsOverlay::requestRepaint()
{
    repaint();
    ++repaintRequests;
}

void DynamicsOverlay::advance (double seconds)
{
    if (! shown)
        return;
    DynamicsTelemetry::Snapshot s;
    if (! telemetry.take (s))
        return; // the writer was busy: next frame

    if (! started || s.generation != generation)
    {
        // Showing starts (or a new stream): only audio offered from now on.
        started = true;
        generation = s.generation;
        activationOffered = lastOffered = s.offered;
        sinceAudio = 0.0;
        clear();
        return;
    }
    if (s.offered != lastOffered)
    {
        lastOffered = s.offered;
        sinceAudio = 0.0;
    }
    else
    {
        sinceAudio += seconds;
    }

    // D3: no audio for 100 ms, the host's bypass, or nothing since showing
    // started: no dynamics on the screen.
    const auto live = s.offered != activationOffered && sinceAudio < staleSeconds && ! s.hostBypassed;
    std::array<double, 8> wanted {};
    auto any = false;
    if (live)
        for (int b = 0; b < 8; ++b)
        {
            wanted[(size_t) b] = DynamicsTelemetry::effectiveReductionDb (s.bands[(size_t) b]);
            any = any || wanted[(size_t) b] > 0.0;
        }

    auto changed = any != showing;
    for (int b = 0; b < 8 && ! changed; ++b)
    {
        const auto w = wanted[(size_t) b], d = drawn[(size_t) b];
        const auto& now = s.bands[(size_t) b];
        const auto& before = snapshot.bands[(size_t) b];
        changed = (w > 0.0) != (d > 0.0) || std::abs (w - d) >= repaintStepDb
               || (w > 0.0 && (std::abs (now.frequencyHz - before.frequencyHz) > 1.0e-3f * before.frequencyHz
                               || std::abs (now.q - before.q) > 1.0e-3f * before.q
                               || std::abs (now.staticGainDb - before.staticGainDb) >= (float) repaintStepDb));
    }
    if (! changed)
        return;

    if (! any)
    {
        clear();
        return;
    }
    snapshot = s;
    drawn = wanted;
    showing = true;
    compute();
    requestRepaint();
}

void DynamicsOverlay::compute()
{
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto fs = sampleRate();
    if (frequencies.empty() || ! juce::exactlyEqual (fs, rate))
    {
        rate = fs;
        frequencies.clear();
        // The screen's points (one every 1.5 reference px), below Nyquist.
        for (float x = g::plotArea.x; x <= g::plotArea.x + g::plotArea.w; x += 1.5f)
        {
            const auto f = xToFrequency (x);
            if (f < 0.497 * fs)
                frequencies.push_back (f);
        }
        staticDirty = true;
        for (auto& w : withoutDb)
            w.clear();
    }
    if (staticDirty)
    {
        staticDb = cascadeResponseDb (readBands(), fs, frequencies);
        staticDirty = false;
    }

    dynamicDb = staticDb;
    for (int b = 0; b < 8; ++b)
    {
        auto& delta = deltas[(size_t) b];
        const auto r = drawn[(size_t) b];
        if (! (r > 0.0))
        {
            delta.clear();
            continue;
        }
        // The band as the DSP has it, without and with its reduction.
        const auto& v = snapshot.bands[(size_t) b];
        const BandView without { v.frequencyHz, v.staticGainDb, v.q, dsp::FilterType::bell, false };
        auto with = without;
        with.gainDb = v.staticGainDb - r;
        const std::array<float, 3> key { v.staticGainDb, v.frequencyHz, v.q };
        auto& a = withoutDb[(size_t) b];
        if (a.empty() || key != withoutKey[(size_t) b])
        {
            a = bandResponseDb (without, fs, frequencies);
            withoutKey[(size_t) b] = key;
        }
        const auto d = bandResponseDb (with, fs, frequencies);
        delta.resize (frequencies.size());
        for (size_t i = 0; i < frequencies.size(); ++i)
        {
            delta[i] = d[i] - a[i];
            dynamicDb[i] += delta[i];
        }
    }
    lastComputeMs = juce::Time::getMillisecondCounterHiRes() - start;
}

juce::String DynamicsOverlay::readoutText (int band) const
{
    return juce::String::fromUTF8 ("\xe2\x88\x92") + juce::String (drawn[(size_t) band], 1);
}

juce::Rectangle<float> DynamicsOverlay::readoutBounds (int band) const
{
    const auto& v = snapshot.bands[(size_t) band];
    const auto nearest = (size_t) std::distance (frequencies.begin(),
        std::min_element (frequencies.begin(), frequencies.end(),
                          [f = (double) v.frequencyHz] (double a, double b) { return std::abs (std::log (a / f)) < std::abs (std::log (b / f)); }));
    const auto x = juce::jlimit (g::plotArea.x + readoutWidth / 2.0f, g::plotArea.x + g::plotArea.w - readoutWidth / 2.0f, frequencyToX (v.frequencyHz));
    const auto node = decibelsToY (staticDb[nearest]);
    const auto dyn = clampY (decibelsToY (dynamicDb[nearest]));
    // Under the dynamic point, clear of the node; above the node if the plot ends.
    auto top = std::max (dyn, node + g::nodeDiameter / 2.0f) + 3.0f;
    if (top + readoutHeight > g::plotArea.y + g::plotArea.h - 2.0f)
        top = std::min (dyn, node - g::nodeDiameter / 2.0f) - 3.0f - readoutHeight;
    return { x - readoutWidth / 2.0f, top, readoutWidth, readoutHeight };
}

std::vector<juce::Rectangle<float>> DynamicsOverlay::getReadoutBounds() const
{
    std::vector<juce::Rectangle<float>> result;
    if (showing && readouts)
        for (int b = 0; b < 8; ++b)
            if (drawn[(size_t) b] >= 0.05)
                result.push_back (readoutBounds (b));
    return result;
}

void DynamicsOverlay::paint (juce::Graphics& gr)
{
    ++paintCount;
    if (! showing || frequencies.empty())
        return;
    gr.addTransform (juce::AffineTransform::translation (-(float) getX(), -(float) getY()));
    gr.reduceClipRegion (juce::Rectangle<float> (g::plotArea.x, g::plotArea.y, g::plotArea.w, g::plotArea.h).toNearestInt());
    const auto n = frequencies.size();
    std::vector<float> xs (n);
    for (size_t i = 0; i < n; ++i)
        xs[i] = frequencyToX (frequencies[i]);

    // Fills: band by band, stacked from the static curve, so overlapping
    // bands add up to the dynamic curve exactly.
    std::vector<double> lower = staticDb;
    for (int b = 0; b < 8; ++b)
    {
        const auto& delta = deltas[(size_t) b];
        if (delta.empty())
            continue;
        size_t first = n, last = 0;
        for (size_t i = 0; i < n; ++i)
            if (std::abs (delta[i]) >= visibleDb)
            {
                first = std::min (first, i);
                last = i;
            }
        if (first < n)
        {
            juce::Path area;
            area.startNewSubPath (xs[first], clampY (decibelsToY (lower[first] + delta[first])));
            for (size_t i = first + 1; i <= last; ++i)
                area.lineTo (xs[i], clampY (decibelsToY (lower[i] + delta[i])));
            for (size_t i = last + 1; i-- > first;)
                area.lineTo (xs[i], clampY (decibelsToY (lower[i])));
            area.closeSubPath();
            gr.setColour (bandColour (b, fillAlpha));
            gr.fillPath (area);
        }
        for (size_t i = 0; i < n; ++i)
            lower[i] += delta[i];
    }

    // The dynamic curve, dotted, each stretch in the colour of the band that
    // moves it most there; only where it leaves the static curve.
    std::array<juce::Path, 8> lines;
    int previous = -1;
    for (size_t i = 0; i < n; ++i)
    {
        int dominant = -1;
        double largest = 0.0;
        for (int b = 0; b < 8; ++b)
            if (! deltas[(size_t) b].empty() && std::abs (deltas[(size_t) b][i]) > largest)
            {
                largest = std::abs (deltas[(size_t) b][i]);
                dominant = b;
            }
        if (std::abs (dynamicDb[i] - staticDb[i]) < visibleDb)
            dominant = -1;
        const auto y = clampY (decibelsToY (dynamicDb[i]));
        if (dominant >= 0)
        {
            auto& path = lines[(size_t) dominant];
            if (dominant != previous)
            {
                // Join to the previous point so a change of colour leaves no gap.
                if (previous >= 0 && i > 0)
                    path.startNewSubPath (xs[i - 1], clampY (decibelsToY (dynamicDb[i - 1])));
                else
                    path.startNewSubPath (xs[i], y);
            }
            path.lineTo (xs[i], y);
        }
        previous = dominant;
    }
    const float dashes[] = { 4.0f, 3.0f };
    for (int b = 0; b < 8; ++b)
        if (! lines[(size_t) b].isEmpty())
        {
            juce::Path dashed;
            juce::PathStrokeType (1.6f, juce::PathStrokeType::beveled, juce::PathStrokeType::butt)
                .createDashedStroke (dashed, lines[(size_t) b], dashes, 2);
            gr.setColour (bandColour (b, lineAlpha));
            gr.fillPath (dashed);
        }

    if (readouts)
    {
        gr.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", readoutFontHeight, 0).withStyle ("Bold")));
        for (int b = 0; b < 8; ++b)
            if (drawn[(size_t) b] >= 0.05)
            {
                gr.setColour (bandColour (b));
                gr.drawText (readoutText (b), readoutBounds (b), juce::Justification::centred, false);
            }
    }
}
} // namespace labq8::ui
