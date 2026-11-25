#include "assist/Contract.h"

#include "analysis/AnalysisTuning.h"
#include "parameters/ParameterIDs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace iaq8::assist
{
namespace
{
struct SourceName { Source source; const char* name; };
constexpr std::array<SourceName, 7> kSources { {
    { Source::voice, "voice" },
    { Source::acousticGuitar, "acoustic_guitar" },
    { Source::electricGuitar, "electric_guitar" },
    { Source::bass, "bass" },
    { Source::drums, "drums" },
    { Source::mix, "mix" },
    { Source::other, "other" } } };

const char* kindName (analysis::CandidateKind k)
{
    using K = analysis::CandidateKind;
    switch (k)
    {
        case K::excess:    return "excess";
        case K::deficit:   return "deficit";
        case K::resonance: return "resonance";
        case K::lowRumble: return "low_rumble";
        case K::sibilance: return "sibilance";
    }
    return "excess";
}

// Rounds to 1/scale by dividing (not multiplying by the step), which gives the
// double nearest the decimal, so the text has no 0.60000000000001 noise.
// A non-finite value becomes `fallback` (never a misleading full scale).
double round (double v, double scale, double fallback)
{
    return std::isfinite (v) ? std::round (v * scale) / scale + 0.0 : fallback; // + 0.0: no "-0.0"
}
constexpr double kFloorDb = analysis::tuning::floorDb;
juce::var r1 (double v, double fallback = kFloorDb) { return round (v, 10.0, fallback); }   // dB: -120 when not finite
juce::var rDiff (double v) { return r1 (v, 0.0); }    // dB differences (crest, slope, magnitude): 0 when not finite
juce::var r0 (double v) { return round (v, 1.0, 0.0); }    // Hz
juce::var r2 (double v) { return round (v, 100.0, 0.0); }  // octaves, seconds, correlation
} // namespace

const char* toString (Source source)
{
    for (const auto& s : kSources)
        if (s.source == source)
            return s.name;
    return "other";
}

std::optional<Source> sourceFromString (std::string_view text)
{
    for (const auto& s : kSources)
        if (text == s.name)
            return s.source;
    return std::nullopt;
}

juce::var toJson (const analysis::AnalysisSummary& a)
{
    const auto object = [] { return new juce::DynamicObject(); };

    auto* root = object();
    root->setProperty ("version", a.version);
    root->setProperty ("sample_rate", r0 (a.sampleRate));
    root->setProperty ("seconds", r2 (a.seconds));
    root->setProperty ("channels", a.channels);

    auto* level = object();
    level->setProperty ("rms_dbfs", r1 (a.rmsDbfs));
    level->setProperty ("peak_dbfs", r1 (a.peakDbfs));
    level->setProperty ("crest_db", rDiff (a.crestDb));
    root->setProperty ("level", juce::var (level));

    auto* stereo = object();
    stereo->setProperty ("is_mono", a.isMono);
    stereo->setProperty ("side_to_mid_db", r1 (a.sideToMidDb));
    stereo->setProperty ("correlation", r2 (a.correlation));
    root->setProperty ("stereo", juce::var (stereo));

    juce::Array<juce::var> db;
    for (auto v : a.spectrumDb)
        db.add (r1 (v));
    auto* spectrum = object();
    spectrum->setProperty ("start_hz", r0 (analysis::AnalysisSummary::spectrumStartHz));
    spectrum->setProperty ("step_oct", round (analysis::AnalysisSummary::spectrumStepOct, 10000.0, 0.0));
    spectrum->setProperty ("db", db);
    root->setProperty ("spectrum", juce::var (spectrum));

    auto* trend = object();
    trend->setProperty ("slope_db_per_oct", rDiff (a.slopeDbPerOct));
    trend->setProperty ("fit_from_hz", r0 (a.fitFromHz));
    trend->setProperty ("fit_to_hz", r0 (a.fitToHz));
    root->setProperty ("trend", juce::var (trend));

    juce::Array<juce::var> candidates;
    for (const auto& c : a.candidates)
    {
        auto* o = object();
        o->setProperty ("id", juce::String (c.id));
        o->setProperty ("kind", kindName (c.kind));
        o->setProperty ("freq_hz", r0 (c.freqHz));
        o->setProperty ("magnitude_db", rDiff (c.magnitudeDb));
        o->setProperty ("width_oct", r2 (c.widthOct));
        candidates.add (juce::var (o));
    }
    root->setProperty ("candidates", candidates);
    root->setProperty ("enough_signal", a.enoughSignal);
    return juce::var (root);
}

juce::String toJsonText (const analysis::AnalysisSummary& summary)
{
    return juce::JSON::toString (toJson (summary), true, 4);
}

std::optional<analysis::AnalysisSummary> analysisCandidatesFromJson (const juce::String& json)
{
    const auto root = juce::JSON::parse (json);
    const auto* o = root.getDynamicObject();
    const auto* list = o != nullptr ? o->getProperty ("candidates").getArray() : nullptr;
    if (list == nullptr)
        return std::nullopt;

    const auto number = [] (const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble() ? (double) v : 0.0; };
    analysis::AnalysisSummary a;
    a.enoughSignal = (bool) o->getProperty ("enough_signal");
    for (const auto& item : *list)
    {
        const auto* c = item.getDynamicObject();
        if (c == nullptr || ! c->getProperty ("id").isString() || ! c->getProperty ("kind").isString())
            return std::nullopt;
        std::optional<analysis::CandidateKind> kind;
        for (const auto k : { analysis::CandidateKind::excess, analysis::CandidateKind::deficit, analysis::CandidateKind::resonance,
                              analysis::CandidateKind::lowRumble, analysis::CandidateKind::sibilance })
            if (c->getProperty ("kind").toString() == kindName (k))
                kind = k;
        if (! kind)
            return std::nullopt;
        a.candidates.push_back ({ c->getProperty ("id").toString().toStdString(), *kind, number (c->getProperty ("freq_hz")),
                                  number (c->getProperty ("magnitude_db")), number (c->getProperty ("width_oct")) });
    }
    return a;
}
} // namespace iaq8::assist


//==============================================================================
namespace iaq8::assist
{
namespace
{
using labq8::dsp::FilterType;

struct TypeName { FilterType type; const char* name; };
constexpr std::array<TypeName, 6> kTypes { {
    { FilterType::bell, "bell" },
    { FilterType::lowShelf, "low_shelf" },
    { FilterType::highShelf, "high_shelf" },
    { FilterType::lowCut, "low_cut" },
    { FilterType::highCut, "high_cut" },
    { FilterType::notch, "notch" } } };

juce::String fromUtf8 (const std::string& s) { return juce::String (juce::CharPointer_UTF8 (s.c_str())); }

juce::var nullOr (const std::string& s) { return s.empty() ? juce::var() : juce::var (fromUtf8 (s)); }

// Band values: 0.1 Hz, 0.01 dB, 0.001 Q. A non-finite value (never expected
// from the parameters) is written as the slot's default, never as NaN.
juce::var bandJson (const labq8::dsp::BandSettings& b, size_t slot, const std::string& candidateId, const std::string& reason)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", toString (b.type));
    o->setProperty ("freq_hz", round (b.frequencyHz, 10.0, labq8::params::defaultFrequencyHz[slot % labq8::params::defaultFrequencyHz.size()]));
    o->setProperty ("gain_db", round (b.gainDb, 100.0, 0.0));
    o->setProperty ("q", round (b.q, 1000.0, 1.0));
    o->setProperty ("bypass", b.bypassed);
    o->setProperty ("candidate_id", nullOr (candidateId));
    o->setProperty ("reason", nullOr (reason));
    return juce::var (o);
}

juce::var bandsObject (juce::Array<juce::var> bands)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("bands", bands);
    return juce::var (o);
}

// ---- reply parsing ----
bool isNumber (const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }

std::optional<double> finiteNumber (const juce::var& v)
{
    if (! isNumber (v))
        return std::nullopt;
    const double d = v;
    return std::isfinite (d) ? std::optional<double> (d) : std::nullopt;
}

// Absent or null -> "", string -> its text, anything else -> invalid.
std::optional<std::string> optionalString (const juce::DynamicObject& o, const char* key)
{
    const auto v = o.getProperty (key);
    if (v.isVoid() || v.isUndefined())
        return std::string();
    if (! v.isString())
        return std::nullopt;
    return v.toString().toStdString();
}

std::optional<std::vector<std::string>> quickReplies (const juce::DynamicObject& o)
{
    std::vector<std::string> out;
    const auto v = o.getProperty ("quick_replies");
    if (v.isVoid() || v.isUndefined())
        return out;
    const auto* items = v.getArray();
    if (items == nullptr)
        return std::nullopt;
    for (const auto& item : *items)
    {
        if (! item.isString())
            return std::nullopt;
        const auto text = item.toString().trim().substring (0, maxQuickReplyChars).trim();
        if (text.isNotEmpty() && out.size() < maxQuickReplies)
            out.push_back (text.toStdString());
    }
    return out;
}

std::optional<ProposedBand> parseBand (const juce::var& v)
{
    const auto* o = v.getDynamicObject();
    if (o == nullptr || ! o->getProperty ("type").isString())
        return std::nullopt;
    const auto type = filterTypeFromString (o->getProperty ("type").toString().toStdString());
    const auto freq = finiteNumber (o->getProperty ("freq_hz"));
    const auto gain = finiteNumber (o->getProperty ("gain_db"));
    const auto q = finiteNumber (o->getProperty ("q"));
    const auto candidate = optionalString (*o, "candidate_id");
    const auto reason = optionalString (*o, "reason");
    const auto bypass = o->getProperty ("bypass");
    const bool bypassOk = bypass.isVoid() || bypass.isUndefined() || bypass.isBool();
    if (! type || ! freq || ! gain || ! q || ! candidate || ! reason || ! bypassOk)
        return std::nullopt;

    ProposedBand b;
    b.settings = { *freq, *gain, *q, bypass.isBool() && (bool) bypass, *type };
    b.candidateId = *candidate;
    b.reason = *reason;
    return b;
}

} // namespace

namespace
{
// Strict RFC 8259 validator over the body's UTF-8 bytes, iterative (an explicit
// stack of open containers, never recursion), so any body is checked in O(n)
// time and constant stack.
class StrictJson
{
public:
    static constexpr size_t maxIntegerDigits = 15, maxExponentDigits = 3;

    StrictJson (const char* text, int maxDepthIn)
        : s (reinterpret_cast<const unsigned char*> (text)), maxDepth (maxDepthIn) {}

    bool run()
    {
        // `s` is NUL-terminated (a juce::String); NUL is never valid JSON
        // outside a string and is a control character inside one, so every
        // check below stops at it, exactly where JUCE's parser stops reading.
        for (;;)
        {
            skipWhitespace();                      // a value is expected here
            const auto c = s[i];
            if (c == '{' || c == '[')
            {
                if ((int) open.size() >= maxDepth)
                {
                    deepest = maxDepth + 1;
                    return false;
                }
                open.push_back ((char) c);
                deepest = std::max (deepest, (int) open.size());
                ++i;
                skipWhitespace();
                if (s[i] == (c == '{' ? '}' : ']'))
                {
                    open.pop_back();
                    ++i;
                }
                else
                {
                    if (c == '{' && ! key())
                        return false;
                    continue;                      // the first element or member value
                }
            }
            else if (! scalar())
            {
                return false;
            }

            // After a value: close containers, or a ',' starts the next value.
            for (;;)
            {
                skipWhitespace();
                if (open.empty())
                    return s[i] == 0;              // exactly one top-level value
                const char close = open.back() == '{' ? '}' : ']';
                if (s[i] == close)
                {
                    open.pop_back();
                    ++i;
                    continue;
                }
                if (s[i] != ',')
                    return false;
                ++i;
                if (open.back() == '{' && ! key())
                    return false;
                break;
            }
        }
    }

    int depth() const { return deepest; }

private:
    const unsigned char* s;
    size_t i = 0;
    const int maxDepth;
    int deepest = 0;
    std::string open; // '{' or '[' per open container

    void skipWhitespace() { while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') ++i; }

    static bool isDigit (unsigned char c) { return c >= '0' && c <= '9'; }
    static bool isHex (unsigned char c) { return isDigit (c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

    // A member name and its ':' (whitespace before both). Not empty: JUCE
    // stores names as Identifiers, which must not be empty.
    bool key()
    {
        skipWhitespace();
        if (s[i] != '"' || s[i + 1] == '"' || ! string())
            return false;
        skipWhitespace();
        if (s[i] != ':')
            return false;
        ++i;
        return true;
    }

    bool scalar()
    {
        const auto c = s[i];
        if (c == '"')                 return string();
        if (c == '-' || isDigit (c))  return number();
        return literal ("true") || literal ("false") || literal ("null");
    }

    bool literal (const char* word)
    {
        size_t n = 0;
        while (word[n] != 0 && s[i + n] == (unsigned char) word[n]) // stops at a mismatch or NUL
            ++n;
        if (word[n] != 0)
            return false;
        i += n;
        return true;
    }

    // Digits per run of [0-9] that the number may have.
    size_t digits (size_t max)
    {
        size_t n = 0;
        while (isDigit (s[i]) && n <= max)
        {
            ++i;
            ++n;
        }
        return n;
    }

    // -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?, with at most
    // maxIntegerDigits before the '.' and maxExponentDigits in the exponent:
    // JUCE reads the integer part into an int64 and the exponent into an int
    // with no overflow check (undefined behaviour). Values the contract uses
    // are far inside both caps; longer ones are rejected, not rounded.
    bool number()
    {
        if (s[i] == '-')
            ++i;
        if (s[i] == '0')
            ++i;
        else if (s[i] >= '1' && s[i] <= '9')
        {
            if (digits (maxIntegerDigits) > maxIntegerDigits)
                return false;
        }
        else
            return false;
        if (s[i] == '.')
        {
            ++i;
            if (! isDigit (s[i]))
                return false;
            while (isDigit (s[i])) ++i;
        }
        if (s[i] == 'e' || s[i] == 'E')
        {
            ++i;
            if (s[i] == '+' || s[i] == '-')
                ++i;
            const auto n = digits (maxExponentDigits);
            if (n == 0 || n > maxExponentDigits)
                return false;
        }
        return true;
    }

    // From the opening '"' to just past the closing one.
    bool string()
    {
        ++i;
        for (;;)
        {
            const auto c = s[i];
            if (c == '"')
            {
                ++i;
                return true;
            }
            if (c < 0x20)                          // raw control character, or NUL (end of text)
                return false;
            if (c == '\\')
            {
                const auto e = s[i + 1];
                if (e == 'u')
                {
                    for (size_t k = 2; k < 6; ++k)
                        if (! isHex (s[i + k]))    // stops at NUL too
                            return false;
                    i += 6;
                }
                else if (e == '"' || e == '\\' || e == '/' || e == 'b' || e == 'f' || e == 'n' || e == 'r' || e == 't')
                {
                    i += 2;
                }
                else
                {
                    return false;
                }
            }
            else if (c < 0x80)
            {
                ++i;
            }
            else if (! utf8Sequence())
            {
                return false;
            }
        }
    }

    // One well-formed UTF-8 sequence (RFC 3629: no overlong forms, no
    // surrogates, nothing above U+10FFFF). A well-formed sequence decodes to
    // a code point >= 0x80, never to a quote, backslash, bracket or NUL, so
    // JUCE's decoder sees the same ASCII structure as this byte scan. (JUCE 9
    // already maps malformed sequences to U+FFFD; this check does not rely on
    // that.)
    bool utf8Sequence()
    {
        const auto c = s[i];
        unsigned char lo = 0x80, hi = 0xbf;
        int extra = 0;
        if (c >= 0xc2 && c <= 0xdf)      extra = 1;
        else if (c == 0xe0)              { extra = 2; lo = 0xa0; }
        else if (c == 0xed)              { extra = 2; hi = 0x9f; }
        else if (c >= 0xe1 && c <= 0xef) extra = 2;
        else if (c == 0xf0)              { extra = 3; lo = 0x90; }
        else if (c == 0xf4)              { extra = 3; hi = 0x8f; }
        else if (c >= 0xf1 && c <= 0xf3) extra = 3;
        else                             return false;

        for (int k = 1; k <= extra; ++k)
        {
            const auto b = s[i + (size_t) k];      // a NUL fails here, so we never read past it
            if (b < (k == 1 ? lo : 0x80) || b > (k == 1 ? hi : 0xbf))
                return false;
        }
        i += (size_t) extra + 1;
        return true;
    }
};
} // namespace

bool isStrictJson (const juce::String& text, int maxDepth, int* depthOut)
{
    StrictJson v (text.toRawUTF8(), maxDepth);
    const bool ok = v.run();
    if (depthOut != nullptr)
        *depthOut = v.depth();
    return ok;
}

namespace
{

Reply parseOk (const juce::String& body)
{
    const auto invalid = makeError (ErrorKind::invalidResponse);
    // JUCE's parser is lax and recurses once per [ / { without a limit, so
    // only strictly valid JSON within the depth limit reaches it.
    if (! isStrictJson (body, maxJsonDepth))
        return invalid;
    juce::var root;
    if (juce::JSON::parse (body, root).failed())
        return invalid;
    const auto* o = root.getDynamicObject();
    if (o == nullptr || ! o->getProperty ("kind").isString())
        return invalid;

    const auto kind = o->getProperty ("kind").toString();
    const auto replies = quickReplies (*o);
    if (! replies)
        return invalid;

    if (kind == "question")
    {
        const auto text = o->getProperty ("text");
        if (! text.isString() || text.toString().trim().isEmpty())
            return invalid;
        return Question { text.toString().toStdString(), *replies };
    }

    if (kind == "proposal")
    {
        const auto explanation = o->getProperty ("explanation");
        const auto* bands = o->getProperty ("bands").getArray();
        if (! explanation.isString() || bands == nullptr)
            return invalid;
        Proposal p;
        p.explanation = explanation.toString().toStdString();
        p.quickReplies = *replies;
        for (int i = 0; i < bands->size() && p.bands.size() < maxBands; ++i)
        {
            auto band = parseBand ((*bands)[i]);
            if (! band)
                return invalid;
            p.bands.push_back (std::move (*band));
        }
        return p;
    }
    return invalid;
}
} // namespace

const char* userMessage (ErrorKind kind)
{
    switch (kind)
    {
        case ErrorKind::noAccess:        return "Tu código de acceso no es válido o ha caducado. Revísalo para seguir usando el asistente.";
        case ErrorKind::limitReached:    return "Has llegado al límite de uso del asistente. Tu ecualización sigue activa.";
        case ErrorKind::invalidRequest:  return "El asistente no ha podido entender la petición. Inténtalo de nuevo.";
        case ErrorKind::unavailable:     return "Sin conexión: tu ecualización sigue activa";
        case ErrorKind::invalidResponse: return "El asistente ha enviado una respuesta que no se puede usar. Inténtalo de nuevo.";
    }
    return "Sin conexión: tu ecualización sigue activa";
}

Error makeError (ErrorKind kind) { return { kind, userMessage (kind) }; }

const char* toString (FilterType type)
{
    for (const auto& t : kTypes)
        if (t.type == type)
            return t.name;
    return "bell";
}

std::optional<FilterType> filterTypeFromString (std::string_view text)
{
    for (const auto& t : kTypes)
        if (text == t.name)
            return t.type;
    return std::nullopt;
}

juce::String toJson (const TurnRequest& r)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("plugin_id", pluginId);
    root->setProperty ("plugin_version", IAQ8_PLUGIN_VERSION);
    root->setProperty ("session_id", fromUtf8 (r.sessionId));
    root->setProperty ("source", toString (r.source));
    root->setProperty ("source_note", fromUtf8 (r.sourceNote));

    juce::Array<juce::var> messages;
    for (const auto& m : r.messages)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("role", m.role == Message::Role::assistant ? "assistant" : "user");
        o->setProperty ("text", fromUtf8 (m.text));
        messages.add (juce::var (o));
    }
    root->setProperty ("messages", messages);
    root->setProperty ("analysis", r.analysis.has_value() ? toJson (*r.analysis) : juce::var());

    juce::Array<juce::var> applied;
    for (size_t i = 0; i < r.applied.size(); ++i)
        applied.add (bandJson (r.applied[i], i, {}, {}));
    root->setProperty ("applied", bandsObject (applied));

    if (r.proposal.has_value())
    {
        juce::Array<juce::var> proposed;
        for (size_t i = 0; i < r.proposal->size(); ++i)
            proposed.add (bandJson ((*r.proposal)[i].settings, i, (*r.proposal)[i].candidateId, (*r.proposal)[i].reason));
        root->setProperty ("proposal", bandsObject (proposed));
    }
    else
    {
        root->setProperty ("proposal", juce::var());
    }
    return juce::JSON::toString (juce::var (root), true, 4);
}

Reply parseReply (int httpStatus, const juce::String& body)
{
    switch (httpStatus)
    {
        case 200: return parseOk (body);
        case 401: return makeError (ErrorKind::noAccess);
        case 429: return makeError (ErrorKind::limitReached);
        case 422: return makeError (ErrorKind::invalidRequest);
        default:  return makeError (ErrorKind::unavailable); // 0 (network, timeout), 5xx, anything else
    }
}
} // namespace iaq8::assist
