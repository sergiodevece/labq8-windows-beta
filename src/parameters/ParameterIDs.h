#pragma once

#include <array>

// Stable parameter identifiers. Once a build has been used in a saved
// session these strings MUST NOT change: hosts store automation and state
// against them. See docs/PARAMETER_SCHEMA.md.

namespace labq8::params
{
inline constexpr int numBands = 8;

// JUCE ParameterID version hints (AU/VST3 parameter ordering). A parameter
// keeps the hint of the phase that introduced it.
inline constexpr int versionHintE0  = 1;
inline constexpr int versionHintE1a = 2;
inline constexpr int versionHintE1b = 3;
inline constexpr int versionHintE2c = 4;
inline constexpr int versionHintE4  = 5; // proc_mode and the MID / SIDE banks

struct BandIDs
{
    const char* frequency;
    const char* gain;
    const char* q;
    const char* bypass;
    const char* type; // E1b
};

// Spelled out on purpose (no string building): every frozen ID is greppable.
inline constexpr std::array<BandIDs, numBands> bandIDs { {
    { "b1_freq", "b1_gain", "b1_q", "b1_bypass", "b1_type" }, // E0
    { "b2_freq", "b2_gain", "b2_q", "b2_bypass", "b2_type" }, // E1a
    { "b3_freq", "b3_gain", "b3_q", "b3_bypass", "b3_type" },
    { "b4_freq", "b4_gain", "b4_q", "b4_bypass", "b4_type" },
    { "b5_freq", "b5_gain", "b5_q", "b5_bypass", "b5_type" },
    { "b6_freq", "b6_gain", "b6_q", "b6_bypass", "b6_type" },
    { "b7_freq", "b7_gain", "b7_q", "b7_bypass", "b7_type" },
    { "b8_freq", "b8_gain", "b8_q", "b8_bypass", "b8_type" },
} };

// E2c.1: dynamics of each band (DSP: dsp::DynamicSection). Same rules.
struct DynamicIDs
{
    const char* enabled;
    const char* threshold;
    const char* range;
    const char* attack;
    const char* release;
};

inline constexpr std::array<DynamicIDs, numBands> dynamicIDs { {
    { "b1_dyn_on", "b1_threshold", "b1_range", "b1_attack", "b1_release" },
    { "b2_dyn_on", "b2_threshold", "b2_range", "b2_attack", "b2_release" },
    { "b3_dyn_on", "b3_threshold", "b3_range", "b3_attack", "b3_release" },
    { "b4_dyn_on", "b4_threshold", "b4_range", "b4_attack", "b4_release" },
    { "b5_dyn_on", "b5_threshold", "b5_range", "b5_attack", "b5_release" },
    { "b6_dyn_on", "b6_threshold", "b6_range", "b6_attack", "b6_release" },
    { "b7_dyn_on", "b7_threshold", "b7_range", "b7_attack", "b7_release" },
    { "b8_dyn_on", "b8_threshold", "b8_range", "b8_attack", "b8_release" },
} };

// E4a: the MID and SIDE banks. Each is a full copy of the STEREO bank's
// contract (same types, ranges, grids, defaults and texts; names prefixed
// "Mid " / "Side "), with its own IDs. The STEREO bank is bandIDs +
// dynamicIDs above: its 80 IDs keep their order and meaning.
inline constexpr std::array<BandIDs, numBands> midBandIDs { {
    { "mid_b1_freq", "mid_b1_gain", "mid_b1_q", "mid_b1_bypass", "mid_b1_type" },
    { "mid_b2_freq", "mid_b2_gain", "mid_b2_q", "mid_b2_bypass", "mid_b2_type" },
    { "mid_b3_freq", "mid_b3_gain", "mid_b3_q", "mid_b3_bypass", "mid_b3_type" },
    { "mid_b4_freq", "mid_b4_gain", "mid_b4_q", "mid_b4_bypass", "mid_b4_type" },
    { "mid_b5_freq", "mid_b5_gain", "mid_b5_q", "mid_b5_bypass", "mid_b5_type" },
    { "mid_b6_freq", "mid_b6_gain", "mid_b6_q", "mid_b6_bypass", "mid_b6_type" },
    { "mid_b7_freq", "mid_b7_gain", "mid_b7_q", "mid_b7_bypass", "mid_b7_type" },
    { "mid_b8_freq", "mid_b8_gain", "mid_b8_q", "mid_b8_bypass", "mid_b8_type" },
} };

inline constexpr std::array<DynamicIDs, numBands> midDynamicIDs { {
    { "mid_b1_dyn_on", "mid_b1_threshold", "mid_b1_range", "mid_b1_attack", "mid_b1_release" },
    { "mid_b2_dyn_on", "mid_b2_threshold", "mid_b2_range", "mid_b2_attack", "mid_b2_release" },
    { "mid_b3_dyn_on", "mid_b3_threshold", "mid_b3_range", "mid_b3_attack", "mid_b3_release" },
    { "mid_b4_dyn_on", "mid_b4_threshold", "mid_b4_range", "mid_b4_attack", "mid_b4_release" },
    { "mid_b5_dyn_on", "mid_b5_threshold", "mid_b5_range", "mid_b5_attack", "mid_b5_release" },
    { "mid_b6_dyn_on", "mid_b6_threshold", "mid_b6_range", "mid_b6_attack", "mid_b6_release" },
    { "mid_b7_dyn_on", "mid_b7_threshold", "mid_b7_range", "mid_b7_attack", "mid_b7_release" },
    { "mid_b8_dyn_on", "mid_b8_threshold", "mid_b8_range", "mid_b8_attack", "mid_b8_release" },
} };

inline constexpr std::array<BandIDs, numBands> sideBandIDs { {
    { "side_b1_freq", "side_b1_gain", "side_b1_q", "side_b1_bypass", "side_b1_type" },
    { "side_b2_freq", "side_b2_gain", "side_b2_q", "side_b2_bypass", "side_b2_type" },
    { "side_b3_freq", "side_b3_gain", "side_b3_q", "side_b3_bypass", "side_b3_type" },
    { "side_b4_freq", "side_b4_gain", "side_b4_q", "side_b4_bypass", "side_b4_type" },
    { "side_b5_freq", "side_b5_gain", "side_b5_q", "side_b5_bypass", "side_b5_type" },
    { "side_b6_freq", "side_b6_gain", "side_b6_q", "side_b6_bypass", "side_b6_type" },
    { "side_b7_freq", "side_b7_gain", "side_b7_q", "side_b7_bypass", "side_b7_type" },
    { "side_b8_freq", "side_b8_gain", "side_b8_q", "side_b8_bypass", "side_b8_type" },
} };

inline constexpr std::array<DynamicIDs, numBands> sideDynamicIDs { {
    { "side_b1_dyn_on", "side_b1_threshold", "side_b1_range", "side_b1_attack", "side_b1_release" },
    { "side_b2_dyn_on", "side_b2_threshold", "side_b2_range", "side_b2_attack", "side_b2_release" },
    { "side_b3_dyn_on", "side_b3_threshold", "side_b3_range", "side_b3_attack", "side_b3_release" },
    { "side_b4_dyn_on", "side_b4_threshold", "side_b4_range", "side_b4_attack", "side_b4_release" },
    { "side_b5_dyn_on", "side_b5_threshold", "side_b5_range", "side_b5_attack", "side_b5_release" },
    { "side_b6_dyn_on", "side_b6_threshold", "side_b6_range", "side_b6_attack", "side_b6_release" },
    { "side_b7_dyn_on", "side_b7_threshold", "side_b7_range", "side_b7_attack", "side_b7_release" },
    { "side_b8_dyn_on", "side_b8_threshold", "side_b8_range", "side_b8_attack", "side_b8_release" },
} };

// The three banks. Code that reads or writes a band's parameters goes
// through bankOf() with an explicit Bank, never through a bare array, so
// the bank is always named where an ID is chosen (docs/MIDSIDE_E4A.md).
enum class Bank { stereo = 0, mid = 1, side = 2 };
inline constexpr int numBanks = 3;

struct BankIDs
{
    const std::array<BandIDs, numBands>& bands;
    const std::array<DynamicIDs, numBands>& dynamics;
    const char* namePrefix; // host name prefix ("" for STEREO: E0..E2c.1 names unchanged)
};

constexpr BankIDs bankOf (Bank bank) noexcept
{
    switch (bank)
    {
        case Bank::mid:  return { midBandIDs, midDynamicIDs, "Mid " };
        case Bank::side: return { sideBandIDs, sideDynamicIDs, "Side " };
        case Bank::stereo: break;
    }
    return { bandIDs, dynamicIDs, "" };
}

// E4a: processing mode. Choice index = the mode, FROZEN. Only selects which
// bank(s) the DSP runs; it never rewrites any bank's parameters.
inline constexpr const char* procModeID = "proc_mode";
inline constexpr std::array<const char*, 2> procModeNames { "Stereo", "Mid/Side" };
enum class ProcMode { stereo = 0, midSide = 1 };

// Defaults of the public dynamic parameters (ranges: dsp::dynamics::limits).
inline constexpr double defaultThresholdDb = -20.0;
inline constexpr double defaultRangeDb     = 6.0;
inline constexpr double defaultAttackMs    = 10.0;
inline constexpr double defaultReleaseMs   = 100.0;

// Host order: the 32 E0/E1a parameters keep indices 0..31 (b1..b8, each
// freq, gain, q, bypass); the 8 E1b type parameters follow as 32..39; the
// 40 E2c.1 dynamic parameters follow as 40..79 (b1..b8, each dyn_on,
// threshold, range, attack, release). E4a appends proc_mode as 80, then the
// MID bank as 81..160 and the SIDE bank as 161..240, each band by band
// (b1..b8, each freq, gain, q, bypass, type, dyn_on, threshold, range,
// attack, release).
inline constexpr int parametersPerBank = 10 * numBands;
inline constexpr int numParametersV4 = parametersPerBank;          // 0..79
inline constexpr int procModeIndex = numParametersV4;              // 80
inline constexpr int midBankFirstIndex = procModeIndex + 1;        // 81
inline constexpr int sideBankFirstIndex = midBankFirstIndex + parametersPerBank; // 161
inline constexpr int numParameters = sideBankFirstIndex + parametersPerBank;     // 241

// b{n}_type choices. The index of each name is dsp::FilterType and is FROZEN.
inline constexpr std::array<const char*, 6> filterTypeNames {
    "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch"
};

// Default centre frequencies (Hz). Band 1 keeps its E0 default; bands 2..8
// spread over the spectrum around it. Rationale in docs/PARAMETER_SCHEMA.md.
inline constexpr std::array<double, numBands> defaultFrequencyHz {
    1000.0, 40.0, 100.0, 250.0, 2500.0, 5000.0, 10000.0, 16000.0
};

namespace id
{
    inline constexpr const char* band1Frequency = bandIDs[0].frequency;
    inline constexpr const char* band1Gain      = bandIDs[0].gain;
    inline constexpr const char* band1Q         = bandIDs[0].q;
    inline constexpr const char* band1Bypass    = bandIDs[0].bypass;
} // namespace id
} // namespace labq8::params
