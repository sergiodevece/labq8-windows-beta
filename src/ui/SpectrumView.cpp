#include "ui/SpectrumView.h"

#include "ui/UiGeometry.h"
#include "ui/UiModel.h"

#include <algorithm>

namespace labq8::ui
{
namespace g = geometry;

namespace
{
juce::Colour colourOf (g::Rgb c, float alpha = 1.0f) { return juce::Colour (c.r, c.g, c.b).withAlpha (alpha); }

// PRE when shown with POST: a light warm outline, clearly not green.
const juce::Colour preOutline { 236, 222, 188 };

constexpr double refreshSeconds = 1.0 / 30.0 - 0.004; // display frames are not exactly periodic
} // namespace

SpectrumView::SpectrumView (SpectrumTap& preTap, SpectrumTap& postTap)
    : pre (preTap), post (postTap),
      vblank (this, [this] (double timestamp)
      {
          // About 30 frames per second: the drawing, not the analysis, is the
          // cost (measured), and a spectrum reads well at this rate.
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
    // The same points as the EQ curve: one every 1.5 reference pixels.
    for (float x = g::plotArea.x; x <= g::plotArea.x + g::plotArea.w; x += 1.5f)
        frequencies.push_back (xToFrequency (x));
    updateActivity();
}

SpectrumView::~SpectrumView()
{
    pre.tap.setActive (false);
    post.tap.setActive (false);
}

void SpectrumView::setShown (bool shouldBeShown)
{
    shown = shouldBeShown;
    updateActivity();
}

void SpectrumView::setSource (AnalyzerSource newSource)
{
    source = newSource;
    updateActivity();
}

void SpectrumView::setDomain (AnalyzerDomain newDomain)
{
    if (newDomain == domain)
        return;
    domain = newDomain;
    // Levels of another signal are not carried over.
    for (auto* trace : { &pre, &post })
        trace->ballistics.reset (frequencies.size());
    repaint();
    ++repaintRequests;
}

void SpectrumView::updateActivity()
{
    for (auto* trace : { &pre, &post })
    {
        const auto wanted = shown && (source == AnalyzerSource::both || (source == AnalyzerSource::pre) == (trace == &pre));
        if (wanted == trace->active)
            continue;
        trace->active = wanted;
        trace->tap.setActive (wanted);
        // Whatever the tap held from an earlier time is not shown.
        trace->from = trace->lastEnd = trace->tap.getWritten();
        trace->ballistics.reset (frequencies.size());
    }
    repaint();
    ++repaintRequests;
}

bool SpectrumView::isAtRest() const noexcept
{
    return (! pre.active || pre.ballistics.atRest()) && (! post.active || post.ballistics.atRest());
}

bool SpectrumView::advance (Trace& trace, double seconds, const std::uint64_t* alignTo)
{
    trace.copiedThisFrame = false;
    if (! trace.active)
        return false;

    auto copy = [&] (SpectrumTap::Frame& frame)
    {
        std::vector<float*> dest;
        for (auto& channel : trace.samples)
            dest.push_back (channel.data());
        const auto n = trace.analyzer.getSize();
        // L/R: exactly the UI-2b read. MID / SIDE: with the stream position,
        // and PRE at POST's (alignTo).
        if (domain == AnalyzerDomain::lr)
            return trace.tap.copyLatest (dest.data(), (int) dest.size(), n, trace.from, frame);
        if (alignTo != nullptr)
            return trace.tap.copyAtStream (dest.data(), (int) dest.size(), n, trace.from, *alignTo, frame);
        return trace.tap.copyLatestWithStream (dest.data(), (int) dest.size(), n, trace.from, frame);
    };

    if (! trace.prepared)
    {
        trace.analyzer.prepare (48000.0, frequencies);
        trace.samples.assign (SpectrumTap::maxChannels, std::vector<float> ((size_t) trace.analyzer.getSize()));
        trace.prepared = true;
    }
    SpectrumTap::Frame frame;
    auto coherent = copy (frame);
    if (frame.sampleRate > 0.0 && (frame.generation != trace.generation || ! juce::exactlyEqual (frame.sampleRate, trace.analyzer.getSampleRate())))
    {
        // A new stream (or a new rate): analyse at its rate, from its start.
        trace.generation = frame.generation;
        trace.analyzer.prepare (frame.sampleRate, frequencies);
        trace.samples.assign (SpectrumTap::maxChannels, std::vector<float> ((size_t) trace.analyzer.getSize()));
        trace.ballistics.reset (frequencies.size());
        trace.lastEnd = frame.start;
        coherent = copy (frame);
    }
    if (! coherent)
        return false; // overwritten while copying: the next frame tries again

    trace.copiedThisFrame = true;
    trace.streamEnd = frame.streamEnd;
    const auto newAudio = frame.end != trace.lastEnd && frame.sampleRate > 0.0;
    trace.lastEnd = frame.end;
    if (! newAudio)
        return trace.ballistics.update (nullptr, frequencies.size(), seconds);

    const auto start = juce::Time::getMillisecondCounterHiRes();
    if (domain == AnalyzerDomain::lr)
    {
        std::vector<const float*> channels;
        for (int ch = 0; ch < std::max (1, frame.channels); ++ch)
            channels.push_back (trace.samples[(size_t) ch].data());
        trace.analyzer.analyse (channels.data(), (int) channels.size());
    }
    else
    {
        // E4d: the component from the time samples, then one FFT. A mono
        // bus: MID is the channel, SIDE is zero (nothing is drawn).
        const auto n = (size_t) trace.analyzer.getSize();
        trace.component.resize (n);
        const auto& l = trace.samples[0];
        const auto& r = trace.samples[frame.channels >= 2 ? 1 : 0];
        for (size_t i = 0; i < n; ++i)
        {
            if (frame.channels < 2)
                trace.component[i] = domain == AnalyzerDomain::mid ? l[i] : 0.0f;
            else
            {
                const auto a = (double) l[i], b = (double) r[i];
                trace.component[i] = (float) (domain == AnalyzerDomain::mid ? 0.5 * (a + b) : 0.5 * (a - b));
            }
        }
        const float* one[1] = { trace.component.data() };
        trace.analyzer.analyse (one, 1);
    }
    trace.analyzer.toDisplay (trace.display);
    lastAnalysisMs = juce::Time::getMillisecondCounterHiRes() - start;
    return trace.ballistics.update (&trace.display, frequencies.size(), seconds);
}

void SpectrumView::advance (double seconds)
{
    if (domain != AnalyzerDomain::lr && pre.active && post.active)
    {
        // E4d (D3): POST first, then PRE ending at the same stream position.
        const auto movedPost = advance (post, seconds, nullptr);
        const auto streamEnd = post.streamEnd;
        const auto movedPre = post.copiedThisFrame ? advance (pre, seconds, &streamEnd)
                                                   : pre.ballistics.update (nullptr, frequencies.size(), seconds);
        if (movedPre || movedPost)
        {
            repaint();
            ++repaintRequests;
        }
        return;
    }
    const auto movedPre = advance (pre, seconds, nullptr);
    const auto movedPost = advance (post, seconds, nullptr);
    if (movedPre || movedPost)
    {
        repaint();
        ++repaintRequests;
    }
}

void SpectrumView::paintTrace (juce::Graphics& gr, const Trace& trace, bool filled, juce::Colour line) const
{
    if (! trace.active || trace.ballistics.atRest())
        return;
    const auto bottom = g::plotArea.y + g::plotArea.h;
    // Up to the last point below Nyquist (at 44.1 / 48 kHz the plot's right
    // end lies above it): the trace ends there instead of dropping.
    const auto nyquist = 0.497 * trace.analyzer.getSampleRate();
    juce::Path outline;
    float lastX = 0.0f;
    for (size_t i = 0; i < frequencies.size() && frequencies[i] < nyquist; ++i)
    {
        lastX = frequencyToX (frequencies[i]);
        const auto y = spectrumDbToY (trace.ballistics.levelDb[i]);
        if (i == 0)
            outline.startNewSubPath (lastX, y);
        else
            outline.lineTo (lastX, y);
    }
    if (outline.isEmpty())
        return;
    if (filled)
    {
        auto area = outline;
        area.lineTo (lastX, bottom);
        area.lineTo (frequencyToX (frequencies.front()), bottom);
        area.closeSubPath();
        // A solid fill: a gradient costs about 30 % more per frame.
        gr.setColour (colourOf (g::spectrumFillColour, 0.22f));
        gr.fillPath (area);
    }
    gr.setColour (line);
    gr.strokePath (outline, juce::PathStrokeType (1.2f, juce::PathStrokeType::beveled, juce::PathStrokeType::butt));
}

void SpectrumView::paint (juce::Graphics& gr)
{
    gr.addTransform (juce::AffineTransform::translation (-(float) getX(), -(float) getY()));
    gr.reduceClipRegion (juce::Rectangle<float> (g::plotArea.x, g::plotArea.y, g::plotArea.w, g::plotArea.h).toNearestInt());
    const auto green = colourOf (g::spectrumLineColour, 0.9f);
    if (source == AnalyzerSource::both)
    {
        paintTrace (gr, post, true, green);
        paintTrace (gr, pre, false, preOutline.withAlpha (0.85f));
    }
    else
    {
        paintTrace (gr, pre, true, green);
        paintTrace (gr, post, true, green);
    }
}
} // namespace labq8::ui
