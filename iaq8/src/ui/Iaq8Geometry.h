#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>

// Reference geometry of the IAQ8 editor: the 1672 x 941 px space of the
// approved mockup (docs/superpowers/specs/iaq8-mockup-v2/index.html), whose
// elements carry inline left / top / width / height in these pixels. Every
// rectangle below is copied from there; where the mockup gives no size (text
// labels) the height is the CSS line box (font-size x 1.2, or the stated
// line-height) and the width a box that holds the text.
//
// The editor lays everything out in this space and scales it as a whole.
// The IN / OUT meters use LABQ8's labq8::ui::geometry::meterColumns
// (src/ui/UiGeometry.h), since the chassis keeps LABQ8's meter window.

namespace iaq8::ui::geometry
{
inline constexpr int referenceWidth = 1672;
inline constexpr int referenceHeight = 941;

using Rect = juce::Rectangle<float>;

//==============================================================================
// Settings: the panel's top-left screw and the "IAQ8 VitaPlug" logo (both
// printed on the chassis) open the settings menu.
inline const Rect settingsScrew { 94.0f, 40.0f, 32.0f, 32.0f };
inline const Rect logo { 116.0f, 104.0f, 214.0f, 86.0f };

// Descriptor text under the logo (.desc1: two lines at line-height 1.38).
inline const Rect desc1 { 122.0f, 206.0f, 200.0f, 46.0f };
inline const Rect desc2 { 122.0f, 262.0f, 210.0f, 14.0f };
inline const Rect desc3 { 122.0f, 286.0f, 210.0f, 13.0f };

//==============================================================================
// Top row: SOURCE, COMPARE and STATUS.
inline const Rect sourceLabel { 462.0f, 60.0f, 90.0f, 20.0f };
inline const Rect sourceTray { 456.0f, 82.0f, 310.0f, 58.0f };
inline const Rect sourceDrop { 464.0f, 89.0f, 294.0f, 44.0f };

inline const Rect compareLabel { 826.0f, 60.0f, 100.0f, 20.0f };
inline const Rect compareTray { 818.0f, 82.0f, 262.0f, 58.0f };
inline const Rect faceAntes { 825.0f, 89.0f, 120.0f, 44.0f };
inline const Rect faceDespues { 950.0f, 89.0f, 124.0f, 44.0f };

inline const Rect statusLabel { 1132.0f, 60.0f, 90.0f, 20.0f };
// ONLINE, LISTEN, PROPOSAL: 17 px lamps; captions 90 px wide, centred under
// them (margin-left -36, margin-top 8 from top 116).
inline const std::array<Rect, 3> lamps { { { 1146.0f, 96.0f, 17.0f, 17.0f },
                                           { 1216.0f, 96.0f, 17.0f, 17.0f },
                                           { 1286.0f, 96.0f, 17.0f, 17.0f } } };
inline const std::array<Rect, 3> lampCaps { { { 1110.0f, 124.0f, 90.0f, 14.0f },
                                              { 1180.0f, 124.0f, 90.0f, 14.0f },
                                              { 1250.0f, 124.0f, 90.0f, 14.0f } } };

//==============================================================================
// Phosphor screen (CRT), corner radius 22.
inline const Rect crt { 346.0f, 193.0f, 980.0f, 314.0f };
inline constexpr float crtCornerRadius = 22.0f;

// The eight key slots under the screen (LABQ8's band keys): quick replies.
inline const std::array<Rect, 8> quickKeys { { { 365.0f, 540.0f, 99.0f, 40.0f },
                                               { 486.0f, 540.0f, 99.0f, 40.0f },
                                               { 606.0f, 540.0f, 99.0f, 40.0f },
                                               { 727.0f, 540.0f, 99.0f, 40.0f },
                                               { 847.0f, 540.0f, 99.0f, 40.0f },
                                               { 968.0f, 540.0f, 99.0f, 40.0f },
                                               { 1088.0f, 540.0f, 99.0f, 40.0f },
                                               { 1208.0f, 540.0f, 99.0f, 40.0f } } };

//==============================================================================
// Lower panel. Head: orange lamp + "ASSISTANT" (.head, flex, gap 14), then
// "Pídelo con tus palabras" and a rule.
inline const Rect assistantHead { 132.0f, 626.0f, 180.0f, 27.0f };
inline const Rect assistantNote { 345.0f, 632.0f, 200.0f, 20.0f };
inline const Rect assistantRule { 560.0f, 645.0f, 560.0f, 1.0f };

inline const Rect inbox { 132.0f, 684.0f, 990.0f, 72.0f };
inline const Rect hint { 134.0f, 765.0f, 620.0f, 16.0f };

// SOURCE, LISTENED, CHANGES, LEVEL MATCH.
inline const std::array<Rect, 4> valueLabels { { { 132.0f, 800.0f, 200.0f, 16.0f },
                                                 { 352.0f, 800.0f, 200.0f, 16.0f },
                                                 { 572.0f, 800.0f, 200.0f, 16.0f },
                                                 { 792.0f, 800.0f, 200.0f, 16.0f } } };
inline const std::array<Rect, 4> valueBoxes { { { 132.0f, 824.0f, 200.0f, 40.0f },
                                                { 352.0f, 824.0f, 200.0f, 40.0f },
                                                { 572.0f, 824.0f, 200.0f, 40.0f },
                                                { 792.0f, 824.0f, 200.0f, 40.0f } } };

inline const Rect keyListen { 1160.0f, 686.0f, 170.0f, 70.0f };
inline const Rect keySend { 1352.0f, 686.0f, 186.0f, 70.0f };
inline const Rect keyUndo { 1160.0f, 796.0f, 170.0f, 72.0f };
inline const Rect keyApply { 1352.0f, 796.0f, 186.0f, 72.0f };
} // namespace iaq8::ui::geometry
