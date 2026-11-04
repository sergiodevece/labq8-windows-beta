#pragma once

#include <array>

// Reference geometry of the approved design LABQ8_UI_DESIGN_APPROVED_01
// (docs/design/, 1672 x 941 px). Every position here was measured on the
// master PNG (procedure in docs/UI_IMPLEMENTATION.md). The asset generator
// and the editor both use this file, so what is erased from the background
// and what the editor draws back always sit in the same place.
//
// Units: pixels of the master image; angles in degrees, clockwise from 12
// o'clock. The editor lays everything out in this space and scales it.

namespace labq8::ui::geometry
{
inline constexpr int referenceWidth  = 1672;
inline constexpr int referenceHeight = 941;

struct Point { float x, y; };
struct Rect { float x, y, w, h; };

//==============================================================================
// Knobs of the shared band panel, left to right:
// Frequency, Gain, Q | Threshold, Range, Attack, Release.
inline constexpr int numKnobs = 7;
inline constexpr std::array<Point, numKnobs> knobCentres { {
    { 205.0f, 762.0f }, { 397.0f, 762.0f }, { 593.0f, 762.0f },
    { 830.5f, 762.0f }, { 1031.0f, 762.0f }, { 1235.5f, 762.0f }, { 1450.5f, 762.0f },
} };
inline constexpr float knobCapRadius = 43.5f;    // metal cap incl. bevel
inline constexpr float knobArcRadius = 50.5f;    // green value arc (core 48.8 .. 52.2)
inline constexpr float knobTickRadius = 55.0f;   // first pixel of the tick ring
inline constexpr float knobStartAngle = 222.0f;  // minimum (7:24)
inline constexpr float knobSweep = 276.0f;       // to 138 deg (4:36)
inline constexpr float pointerInnerRadius = 21.0f;
inline constexpr float pointerOuterRadius = 38.0f;
inline constexpr float knobHitRadius = 58.0f;    // drag area (cap + ring)

// Pointer angles printed in the master (erased by the generator). The
// Release knob of the master shows two pointers (12 o'clock and ~49 deg);
// -1 = none.
inline constexpr std::array<std::array<float, 2>, numKnobs> masterPointerAngles { {
    { 47.25f, -1.0f }, { 1.0f, -1.0f }, { 46.75f, -1.0f }, { 46.75f, -1.0f }, { 1.75f, -1.0f }, { 2.25f, -1.0f }, { 1.5f, 49.0f },
} };

//==============================================================================
// Numeric displays under the knobs (border pixels, inclusive).
struct Span { float left, right; };
inline constexpr std::array<Span, numKnobs> displaySpans { {
    { 152, 261 }, { 344, 450 }, { 546, 643 }, { 776, 886 }, { 982, 1085 }, { 1184, 1290 }, { 1398, 1506 },
} };
inline constexpr float displayTop = 837.0f, displayBottom = 872.0f;
inline constexpr float displayTextCentreY = 854.5f;
inline constexpr float displayTextCapHeight = 12.0f; // "820 Hz": rows 848..860

// Scale labels under the knobs that the master prints with illustrative
// ranges. UI-1 (approved 29-10-2026) re-labels them in the derived
// background with the real public ranges, centred where the old ink was and
// in the same colour and size. Old ink boxes measured on the master.
struct ScaleLabel { Rect oldInk; const char* text; bool rightAligned = false; };
inline constexpr std::array<ScaleLabel, 10> scaleLabels { {
    { { 330, 814, 23, 12 }, "\xe2\x88\x92" "24" }, { { 438, 815, 24, 11 }, "+24" },    // GAIN
    { { 529, 814, 19, 12 }, "0.1" },                    { { 641, 815, 15, 11 }, "18" },     // Q
    { { 760, 814, 25, 12 }, "\xe2\x88\x92" "80" }, { { 879, 814, 7, 12 }, "+24" },     // THRESHOLD
    { { 1168, 814, 20, 12 }, "0.5" },                   { { 1285, 814, 25, 12 }, "500" },   // ATTACK
    { { 1389, 815, 15, 11 }, "5" },                     { { 1495, 814, 34, 12 }, "5000" },  // RELEASE
} };
inline constexpr float scaleLabelInkHeight = 12.0f; // digits, rows 814..825

// Screen dB axis: +-24 dB approved 29-10-2026 (same seven grid lines, 8 dB
// apart). Labels right-aligned on the master's ink (x 391); "0" is kept.
inline constexpr std::array<ScaleLabel, 6> screenAxisLabels { {
    { { 368, 248, 24, 12 }, "+24", true }, { { 368, 284, 24, 11 }, "+16", true }, { { 377, 320, 15, 11 }, "+8", true },
    { { 377, 394, 15, 12 }, "\xe2\x88\x92" "8", true }, { { 369, 432, 23, 11 }, "\xe2\x88\x92" "16", true },
    { { 369, 466, 23, 11 }, "\xe2\x88\x92" "24", true },
} };

//==============================================================================
// Band header of the shared panel: "o BAND 4".
inline constexpr Point bandDotCentre { 144.0f, 637.0f };
inline constexpr float bandDotRadius = 9.5f;
inline constexpr float bandDotHitSize = 34.0f;  // invisible bypass click area (approved)
inline constexpr float bandLabelLeft = 171.0f;
inline constexpr float bandLabelCapTop = 630.0f, bandLabelCapBottom = 644.0f;
inline constexpr Rect bandLabelEraseArea { 131.0f, 624.0f, 122.0f, 27.0f };

// Filter Type selector box (border) and its content (icon + name).
inline constexpr Rect filterTypeBox { 444.0f, 621.0f, 215.0f, 38.0f };
inline constexpr Rect filterTypeIcon { 458.0f, 634.0f, 29.0f, 13.0f };
inline constexpr float filterTypeTextLeft = 499.0f;
inline constexpr float filterTypeCapTop = 635.0f, filterTypeCapBottom = 646.0f;
inline constexpr Rect filterTypeEraseArea { 452.0f, 629.0f, 154.0f, 24.0f }; // arrow (632..643) kept

// Dynamic EQ switch (the pill; "Dynamic EQ" text stays in the background).
inline constexpr Rect dynamicSwitch { 742.0f, 627.0f, 47.0f, 27.0f };
inline constexpr Rect dynamicSwitchSprite { 738.0f, 623.0f, 55.0f, 35.0f };
inline constexpr Rect dynamicSwitchTrack { 742.0f, 627.0f, 47.0f, 27.0f };   // pill outline
inline constexpr float dynamicSwitchTrackRadius = 13.5f;
inline constexpr Rect dynamicSwitchHit { 738.0f, 623.0f, 150.0f, 35.0f }; // pill + its label

//==============================================================================
// Band selector: eight buttons under the screen.
inline constexpr int numBands = 8;
inline constexpr float bandButtonLeft0 = 363.0f;
inline constexpr float bandButtonPitch = 120.43f;
inline constexpr float bandButtonWidth = 102.0f;
inline constexpr float bandButtonTop = 536.0f, bandButtonBottom = 581.0f;
inline constexpr float bandButtonDotOffset = 28.5f;    // dot centre x from the left edge
inline constexpr float bandButtonNumberOffset = 63.5f; // number centre x from the left edge
inline constexpr float bandButtonCentreY = 559.0f;
inline constexpr float bandButtonDotRadius = 6.5f;
inline constexpr float bandButtonNumberCapHeight = 12.0f;

constexpr float bandButtonLeft (int band) { return bandButtonLeft0 + bandButtonPitch * (float) band; }

// Band colours, sampled at the centre of each selector dot.
struct Rgb { unsigned char r, g, b; };
inline constexpr std::array<Rgb, numBands> bandColours { {
    { 143, 209, 244 }, { 145, 247, 125 }, { 250, 233, 129 }, { 242, 121, 78 },
    { 241, 152, 220 }, { 161, 191, 244 }, { 180, 127, 243 }, { 147, 234, 226 },
} };

inline constexpr Rgb displayTextColour { 155, 245, 130 };
inline constexpr Rgb lightTextColour { 222, 224, 216 };

//==============================================================================
// Central screen: frequency-response plot.
inline constexpr Rect plotArea { 407.0f, 247.0f, 905.0f, 230.0f }; // x 407..1312, y 247..477
inline constexpr float plotZeroDbY = 362.0f;
inline constexpr float plotPixelsPerDb = 37.0f / 8.0f; // +-24 dB on the seven grid lines
inline constexpr float plotMaxDb = 24.0f;

// Frequency axis: the printed labels are not on an exact log scale, so the
// response is drawn through the label positions (piecewise log), keeping a
// 1 kHz node on the "1k" line.
struct FrequencyAnchor { float hz, x; };
inline constexpr std::array<FrequencyAnchor, 10> frequencyAnchors { {
    { 20.0f, 407.0f }, { 50.0f, 509.5f }, { 100.0f, 598.5f }, { 200.0f, 687.5f }, { 500.0f, 801.0f },
    { 1000.0f, 896.5f }, { 2000.0f, 986.5f }, { 5000.0f, 1104.0f }, { 10000.0f, 1195.5f }, { 20000.0f, 1284.0f },
} };

// Screen editing (UI-1.1): band nodes and the Q handles of the selected one.
inline constexpr float nodeDiameter = 15.0f;
inline constexpr float nodeHitRadius = 11.0f;
inline constexpr float selectionRingDiameter = 20.0f;
inline constexpr float qHandleWidth = 5.0f, qHandleHeight = 14.0f;
inline constexpr float qHandleHitWidth = 12.0f, qHandleHitHeight = 24.0f;
inline constexpr float qHandleMinDistance = 14.0f;      // from the node centre, so both stay grabbable
inline constexpr float qHandleMaxControlDistance = 48.0f; // shelves / cuts: distance at the minimum Q
inline constexpr float qDragPixelsPerOctave = 45.0f;    // handle drag: Q halves / doubles every 45 px

// Settings icon at the right of the screen header (the zoom menu, UI-1.1).
inline constexpr Point settingsIconCentre { 1281.5f, 215.0f };
inline constexpr float settingsHitSize = 32.0f;

// Horizontal grid rows (re-drawn by the generator after removing the curves).
inline constexpr std::array<float, 7> plotGridRows { 251.0f, 289.0f, 325.5f, 362.0f, 399.5f, 437.0f, 474.0f };

//==============================================================================
// IN / OUT meters: four segmented columns (x range), lit area to clear.
inline constexpr std::array<Span, 4> meterColumns { { { 1395, 1408 }, { 1420, 1433 }, { 1505, 1518 }, { 1530, 1544 } } };
inline constexpr float meterLitTop = 304.0f, meterLitBottom = 475.0f;
inline constexpr float meterTemplateCellRow = 252.0f, meterTemplateGapRow = 248.0f;

// UI-2a: the rows of the 26 cells of every column, top to bottom, measured
// on the master: the 9 cells above the 17 lit ones from their unlit
// pattern (the ninth, y 298, is the "0" cell), the 17 lit ones as lit
// there. The same rows in the four columns.
struct Rows { float top, bottom; };
inline constexpr int numMeterCells = 26, meterZeroCell = 8; // index from the top
inline constexpr std::array<Rows, numMeterCells> meterCells { {
    { 222, 227 }, { 230, 236 }, { 240, 246 }, { 250, 256 }, { 260, 265 }, { 269, 275 }, { 279, 285 }, { 288, 294 }, { 298, 303 },
    { 307, 312 }, { 317, 322 }, { 326, 333 }, { 337, 342 }, { 347, 354 }, { 359, 365 }, { 369, 375 }, { 379, 384 },
    { 388, 394 }, { 398, 405 }, { 409, 416 }, { 421, 426 }, { 431, 436 }, { 440, 445 }, { 449, 454 }, { 458, 463 }, { 468, 472 },
} };
// The printed scale between the columns: x = dB, y = centre row of its label.
inline constexpr std::array<Point, 7> meterScale { { { 12, 224 }, { 6, 262 }, { 0, 299 }, { -6, 339 }, { -12, 380 }, { -24, 431.5f }, { -48, 468 } } };
// A lit cell is drawn with its glow: the column widened by this much.
inline constexpr float meterGlowWidth = 6.0f;
// The master's lit cells (with their glow), cut by the asset generator into
// meter_lit.png; the editor draws its meters only inside meterArea.
inline constexpr Rect meterLitSprite { 1389.0f, 302.0f, 162.0f, 176.0f };
inline constexpr Rect meterArea { 1372.0f, 176.0f, 196.0f, 338.0f };

//==============================================================================
// UI-2b: the screen's header, measured on the master. The view tabs EQ /
// SPECTRUM / DYN / ALL sit between these edges (outer border, dividers);
// the master shows EQ selected (its lit pill spans x 375-439).
inline constexpr int numViewTabs = 4;
inline constexpr std::array<float, numViewTabs + 1> viewTabEdges { 372.0f, 443.0f, 539.0f, 611.0f, 684.0f };
inline constexpr float viewTabTop = 202.0f, viewTabBottom = 229.0f, viewTabCapCentre = 216.0f;
// The light the lit pill adds to an empty tab (asset viewtab_light.png): the
// EQ tab's area with the pill's halo, from x = edge - 6 to the next edge + 4,
// rows 188-246 (the halo reaches the plot's top border).
inline constexpr Rect viewTabLightSprite { 366.0f, 188.0f, 82.0f, 59.0f };
inline constexpr float viewTabLeftCap = 20.0f, viewTabRightCap = 17.0f, viewTabMiddleColumn = 41.0f; // in the sprite
inline constexpr Rgb viewTabTextColour { 146, 200, 128 }, viewTabSelectedTextColour { 1, 3, 1 };
// The analyzer selector ("Analyzer: Post" in the master); its value is drawn
// by the editor, the box and its arrow are the background's.
inline constexpr Rect analyzerBox { 951.0f, 202.0f, 135.0f, 28.0f };
inline constexpr float analyzerTextLeft = 958.0f, analyzerTextRight = 1066.0f, analyzerCapCentre = 215.5f;
inline constexpr Rgb analyzerTextColour { 167, 228, 142 };
// The "View: EQ + Dyn" box, removed from the background until the dynamic
// view exists (UI-2c).
inline constexpr Rect removedViewBox { 1093.0f, 198.0f, 165.0f, 37.0f };

// UI-2b: the spectrum on the screen: its own scale, 0 dBFS on the top grid
// line and -96 dBFS on the bottom one (16 dB per line; approved, 4 px inside
// the plot's edges), the same in SPECTRUM and ALL; the EQ keeps its +-24 dB.
// Colours sampled from the master's illustrative spectrum.
inline constexpr float spectrumTopDb = 0.0f, spectrumBottomDb = -96.0f;

// UI-2b: the screen's vertical scales. The left axis (the +-24 dB of the EQ,
// drawn into the background) holds its labels at these centres, right edge
// x 391 (measured). In SPECTRUM the editor covers it with the same strip
// without labels (asset axis_blank.png) and writes the dBFS scale there in
// the same font and colour; in ALL a discreet dBFS scale sits inside the
// plot's right edge.
inline constexpr std::array<float, 7> axisLabelCentres { 254.0f, 289.0f, 325.0f, 362.0f, 400.0f, 437.0f, 472.0f };
inline constexpr float axisLabelRight = 391.0f;
inline constexpr Rect axisBlankSprite { 356.0f, 240.0f, 49.0f, 245.0f };
inline constexpr float axisUnitCentreY = 238.0f; // "dBFS", above the top label
inline constexpr float rightScaleRight = 1309.0f, rightScaleLeft = 1284.0f;
inline constexpr Rgb spectrumLineColour { 154, 209, 155 }, spectrumFillColour { 72, 113, 72 };
} // namespace labq8::ui::geometry
