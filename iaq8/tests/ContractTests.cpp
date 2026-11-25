// Exact float comparisons are intentional (values read back from JSON).
#pragma clang diagnostic ignored "-Wfloat-equal"

// Tests of the server contract: request JSON and reply parsing, against the
// committed examples in fixtures/contract (the server implements the same).

#include "TestHarness.h"

#include "assist/Contract.h"
#include "parameters/ParameterIDs.h"

#include <cctype>
#include <cmath>
#include <limits>
#include <regex>
#include <thread>
#include <utility>

using namespace iaq8;
using assist::Source;
using labq8::dsp::FilterType;
using labq8::test::str;

namespace
{
juce::File fixture (const char* name)
{
    return juce::File (IAQ8_TEST_FIXTURES_DIR).getChildFile ("contract").getChildFile (name);
}

juce::String fixtureText (const char* name) { return fixture (name).loadFileAsString(); }

bool isNumber (const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }

// Deep, order-independent comparison of parsed JSON. Numbers compare by value
// (48000 and 48000.0 are equal). `where` receives the first difference.
bool sameJson (const juce::var& a, const juce::var& b, const juce::String& path, juce::String& where)
{
    if (isNumber (a) && isNumber (b))
    {
        if (std::abs ((double) a - (double) b) < 1.0e-9)
            return true;
        where = path + ": " + a.toString() + " != " + b.toString();
        return false;
    }
    if (a.isVoid() || b.isVoid())
    {
        if (a.isVoid() && b.isVoid())
            return true;
        where = path + ": null vs " + (a.isVoid() ? b.toString() : a.toString());
        return false;
    }
    if (a.isBool() || b.isBool())
    {
        if (a.isBool() && b.isBool() && (bool) a == (bool) b)
            return true;
        where = path + ": bool mismatch";
        return false;
    }
    if (a.isString() || b.isString())
    {
        if (a.isString() && b.isString() && a.toString() == b.toString())
            return true;
        where = path + ": \"" + a.toString() + "\" != \"" + b.toString() + "\"";
        return false;
    }
    if (a.isArray() || b.isArray())
    {
        if (! (a.isArray() && b.isArray()) || a.size() != b.size())
        {
            where = path + ": array size";
            return false;
        }
        for (int i = 0; i < a.size(); ++i)
            if (! sameJson (a[i], b[i], path + "[" + juce::String (i) + "]", where))
                return false;
        return true;
    }
    auto* oa = a.getDynamicObject();
    auto* ob = b.getDynamicObject();
    if (oa == nullptr || ob == nullptr)
    {
        where = path + ": not an object";
        return false;
    }
    for (const auto& p : ob->getProperties())
        if (! oa->hasProperty (p.name))
        {
            where = path + "." + p.name.toString() + ": missing";
            return false;
        }
    for (const auto& p : oa->getProperties())
    {
        if (! ob->hasProperty (p.name))
        {
            where = path + "." + p.name.toString() + ": unexpected";
            return false;
        }
        if (! sameJson (p.value, ob->getProperty (p.name), path + "." + p.name.toString(), where))
            return false;
    }
    return true;
}

// The analysis of the first-turn example (hand-made, independent of the
// analyser's tuning).
analysis::AnalysisSummary exampleSummary()
{
    analysis::AnalysisSummary s;
    s.sampleRate = 48000; s.seconds = 12; s.channels = 2;
    s.rmsDbfs = -18.7; s.peakDbfs = -4.2; s.crestDb = 14.5;
    s.isMono = false; s.sideToMidDb = -14.2; s.correlation = 0.82;
    for (int i = 0; i < 60; ++i)
        s.spectrumDb.push_back (-40.0 - 0.5 * i);
    s.slopeDbPerOct = -3.3; s.fitFromHz = 100; s.fitToHz = 12000;
    s.candidates = { { "c1", analysis::CandidateKind::resonance, 2480, 7.2, 0.05 },
                     { "c2", analysis::CandidateKind::excess, 257, 4.5, 0.91 } };
    s.enoughSignal = true;
    return s;
}

dsp::BandSet defaultBands()
{
    dsp::BandSet b {};
    for (size_t i = 0; i < b.size(); ++i)
        b[i] = { labq8::params::defaultFrequencyHz[i], 0.0, 1.0, true, FilterType::bell };
    return b;
}

assist::TurnRequest firstTurn()
{
    assist::TurnRequest r;
    r.sessionId = "3f2b8c1e-6d4a-4f7e-9b2a-1c5d8e7f9a00";
    r.source = Source::voice;
    r.messages = { { assist::Message::Role::user, "Quiero que la voz suene m\xc3\xa1s clara y menos nasal" } };
    r.analysis = exampleSummary();
    r.applied = defaultBands();
    return r;
}

int countLongDecimals (const std::string& t)
{
    int bad = 0;
    for (size_t i = 0; i < t.size(); ++i)
        if (t[i] == '.' && i > 0 && std::isdigit ((unsigned char) t[i - 1]))
        {
            size_t j = i + 1;
            while (j < t.size() && std::isdigit ((unsigned char) t[j]))
                ++j;
            bad += (j - i - 1 > 4) ? 1 : 0;
        }
    return bad;
}
} // namespace

LQ_TEST ("contract: first-turn request equals the committed example")
{
    const auto expected = juce::JSON::parse (fixtureText ("request_first_turn.json"));
    LQ_CHECK (expected.isObject(), "request_first_turn.json parses");

    const auto text = assist::toJson (firstTurn());
    const auto actual = juce::JSON::parse (text);
    LQ_CHECK (actual.isObject(), "request text parses");

    juce::String where;
    LQ_CHECK (sameJson (actual, expected, "$", where), ("request differs at " + where).toStdString());
    LQ_CHECK (! text.containsChar ('\n'), "one line");
}

LQ_TEST ("contract: request with a proposal, notes and every type")
{
    auto r = firstTurn();
    r.source = Source::other;
    r.sourceNote = "ukelele";
    r.analysis.reset();
    r.messages.push_back ({ assist::Message::Role::assistant, "Hola" });
    std::vector<assist::ProposedBand> p;
    const FilterType types[] = { FilterType::bell, FilterType::lowShelf, FilterType::highShelf, FilterType::lowCut, FilterType::highCut, FilterType::notch };
    for (int i = 0; i < 6; ++i)
        p.push_back ({ { 100.0 * (i + 1), -2.0, 1.5, i == 2, types[i] }, i == 0 ? "c1" : "", i == 0 ? "Por algo" : "" });
    r.proposal = p;

    const auto j = juce::JSON::parse (assist::toJson (r));
    LQ_CHECK (j["source"].toString() == "other" && j["source_note"].toString() == "ukelele", "source and note");
    LQ_CHECK (j.getDynamicObject()->hasProperty ("analysis") && j["analysis"].isVoid(), "analysis: null");
    LQ_CHECK (j["messages"].size() == 2 && j["messages"][1]["role"].toString() == "assistant", "assistant role");
    const auto& bands = j["proposal"]["bands"];
    LQ_CHECK (bands.size() == 6, "6 proposed bands");
    const char* names[] = { "bell", "low_shelf", "high_shelf", "low_cut", "high_cut", "notch" };
    for (int i = 0; i < 6; ++i)
        LQ_CHECK (bands[i]["type"].toString() == names[i], str ("type ", names[i]));
    LQ_CHECK (bands[0]["candidate_id"].toString() == "c1" && bands[0]["reason"].toString() == "Por algo", "candidate and reason");
    LQ_CHECK (bands[1]["candidate_id"].isVoid() && bands[1]["reason"].isVoid(), "empty candidate and reason are null");
    LQ_CHECK ((bool) bands[2]["bypass"] && ! (bool) bands[1]["bypass"], "bypass");
    LQ_CHECK (j["applied"]["bands"].size() == 8, "8 applied bands");

    for (int i = 0; i < 6; ++i)
    {
        const auto t = assist::toString (types[i]);
        const auto back = assist::filterTypeFromString (t);
        LQ_CHECK (back.has_value() && *back == types[i], str ("type round trip ", t));
    }
    LQ_CHECK (! assist::filterTypeFromString ("peak").has_value(), "unknown type rejected");
}

LQ_TEST ("contract: request text has at most 4 decimals")
{
    auto r = firstTurn();
    r.applied[0] = { 1234.56789012, -3.333333333, 0.707106781, false, FilterType::bell };
    r.applied[1] = { 70.6, 1.0e-7, 0.1 + 0.2, false, FilterType::lowShelf };
    r.applied[2] = { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 1.0, false, FilterType::bell };
    r.proposal = std::vector<assist::ProposedBand> { { { 999.99999, 2.0 / 3.0, 1.0 / 3.0, false, FilterType::notch }, "", "" } };
    const auto text = assist::toJson (r).toStdString();
    LQ_CHECK (countLongDecimals (text) == 0, "no number with more than 4 decimals");
    LQ_CHECK (! std::regex_search (text, std::regex ("[:\\[,]\\s*-?[0-9]+(\\.[0-9]+)?[eE]")), "no exponent");
    LQ_CHECK (! std::regex_search (text, std::regex (":\\s*-?(nan|inf|NaN|Inf)", std::regex::icase)), "no non-finite text");
    LQ_CHECK (std::regex_search (std::string ("{\"a\": 1.0e-07}"), std::regex ("[:\\[,]\\s*-?[0-9]+(\\.[0-9]+)?[eE]")), "scanner detects exponents");
    const auto j = juce::JSON::parse (juce::String (text));
    LQ_CHECK (j.isObject(), "still valid JSON");
    LQ_CHECK ((double) j["applied"]["bands"][0]["freq_hz"] == 1234.6, str ("freq rounded to 0.1: ", (double) j["applied"]["bands"][0]["freq_hz"]));
    LQ_CHECK ((double) j["applied"]["bands"][0]["gain_db"] == -3.33, "gain rounded to 0.01");
    LQ_CHECK ((double) j["applied"]["bands"][0]["q"] == 0.707, "q rounded to 0.001");
    LQ_CHECK ((double) j["applied"]["bands"][1]["gain_db"] == 0.0, "tiny gain is 0");
    // A non-finite applied value is written as the slot's default (never NaN).
    LQ_CHECK ((double) j["applied"]["bands"][2]["freq_hz"] == 100.0 && (double) j["applied"]["bands"][2]["gain_db"] == 0.0,
              "non-finite values fall back to the slot defaults");
}

LQ_TEST ("contract: question reply")
{
    const auto reply = assist::parseReply (200, fixtureText ("reply_question.json"));
    const auto* q = std::get_if<assist::Question> (&reply);
    LQ_CHECK (q != nullptr, "question");
    if (q != nullptr)
    {
        LQ_CHECK (q->text == "\xc2\xbfQu\xc3\xa9 te molesta m\xc3\xa1s de la voz ahora mismo?", "text (UTF-8)");
        LQ_CHECK (q->quickReplies.size() == 4 && q->quickReplies[3] == "Suena \xc3\xa1spera", "quick replies");
    }
}

LQ_TEST ("contract: proposal reply")
{
    const auto reply = assist::parseReply (200, fixtureText ("reply_proposal.json"));
    const auto* p = std::get_if<assist::Proposal> (&reply);
    LQ_CHECK (p != nullptr, "proposal");
    if (p == nullptr)
        return;
    LQ_CHECK (p->explanation.rfind ("Limpio el grave", 0) == 0, "explanation");
    LQ_CHECK (p->bands.size() == 4, "4 bands");
    LQ_CHECK (p->bands[0].settings.type == FilterType::lowCut && p->bands[0].settings.frequencyHz == 90.0
                  && p->bands[0].candidateId.empty() && ! p->bands[0].settings.bypassed, "low cut, null candidate");
    LQ_CHECK (p->bands[1].settings.type == FilterType::bell && p->bands[1].settings.gainDb == -3.0
                  && p->bands[1].settings.q == 1.4 && p->bands[1].candidateId == "c2", "bell on c2");
    LQ_CHECK (p->bands[2].settings.type == FilterType::notch && p->bands[2].settings.q == 12.0 && p->bands[2].candidateId == "c1", "notch on c1");
    LQ_CHECK (p->bands[3].settings.type == FilterType::highShelf && p->bands[3].reason == "Un poco m\xc3\xa1s de aire", "shelf with reason");
    LQ_CHECK (p->quickReplies.size() == 4 && p->quickReplies[1] == "M\xc3\xa1s suave", "quick replies");
}

LQ_TEST ("contract: a proposal with 9 bands is truncated to 8")
{
    auto j = juce::JSON::parse (fixtureText ("reply_proposal.json"));
    auto* bands = j["bands"].getArray();
    LQ_CHECK (bands != nullptr, "bands array");
    if (bands == nullptr)
        return;
    while (bands->size() < 9)
        bands->add (juce::JSON::parse (juce::JSON::toString ((*bands)[1])));
    (*bands)[8].getDynamicObject()->setProperty ("type", "not_a_type"); // the 9th is never read
    const auto reply = assist::parseReply (200, juce::JSON::toString (j));
    const auto* p = std::get_if<assist::Proposal> (&reply);
    LQ_CHECK (p != nullptr && p->bands.size() == 8, "8 bands kept");
}

LQ_TEST ("contract: quick replies are capped")
{
    juce::String body (juce::CharPointer_UTF8 (
        "{\"kind\":\"question\",\"text\":\"\xc2\xbfY ahora?\",\"quick_replies\":["
        "\"uno\",\"  dos  \",\"\",\"Una respuesta demasiado larga\",\"\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba\xc3\xa1\xc3\xa9\xc3\xad\","
        "\"5\",\"6\",\"7\",\"8\",\"9\"]}"));
    const auto reply = assist::parseReply (200, body);
    const auto* q = std::get_if<assist::Question> (&reply);
    LQ_CHECK (q != nullptr, "question");
    if (q == nullptr)
        return;
    LQ_CHECK (q->quickReplies.size() == 8, str ("at most 8: ", q->quickReplies.size()));
    LQ_CHECK (q->quickReplies[1] == "dos", "trimmed");
    LQ_CHECK (q->quickReplies[2] == "Una respuesta dema", "cut to 18 characters");
    const auto accented = juce::String (juce::CharPointer_UTF8 (q->quickReplies[3].c_str()));
    LQ_CHECK (accented.length() == 18 && q->quickReplies[3].size() == 36, "18 characters, not bytes");
    for (const auto& r : q->quickReplies)
        LQ_CHECK (! r.empty() && juce::String (juce::CharPointer_UTF8 (r.c_str())).length() <= 18, "non-empty, <= 18 chars");
}

LQ_TEST ("contract: error statuses")
{
    const auto kindOf = [] (const assist::Reply& r)
    {
        const auto* e = std::get_if<assist::Error> (&r);
        return e != nullptr ? std::optional<assist::ErrorKind> (e->kind) : std::nullopt;
    };
    using K = assist::ErrorKind;
    const auto body401 = fixtureText ("reply_error_401.json");
    LQ_CHECK (kindOf (assist::parseReply (401, body401)) == K::noAccess, "401 -> noAccess");
    LQ_CHECK (kindOf (assist::parseReply (429, "{\"error\":\"limit_reached\"}")) == K::limitReached, "429 -> limitReached");
    LQ_CHECK (kindOf (assist::parseReply (422, "{\"error\":\"invalid_request\"}")) == K::invalidRequest, "422 -> invalidRequest");
    LQ_CHECK (kindOf (assist::parseReply (503, "")) == K::unavailable, "503 -> unavailable");
    LQ_CHECK (kindOf (assist::parseReply (500, "<html>")) == K::unavailable, "500 -> unavailable");
    LQ_CHECK (kindOf (assist::parseReply (0, "")) == K::unavailable, "0 (network/timeout) -> unavailable");
    LQ_CHECK (kindOf (assist::parseReply (404, "")) == K::unavailable, "other status -> unavailable");
    // A 200-like body with an error status is still the error.
    LQ_CHECK (kindOf (assist::parseReply (401, fixtureText ("reply_question.json"))) == K::noAccess, "status wins over body");

    // Every message is fixed, non-empty, valid UTF-8 Spanish.
    for (auto k : { K::noAccess, K::limitReached, K::invalidRequest, K::unavailable, K::invalidResponse })
    {
        const auto e = assist::makeError (k);
        LQ_CHECK (e.kind == k && ! e.userMessage.empty() && e.userMessage == assist::userMessage (k), "message per kind");
        LQ_CHECK (juce::CharPointer_UTF8::isValidString (e.userMessage.c_str(), (int) e.userMessage.size()), "valid UTF-8");
    }
    LQ_CHECK (assist::makeError (K::unavailable).userMessage == "Sin conexi\xc3\xb3n: tu ecualizaci\xc3\xb3n sigue activa", "offline text of the spec");
}

namespace
{
bool isInvalidResponse (const assist::Reply& r)
{
    const auto* e = std::get_if<assist::Error> (&r);
    return e != nullptr && e->kind == assist::ErrorKind::invalidResponse;
}

// parseReply (200, body) on a default-stack (512 KB) thread like the
// transport's pool thread, joined before the locals it uses go away.
assist::Reply parseOnThread (const juce::String& body)
{
    assist::Reply r;
    std::thread ([&] { r = assist::parseReply (200, body); }).join();
    return r;
}

bool strict (const juce::String& body) { return assist::isStrictJson (body, assist::maxJsonDepth); }

juce::String rawUtf8 (const char* bytes) { return juce::String (juce::CharPointer_UTF8 (bytes)); }
} // namespace

LQ_TEST ("contract: deeply nested bodies are rejected before parsing")
{
    // JUCE's parser recurses once per [ / { it sees, without a limit, and it is
    // lax (single quotes; after '-' it swallows any character, quotes and
    // brackets included), so these bodies would overflow a thread stack. Each
    // one is first checked against the validator; parseReply only runs when the
    // validator rejects it, so a regression fails the test instead of crashing.
    const auto repeat = [] (const char* s, int n) { return juce::String::repeatedString (s, n); };
    const auto brackets = repeat ("[", 100000) + repeat ("]", 100000);
    const juce::String overlongQuote = rawUtf8 ("\xc0\xa2"); // '"' as an overlong 2-byte sequence
    const std::pair<const char*, juce::String> bodies[] = {
        { "[[[...", repeat ("[", 100000) },
        { "{\"a\":{\"a\":...", repeat ("{\"a\":", 100000) },
        { "['\"', <deep>]", "['\"', " + brackets + "]" },
        { "[\"'\", <deep>]", "[\"'\", " + brackets + "]" },
        { "[-\"1, <deep>]", "[-\"1, " + brackets + "]" },
        { "[-'1, <deep>]", "[-'1, " + brackets + "]" },
        { "{\"a\":-\"1, \"b\":<deep>}", "{\"a\":-\"1, \"b\":" + brackets + "}" },
        { "[-], x200000 + 1", repeat ("[-],", 200000) + "1" },
        { "{\"a\": ({\"a\":-},\"b\":) x100000 + 1", "{\"a\":" + repeat ("{\"a\":-},\"b\":", 100000) + "1" },
        { "[\"<overlong \">, <deep>, <overlong \">\"]", "[\"" + overlongQuote + "," + brackets + "," + overlongQuote + "\"]" },
    };
    for (const auto& [name, body] : bodies)
    {
        const bool accepted = strict (body);
        LQ_CHECK (! accepted, str ("validator rejects ", name));
        if (! accepted)
            LQ_CHECK (isInvalidResponse (parseOnThread (body)), str ("invalidResponse, no crash: ", name));
    }

    // Exactly 32 levels are accepted, 33 are not (objects and arrays count alike).
    int depth = 0;
    LQ_CHECK (assist::isStrictJson (repeat ("[", 32) + repeat ("]", 32), 32, &depth) && depth == 32, str ("32 levels accepted, depth ", depth));
    LQ_CHECK (! assist::isStrictJson (repeat ("[", 33) + repeat ("]", 33), 32, &depth) && depth == 33, str ("33 levels rejected, depth ", depth));
    LQ_CHECK (assist::isStrictJson (repeat ("{\"a\":[", 16) + "1" + repeat ("]}", 16), 32, &depth) && depth == 32, "32 mixed levels accepted");
    LQ_CHECK (! assist::isStrictJson (repeat ("{\"a\":[", 16) + "{}" + repeat ("]}", 16), 32), "33 mixed levels rejected");

    const auto head = juce::String ("{\"kind\":\"question\",\"text\":\"x\",\"extra\":");
    LQ_CHECK (std::holds_alternative<assist::Question> (parseOnThread (head + repeat ("[", 31) + repeat ("]", 31) + "}")), "a 32-level reply parses");
    LQ_CHECK (isInvalidResponse (parseOnThread (head + repeat ("[", 32) + repeat ("]", 32) + "}")), "a 33-level reply is invalidResponse");

    // Brackets inside strings do not count.
    const auto inString = juce::String ("{\"kind\":\"question\",\"text\":\"") + repeat ("[{", 1000) + "\\\"]\"}";
    LQ_CHECK (std::holds_alternative<assist::Question> (parseOnThread (inString)), "brackets inside strings are ignored");
    const auto mixed = juce::String ("{\"kind\":\"question\",\"text\":\"it's [[[{{\"}");
    LQ_CHECK (std::holds_alternative<assist::Question> (parseOnThread (mixed)), "a ' inside a double-quoted string is plain text");
}

LQ_TEST ("contract: replies must be strict JSON")
{
    // Each body is a valid question except for the one grammar fault named.
    const std::pair<const char*, juce::String> rejected[] = {
        { "single-quoted string", "{\"kind\":\"question\",\"text\":'x'}" },
        { "single-quoted key", "{'kind':\"question\",\"text\":\"x\"}" },
        { "unquoted key", "{kind:\"question\",\"text\":\"x\"}" },
        { "trailing comma in object", "{\"kind\":\"question\",\"text\":\"x\",}" },
        { "trailing comma in array", "{\"kind\":\"question\",\"text\":\"x\",\"quick_replies\":[\"a\",]}" },
        { "leading comma in array", "{\"kind\":\"question\",\"text\":\"x\",\"quick_replies\":[,\"a\"]}" },
        { "missing colon", "{\"kind\" \"question\",\"text\":\"x\"}" },
        { "missing comma", "{\"kind\":\"question\" \"text\":\"x\"}" },
        { "leading zero", "{\"kind\":\"question\",\"text\":\"x\",\"n\":01}" },
        { "bare '-'", "{\"kind\":\"question\",\"text\":\"x\",\"n\":-}" },
        { "'-' then a space", "{\"kind\":\"question\",\"text\":\"x\",\"n\":- 1}" },
        { "leading '+'", "{\"kind\":\"question\",\"text\":\"x\",\"n\":+1}" },
        { "leading '.'", "{\"kind\":\"question\",\"text\":\"x\",\"n\":.5}" },
        { "trailing '.'", "{\"kind\":\"question\",\"text\":\"x\",\"n\":1.}" },
        { "empty exponent", "{\"kind\":\"question\",\"text\":\"x\",\"n\":1e}" },
        { "signed empty exponent", "{\"kind\":\"question\",\"text\":\"x\",\"n\":1e+}" },
        { "NaN", "{\"kind\":\"question\",\"text\":\"x\",\"n\":NaN}" },
        { "Infinity", "{\"kind\":\"question\",\"text\":\"x\",\"n\":Infinity}" },
        { "-Infinity", "{\"kind\":\"question\",\"text\":\"x\",\"n\":-Infinity}" },
        { "hex number", "{\"kind\":\"question\",\"text\":\"x\",\"n\":0x10}" },
        { "capitalised literal", "{\"kind\":\"question\",\"text\":\"x\",\"n\":True}" },
        { "truncated literal", "{\"kind\":\"question\",\"text\":\"x\",\"n\":nul}" },
        { "raw tab in a string", "{\"kind\":\"question\",\"text\":\"a\tb\"}" },
        { "raw newline in a string", "{\"kind\":\"question\",\"text\":\"a\nb\"}" },
        { "raw control 0x01 in a string", "{\"kind\":\"question\",\"text\":\"a\x01" "b\"}" },
        { "unknown escape \\x", "{\"kind\":\"question\",\"text\":\"a\\x41\"}" },
        { "escape \\a (JUCE accepts it)", "{\"kind\":\"question\",\"text\":\"a\\a\"}" },
        { "escape \\' (JUCE accepts it)", "{\"kind\":\"question\",\"text\":\"a\\'\"}" },
        { "short \\u escape", "{\"kind\":\"question\",\"text\":\"a\\u12\"}" },
        { "non-hex \\u escape", "{\"kind\":\"question\",\"text\":\"a\\u12G4\"}" },
        { "unterminated string", "{\"kind\":\"question\",\"text\":\"x}" },
        { "block comment", "{\"kind\":\"question\",/* c */\"text\":\"x\"}" },
        { "line comment", "{\"kind\":\"question\",\"text\":\"x\"// c\n}" },
        { "two top-level values", "{\"kind\":\"question\",\"text\":\"x\"} {}" },
        { "trailing garbage", "{\"kind\":\"question\",\"text\":\"x\"}x" },
        { "unclosed object", "{\"kind\":\"question\",\"text\":\"x\"" },
        { "mismatched close", "{\"kind\":\"question\",\"text\":\"x\",\"a\":[1}]" },
        { "extra close", "{\"kind\":\"question\",\"text\":\"x\"}}" },
        { "non-JSON whitespace (vertical tab)", "{\"kind\":\"question\",\"text\":\"x\"}\v" },
        { "byte >= 0x80 outside a string", rawUtf8 ("{\"kind\":\"question\",\"text\":\"x\"}\xc2\xa0") },
        { "overlong UTF-8", rawUtf8 ("{\"kind\":\"question\",\"text\":\"\xc0\xa2\"}") },
        { "lone continuation byte", rawUtf8 ("{\"kind\":\"question\",\"text\":\"\x80\"}") },
        { "UTF-8 surrogate", rawUtf8 ("{\"kind\":\"question\",\"text\":\"\xed\xa0\x80\"}") },
        { "truncated UTF-8", rawUtf8 ("{\"kind\":\"question\",\"text\":\"\xc3\"}") },
        { "beyond U+10FFFF", rawUtf8 ("{\"kind\":\"question\",\"text\":\"\xf4\x90\x80\x80\"}") },
    };
    for (const auto& [name, body] : rejected)
    {
        LQ_CHECK (! strict (body), str ("validator rejects: ", name));
        LQ_CHECK (isInvalidResponse (parseOnThread (body)), str ("invalidResponse: ", name));
    }

    // Accepted: every escape, raw UTF-8, numbers in every RFC 8259 form,
    // whitespace around the value, empty containers and nested extras.
    const auto question = juce::String (
        " \t\r\n{ \"kind\" : \"question\" ,\n\"text\":\"\\u00bfQu\\u00e9? \\\" \\\\ \\/ \\b \\f \\n \\r \\t \\ud83c\\udfb5 ") + rawUtf8 ("\xc3\xa1\xe2\x82\xac\xf0\x9f\x8e\xb5")
        + "\", \"quick_replies\": [ \"a\" , \"b\" ], \"extra\": { \"o\": {}, \"a\": [], \"n\": [0, -0, 12, -3.25, 1e3, 2E-2, -0.5e+10, 1.5E3],"
          " \"l\": [true, false, null], \"deep\": {\"x\": [1, {\"y\": [ {\"z\": null} ]}]} } }\r\n ";
    LQ_CHECK (strict (question), "validator accepts the full question");
    const auto q = parseOnThread (question);
    const auto* qq = std::get_if<assist::Question> (&q);
    LQ_CHECK (qq != nullptr && qq->quickReplies.size() == 2, "full question parses");
    if (qq != nullptr)
        LQ_CHECK (qq->text.rfind ("\xc2\xbfQu\xc3\xa9? \" \\ / \b \f \n \r \t \xf0\x9f\x8e\xb5 \xc3\xa1\xe2\x82\xac\xf0\x9f\x8e\xb5", 0) == 0,
                  "escapes and UTF-8 decoded");

    const auto proposal = juce::String ("{\"kind\":\"proposal\",\"explanation\":\"x\",\"bands\":["
                                        "{\"type\":\"bell\",\"freq_hz\":1.0e3,\"gain_db\":-2.5E0,\"q\":0.7,\"bypass\":false,\"candidate_id\":null,\"reason\":\"r\"},"
                                        "{\"type\":\"notch\",\"freq_hz\":250,\"gain_db\":0,\"q\":12,\"bypass\":true}],\"quick_replies\":[]}");
    LQ_CHECK (strict (proposal), "validator accepts the proposal");
    const auto pr = parseOnThread (proposal);
    const auto* p = std::get_if<assist::Proposal> (&pr);
    LQ_CHECK (p != nullptr && p->bands.size() == 2 && p->bands[0].settings.frequencyHz == 1000.0 && p->bands[0].settings.gainDb == -2.5
                  && p->bands[1].settings.bypassed, "proposal with exponents parses");

    for (const char* name : { "reply_question.json", "reply_proposal.json", "reply_error_401.json", "request_first_turn.json", "analysis_voice.json" })
        LQ_CHECK (strict (fixtureText (name)), str ("fixture is strict JSON: ", name));
    LQ_CHECK (strict (assist::toJson (firstTurn())), "our own request text is strict JSON");
    LQ_CHECK (! strict (fixtureText ("reply_malformed.json")), "reply_malformed.json is not");
    LQ_CHECK (! strict ("") && ! strict ("   "), "empty body is not JSON");
}

LQ_TEST ("contract: invalid 200 bodies are invalidResponse")
{
    const auto isInvalid = [] (const juce::String& body)
    {
        const auto r = assist::parseReply (200, body);
        const auto* e = std::get_if<assist::Error> (&r);
        return e != nullptr && e->kind == assist::ErrorKind::invalidResponse;
    };
    LQ_CHECK (isInvalid (fixtureText ("reply_malformed.json")), "reply_malformed.json");
    LQ_CHECK (isInvalid (""), "empty body");
    LQ_CHECK (isInvalid ("[]"), "not an object");
    LQ_CHECK (isInvalid ("{\"kind\":\"chat\",\"text\":\"x\"}"), "unknown kind");
    LQ_CHECK (isInvalid ("{\"kind\":\"question\"}"), "question without text");
    LQ_CHECK (isInvalid ("{\"kind\":\"question\",\"text\":\"\"}"), "empty question");
    LQ_CHECK (isInvalid ("{\"kind\":\"question\",\"text\":\"x\",\"quick_replies\":\"a\"}"), "quick_replies not an array");
    LQ_CHECK (isInvalid ("{\"kind\":\"question\",\"text\":\"x\",\"quick_replies\":[1]}"), "quick reply not a string");
    LQ_CHECK (isInvalid ("{\"kind\":\"proposal\",\"explanation\":\"x\"}"), "proposal without bands");
    LQ_CHECK (isInvalid ("{\"kind\":\"question\",\"text\":\"x\",\"extra\":" + juce::String::repeatedString ("[", 33) + juce::String::repeatedString ("]", 33) + "}"),
              "nested deeper than 32 levels");
    LQ_CHECK (isInvalid ("{\"kind\":\"proposal\",\"explanation\":\"x\",\"bands\":{}}"), "bands not an array");
    const auto band = [] (const char* fields) { return juce::String ("{\"kind\":\"proposal\",\"explanation\":\"x\",\"bands\":[{") + fields + "}]}"; };
    LQ_CHECK (isInvalid (band ("\"type\":\"peak\",\"freq_hz\":100,\"gain_db\":1,\"q\":1")), "unknown band type");
    LQ_CHECK (isInvalid (band ("\"type\":\"bell\",\"gain_db\":1,\"q\":1")), "missing frequency");
    LQ_CHECK (isInvalid (band ("\"type\":\"bell\",\"freq_hz\":\"100\",\"gain_db\":1,\"q\":1")), "frequency as text");
    LQ_CHECK (isInvalid (band ("\"type\":\"bell\",\"freq_hz\":100,\"gain_db\":true,\"q\":1")), "gain as bool");
    LQ_CHECK (isInvalid (band ("\"type\":\"bell\",\"freq_hz\":100,\"gain_db\":1,\"q\":1,\"bypass\":1")), "bypass not a bool");
    LQ_CHECK (isInvalid (band ("\"type\":\"bell\",\"freq_hz\":100,\"gain_db\":1,\"q\":1,\"candidate_id\":3")), "candidate_id not a string");

    // Optional fields may be absent or null.
    const auto ok = assist::parseReply (200, band ("\"type\":\"bell\",\"freq_hz\":100,\"gain_db\":1,\"q\":1,\"candidate_id\":null"));
    const auto* p = std::get_if<assist::Proposal> (&ok);
    LQ_CHECK (p != nullptr && p->bands.size() == 1 && ! p->bands[0].settings.bypassed && p->bands[0].reason.empty()
                  && p->quickReplies.empty(), "minimal band accepted");
}
