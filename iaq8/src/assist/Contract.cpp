#include "assist/Contract.h"

#include "analysis/AnalysisTuning.h"
#include "parameters/ParameterIDs.h"

#include <algorithm>
#include <array>
#include <cmath>
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

// Deepest [ / { nesting outside strings. JUCE's parser recurses once per
// level without a limit, so a hostile body could overflow the pool thread's
// stack; the body is rejected before parsing when it is too deep.
int nestingDepth (const juce::String& text)
{
    int depth = 0, deepest = 0;
    bool inString = false, escaped = false;
    for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;
        if (inString)
        {
            if (escaped)        escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"')  inString = false;
        }
        else if (c == '"')             inString = true;
        else if (c == '[' || c == '{') deepest = std::max (deepest, ++depth);
        else if (c == ']' || c == '}') --depth;
    }
    return deepest;
}

Reply parseOk (const juce::String& body)
{
    const auto invalid = makeError (ErrorKind::invalidResponse);
    if (nestingDepth (body) > maxJsonDepth)
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
