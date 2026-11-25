#pragma once

#include "analysis/TrackAnalyzer.h"
#include "assist/Source.h"
#include "dsp/AssistEngine.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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

//==============================================================================
// One exchange with the server (fixed contract; the server implements it
// against tests/fixtures/contract/*.json):
//
//   POST {server}/v1/turn
//   Content-Type: application/json
//   Authorization: Bearer <access code>
//
// Request:
//   { plugin_id: "iaq8", plugin_version: "0.1.0", session_id: "<uuid>",
//     source: "voice|acoustic_guitar|electric_guitar|bass|drums|mix|other",
//     source_note: "<text, for other>" (always present, "" when none),
//     messages: [ { role: "assistant|user", text } ],
//     analysis: <toJson(AnalysisSummary)> | null,
//     applied:  { bands: [Band x 8] },
//     proposal: { bands: [Band x n] } | null }
//
//   Band: { type: "bell|low_shelf|high_shelf|low_cut|high_cut|notch",
//           freq_hz, gain_db, q, bypass: bool,
//           candidate_id: "c1" | null, reason: "<short sentence>" | null }
//   In requests freq_hz is rounded to 0.1 Hz, gain_db to 0.01 dB, q to 0.001.
//
// 200 reply, one of:
//   { kind: "question", text, quick_replies: [..] }
//   { kind: "proposal", explanation, bands: [Band..], quick_replies: [..] }
//   The body must be strict RFC 8259 JSON in UTF-8 (double-quoted strings,
//   no comments, no trailing commas, no NaN/Infinity, one top-level value)
//   nested at most 32 levels ([ or {); anything else is rejected unparsed.
//   quick_replies: at most 8 kept, each trimmed and cut to 18 characters,
//   empty ones dropped; the key may be absent. bands: at most 8 kept (the
//   rest are ignored); bypass may be absent (false), candidate_id and reason
//   may be null or absent.
//
// Errors (the body is ignored; the server sends { error: "<kind>" }):
//   401 -> noAccess, 429 -> limitReached, 422 -> invalidRequest,
//   5xx, timeout (an overall 30 s deadline per request, counted from when
//   it starts running, so time queued behind another request is excluded),
//   network error (status 0) or any other status
//   -> unavailable; 200 with an invalid body -> invalidResponse.

struct ProposedBand
{
    labq8::dsp::BandSettings settings;
    std::string candidateId, reason; // "" = null
};

struct Message
{
    enum class Role { assistant, user } role;
    std::string text;
};

struct TurnRequest
{
    std::string sessionId;
    Source source = Source::other;
    std::string sourceNote;
    std::vector<Message> messages;
    std::optional<analysis::AnalysisSummary> analysis;
    dsp::BandSet applied {};
    std::optional<std::vector<ProposedBand>> proposal;
};

struct Question
{
    std::string text;
    std::vector<std::string> quickReplies;
};

struct Proposal
{
    std::string explanation;
    std::vector<ProposedBand> bands;
    std::vector<std::string> quickReplies;
};

enum class ErrorKind { noAccess, limitReached, invalidRequest, unavailable, invalidResponse };

struct Error
{
    ErrorKind kind;
    std::string userMessage; // Spanish, UTF-8, fixed per kind
};

using Reply = std::variant<Question, Proposal, Error>;

inline constexpr const char* pluginId = "iaq8";
inline constexpr size_t maxBands = 8, maxQuickReplies = 8;
inline constexpr int maxQuickReplyChars = 18;
inline constexpr int maxJsonDepth = 32; // deeper 200 bodies are invalidResponse (checked by isStrictJson before parsing)

// The fixed Spanish (UTF-8) text shown for each error kind.
const char* userMessage (ErrorKind kind);
Error makeError (ErrorKind kind);

// Contract names of the band types (bell, low_shelf, ...).
const char* toString (labq8::dsp::FilterType type);
std::optional<labq8::dsp::FilterType> filterTypeFromString (std::string_view text);

// Request body: compact, one line, at most 4 decimals (same serialiser rule
// as toJsonText). Pure; any thread.
juce::String toJson (const TurnRequest& request);

// Pure; any thread. Never throws; anything unexpected is an Error.
Reply parseReply (int httpStatus, const juce::String& body);

// True when `text` is exactly one strict RFC 8259 JSON value (whitespace
// around it allowed) in well-formed UTF-8, nested at most `maxDepth` levels
// of [ / {. parseReply calls it before juce::JSON::parse, which is lax and
// recurses once per level without a limit: for strictly valid JSON that
// parser reads the same structure, so its recursion depth equals the bracket
// depth checked here. Iterative (constant stack, linear time) and safe on any
// byte sequence. `depthOut`, if given, receives the deepest level reached
// (maxDepth + 1 when the limit stopped it). Pure; any thread; exposed for tests.
bool isStrictJson (const juce::String& text, int maxDepth, int* depthOut = nullptr);
} // namespace iaq8::assist
