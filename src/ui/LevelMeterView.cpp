#include "ui/LevelMeterView.h"

#include "LabQ8Assets.h"
#include "ui/UiGeometry.h"

#include <algorithm>
#include <cmath>

namespace labq8::ui
{
namespace g = geometry;

namespace
{
// Hues of the over-level cells (the master only has green ones). Their light
// is the master's green light with the hue changed: same brightness, same
// glow, same saturation.
constexpr float amberHue = 36.0f / 360.0f, redHue = 2.0f / 360.0f;

// Lit cells of the master used for the cells it never shows lit, by height.
constexpr int templateCell6Rows = 12, templateCell7Rows = 14; // (337-342), (359-365)
} // namespace

LevelMeterView::LevelMeterView (LevelMeter& in, LevelMeter& out)
    : input (in), output (out),
      vblank (this, [this] (double timestamp)
      {
          advance (lastFrame < 0.0 ? 0.0 : timestamp - lastFrame);
          lastFrame = timestamp;
      })
{
    setBounds (juce::Rectangle<float> (g::meterArea.x, g::meterArea.y, g::meterArea.w, g::meterArea.h).toNearestInt());
    setInterceptsMouseClicks (false, false);
    setOpaque (false);
    buildCells();

    // Whatever was measured while no editor was open is not shown.
    for (int ch = 0; ch < LevelMeter::maxChannels; ++ch)
    {
        input.takePeak (ch);
        output.takePeak (ch);
    }
}

juce::Rectangle<int> LevelMeterView::slot (int column, int t) const
{
    // A cell with its glow: from the middle of the gap above to the middle
    // of the gap below (the master's lit area at the ends: rows 304 and 475
    // carry glow), the column widened by the glow up to the middle between
    // the L and R columns of a pair.
    const auto& cell = g::meterCells[(size_t) t];
    const auto firstMasterLit = g::meterZeroCell + 1;
    const auto top = t == 0 ? (int) cell.top - 2
                   : t == firstMasterLit ? (int) g::meterLitTop
                   : (int) ((g::meterCells[(size_t) t - 1].bottom + cell.top + 1.0f) / 2.0f);
    const auto bottom = t == g::numMeterCells - 1 ? (int) g::meterLitBottom + 1
                      : t == g::meterZeroCell ? (int) g::meterLitTop
                      : (int) ((cell.bottom + g::meterCells[(size_t) t + 1].top + 1.0f) / 2.0f);
    const auto& span = g::meterColumns[(size_t) column];
    const auto& partner = g::meterColumns[(size_t) (column ^ 1)];
    const auto middle = (int) ((span.left < partner.left ? span.right + partner.left : partner.right + span.left) / 2.0f);
    const auto left = column % 2 == 0 ? (int) (span.left - g::meterGlowWidth) : middle;
    const auto right = column % 2 == 0 ? middle - 1 : (int) (span.right + g::meterGlowWidth);
    return { left, top, right - left + 1, bottom - top };
}

void LevelMeterView::buildCells()
{
    const auto background = juce::ImageCache::getFromMemory (LabQ8Assets::background_png, LabQ8Assets::background_pngSize);
    const auto litSprite = juce::ImageCache::getFromMemory (LabQ8Assets::meter_lit_png, LabQ8Assets::meter_lit_pngSize);
    const auto spriteX = (int) g::meterLitSprite.x, spriteY = (int) g::meterLitSprite.y;
    const auto firstMasterLit = g::meterZeroCell + 1;

    for (int column = 0; column < numColumns; ++column)
        for (int t = 0; t < g::numMeterCells; ++t)
        {
            const auto area = slot (column, t);
            juce::Image image (juce::Image::RGB, area.getWidth(), area.getHeight(), false);
            if (t >= firstMasterLit)
            {
                // Lit in the master: its own pixels.
                juce::Graphics (image).drawImageAt (litSprite, spriteX - area.getX(), spriteY - area.getY());
            }
            else
            {
                // Never lit in the master: the light of a lit cell of the same
                // height (lit minus unlit background), aligned on the cell's
                // top, re-tinted, over this cell's unlit background.
                const auto& cell = g::meterCells[(size_t) t];
                const auto& source = g::meterCells[(size_t) ((cell.bottom - cell.top) >= 6.0f ? templateCell7Rows : templateCell6Rows)];
                const auto colour = meterCellColour (g::numMeterCells - 1 - t);
                for (int y = 0; y < area.getHeight(); ++y)
                    for (int x = 0; x < area.getWidth(); ++x)
                    {
                        const auto px = area.getX() + x;
                        const auto sy = (int) source.top + (area.getY() + y - (int) cell.top);
                        const auto litPixel = litSprite.getPixelAt (px - spriteX, sy - spriteY);
                        const auto unlit = background.getPixelAt (px, sy);
                        auto light = juce::Colour::fromFloatRGBA (std::max (0.0f, litPixel.getFloatRed() - unlit.getFloatRed()),
                                                                  std::max (0.0f, litPixel.getFloatGreen() - unlit.getFloatGreen()),
                                                                  std::max (0.0f, litPixel.getFloatBlue() - unlit.getFloatBlue()), 1.0f);
                        if (colour != MeterColour::green)
                            light = light.withHue (colour == MeterColour::amber ? amberHue : redHue);
                        const auto base = background.getPixelAt (px, area.getY() + y);
                        image.setPixelAt (x, y, juce::Colour::fromFloatRGBA (std::min (1.0f, base.getFloatRed() + light.getFloatRed()),
                                                                             std::min (1.0f, base.getFloatGreen() + light.getFloatGreen()),
                                                                             std::min (1.0f, base.getFloatBlue() + light.getFloatBlue()), 1.0f));
                    }
            }
            cells[(size_t) column][(size_t) t] = image;
        }
}

void LevelMeterView::advance (double seconds)
{
    std::array<float, numColumns> peaks {};
    int column = 0;
    for (auto* meter : { &input, &output })
    {
        const auto left = meter->takePeak (0);
        // Mono: its one channel in both columns (nothing else is measured).
        const auto right = meter->getNumChannels() == 1 ? left : meter->takePeak (1);
        peaks[(size_t) column++] = left;
        peaks[(size_t) column++] = right;
    }

    for (size_t c = 0; c < (size_t) numColumns; ++c)
    {
        const auto cellsLit = meterLitCells (ballistics[c].update (peaks[c], seconds));
        if (cellsLit != lit[c])
        {
            lit[c] = cellsLit;
            const auto first = slot ((int) c, 0), last = slot ((int) c, g::numMeterCells - 1);
            repaint (juce::Rectangle<int> (first.getX(), first.getY(), first.getWidth(), last.getBottom() - first.getY())
                         - getPosition());
            ++repaintRequests;
        }
    }
}

void LevelMeterView::paint (juce::Graphics& gr)
{
    gr.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    for (int column = 0; column < numColumns; ++column)
        for (int k = 0; k < lit[(size_t) column]; ++k)
        {
            const auto t = g::numMeterCells - 1 - k;
            const auto area = slot (column, t) - getPosition();
            gr.drawImageAt (cells[(size_t) column][(size_t) t], area.getX(), area.getY());
        }
}
} // namespace labq8::ui
