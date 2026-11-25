#pragma once

#include "assist/Contract.h"

#include <string>
#include <vector>

namespace iaq8::assist
{
// Safety limits applied by the plugin to every proposal (spec §6.5). The
// server validates too; the plugin never trusts it. Values are the initial
// ones of the spec, to be tuned by listening.
namespace limits
{
inline constexpr int maxActiveBands = 5;
inline constexpr double minGainDb = -12.0, maxGainDb = 6.0;
inline constexpr double minBellQ = 0.5, maxBellQ = 8.0;          // also the notch floor and its normal ceiling
inline constexpr double maxResonanceNotchQ = 18.0;               // notch on a measured resonance candidate
inline constexpr double minFrequencyHz = 20.0, maxFrequencyHz = 20000.0;
inline constexpr double voiceNoBoostBelowHz = 80.0, voiceMaxLowCutHz = 150.0;
inline constexpr double bassMaxLowCutHz = 50.0;
inline constexpr double mixMinGainDb = -6.0, mixMaxGainDb = 4.0;
} // namespace limits

struct LimitReport
{
    std::vector<std::string> adjustments; // one Spanish (UTF-8) line per change
};

// Pure. Returns the 8 bands to preview/apply. Proposal band k goes to slot k
// (k < 8); unused and discarded slots are a bypassed bell at the LABQ8
// default frequency of that slot, 0 dB, Q 1. Per band, in this order:
//  - non-finite frequency, gain or Q: discarded;
//  - frequency into 20..20000 Hz; voice: low_cut <= 150 Hz; bass: low_cut <= 50 Hz;
//  - gain (bell/shelves) into -12..+6 dB (mix: -6..+4 dB); cuts and the notch
//    ignore gain, so it is set to 0 without a line;
//  - voice: a boost (bell/shelf, gain > 0) below 80 Hz is discarded;
//  - Q: bell 0.5..8; notch 0.5..8, or up to 18 when its candidate_id names a
//    resonance candidate of `analysis`; shelves and cuts within LABQ8's
//    0.1..18 (the band itself limits their effective Q).
// Then, if more than 5 bands are active (not bypassed; cuts and notches count),
// the extra ones are discarded: bell/shelves first, smallest |gain| first,
// cuts/notches only after every gain band (last slot first); ties discard the
// later slot. Bypassed bands are kept (clamped as above). Each change adds a
// line to report.adjustments. `analysis` may be null.
dsp::BandSet sanitize (const Proposal& proposal, Source source, const analysis::AnalysisSummary* analysis, LimitReport& report);

// The bypassed default band of `slot` (0..7).
labq8::dsp::BandSettings unusedBand (size_t slot);
} // namespace iaq8::assist
