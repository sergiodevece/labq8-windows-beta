#pragma once

#include "analysis/TrackAnalyzer.h"
#include "assist/Source.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <string_view>

namespace iaq8::assist
{
// Contract strings of Source: voice, acoustic_guitar, electric_guitar, bass,
// drums, mix, other.
const char* toString (Source source);
std::optional<Source> sourceFromString (std::string_view text); // exact match, else nullopt

// The analysis as the server/AI sees it (version 1). Keys are snake_case and
// values are rounded: dB to 0.1, Hz to 1, widths/correlation/seconds to 0.01
// (spectrum.step_oct is 1/6 octave, rounded to 0.1667). A non-finite level or
// spectrum value is written as -120 (the floor), a non-finite crest, slope or
// magnitude as 0, other non-finite numbers as 0.
//
//   { version, sample_rate, seconds, channels,
//     level:  { rms_dbfs, peak_dbfs, crest_db },
//     stereo: { is_mono, side_to_mid_db, correlation },
//     spectrum: { start_hz, step_oct, db: [60 numbers] },
//     trend:  { slope_db_per_oct, fit_from_hz, fit_to_hz },
//     candidates: [ { id, kind, freq_hz, magnitude_db, width_oct } ],
//     enough_signal }
//
// Semantics:
//  - spectrum.db is a power spectral density in dB re FS^2/Hz (not a level in
//    dBFS) at 1/6-octave points: db[i] is at start_hz * 2^(i * step_oct),
//    i = 0..59. It is meaningful only when enough_signal is true; otherwise
//    there are no candidates and every value is -120.
//  - kind is excess | deficit | resonance | low_rumble | sibilance. freq_hz is
//    where the candidate peaks; width_oct is its extent in octaves.
//  - magnitude_db is how far above (excess, resonance, sibilance) or below
//    (deficit, shown as a positive number) the trend/reference the spectrum
//    is. For low_rumble it is the dB by which the energy below the fit range
//    exceeds the -20 dB allowance (of the total energy), not a spectral level.
//  - trend is the robust line fitted to the 1/3-octave density between
//    fit_from_hz and fit_to_hz (slope in dB per octave); candidates are only
//    searched in that range.
//  - candidates are sorted by |magnitude_db| descending, at most 8, ids c1..c8.
juce::var toJson (const analysis::AnalysisSummary& summary);

// The only supported serialiser: compact, one line, at most 4 decimals. Do not
// serialise toJson's var with the default JSON::toString: its 15 decimals print
// 0.1 steps as 70.599999999999994.
juce::String toJsonText (const analysis::AnalysisSummary& summary);
} // namespace iaq8::assist
