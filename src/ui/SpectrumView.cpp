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

bool SpectrumView::advance (Trace& trace, double seconds)
{
    if (! trace.active)
        return false;

    auto copy = [&] (SpectrumTap::Frame& frame)
    {
        std::vector<float*> dest;
        for (auto& channel : trace.samples)
            dest.push_back (channel.data());
        return trace.tap.copyLatest (dest.data(), (int) dest.size(), trace.analyzer.getSize(), trace.from, frame);
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

    const auto newAudio = frame.end != trace.lastEnd && frame.sampleRate > 0.0;
    trace.lastEnd = frame.end;
    if (! newAudio)
        return trace.ballistics.update (nullptr, frequencies.size(), seconds);

    const auto start = juce::Time::getMillisecondCounterHiRes();
    std::vector<const float*> channels;
    for (int ch = 0; ch < std::max (1, frame.channels); ++ch)
        channels.push_back (trace.samples[(size_t) ch].data());
    trace.analyzer.analyse (channels.data(), (int) channels.size());
    trace.analyzer.toDisplay (trace.display);
    lastAnalysisMs = juce::Time::getMillisecondCounterHiRes() - start;
    return trace.ballistics.update (&trace.display, frequencies.size(), seconds);
}

void SpectrumView::advance (double seconds)
{
    const auto movedPre = advance (pre, seconds);
    const auto movedPost = advance (post, seconds);
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
