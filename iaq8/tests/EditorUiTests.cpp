#include "LabQ8Assets.h"
#include "TestHarness.h"

#include <juce_graphics/juce_graphics.h>

namespace
{
juce::Image decode (const void* data, int size)
{
    return juce::ImageFileFormat::loadFrom (data, static_cast<size_t> (size));
}
}

LQ_TEST ("ui assets: chassis and LABQ8 sprites load at reference size")
{
    const auto chassis = decode (LabQ8Assets::iaq8_chassis_png, LabQ8Assets::iaq8_chassis_pngSize);
    LQ_CHECK (chassis.isValid(), "chassis valid");
    LQ_CHECK (chassis.getWidth() == 1672, "chassis width 1672");
    LQ_CHECK (chassis.getHeight() == 941, "chassis height 941");

    LQ_CHECK (decode (LabQ8Assets::meter_lit_png, LabQ8Assets::meter_lit_pngSize).isValid(), "meter_lit valid");
    LQ_CHECK (decode (LabQ8Assets::mode_face_lit_png, LabQ8Assets::mode_face_lit_pngSize).isValid(), "mode_face_lit valid");
    LQ_CHECK (decode (LabQ8Assets::mode_face_unlit_png, LabQ8Assets::mode_face_unlit_pngSize).isValid(), "mode_face_unlit valid");
}

LQ_TEST ("ui assets: fonts load")
{
    const struct { const char* data; int size; } fonts[] = {
        { LabQ8Assets::VT323Regular_ttf, LabQ8Assets::VT323Regular_ttfSize },
        { LabQ8Assets::BarlowMedium_ttf, LabQ8Assets::BarlowMedium_ttfSize },
        { LabQ8Assets::BarlowSemiBold_ttf, LabQ8Assets::BarlowSemiBold_ttfSize },
        { LabQ8Assets::BarlowSemiCondensedSemiBold_ttf, LabQ8Assets::BarlowSemiCondensedSemiBold_ttfSize } };

    for (const auto& f : fonts)
    {
        const auto tf = juce::Typeface::createSystemTypefaceFor (f.data, static_cast<size_t> (f.size));
        LQ_CHECK (tf != nullptr, "font loads");
    }
}
