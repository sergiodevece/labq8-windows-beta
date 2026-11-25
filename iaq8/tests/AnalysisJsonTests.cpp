// Exact float comparisons are intentional (rounded values, fallbacks).
#pragma clang diagnostic ignored "-Wfloat-equal"

// Tests of the analysis JSON (the contract the server/AI consumes) and of the
// Source <-> string mapping.

#include "AnalyzerSignals.h"
#include "TestHarness.h"

#include "analysis/TrackAnalyzer.h"
#include "assist/Contract.h"
#include "dsp/BiquadDesign.h"

#include <cmath>
#include <cctype>
#include <cstdlib>
#include <limits>

using namespace iaq8;
using assist::Source;
using labq8::test::str;

namespace
{
constexpr double kFs = 48000.0;
constexpr size_t kTwelve = 576000; // 12 s

// The Task 7 excess: +6 dB bell at 250 Hz, Q1, on pink noise.
analysis::AnalysisSummary excessSummary()
{
    auto x = test::makePink (kTwelve, kFs, 2);
    test::applyBiquad (x, labq8::dsp::designBell (kFs, 250.0, 6.0, 1.0));
    return analysis::analyse ({ x, x }, kFs, Source::voice);
}

bool isRounded (const juce::var& v, double step)
{
    const double d = v;
    return std::abs (d / step - std::round (d / step)) < 1.0e-6;
}

bool hasKeys (const juce::var& v, std::initializer_list<const char*> keys)
{
    auto* o = v.getDynamicObject();
    if (o == nullptr || o->getProperties().size() != (int) keys.size())
        return false;
    for (auto k : keys)
        if (! o->hasProperty (juce::Identifier (k)))
            return false;
    return true;
}
} // namespace

LQ_TEST ("analysis json: shape and rounding")
{
    const auto s = excessSummary();
    const auto j = assist::toJson (s);

    LQ_CHECK (hasKeys (j, { "version", "sample_rate", "seconds", "channels", "level", "stereo", "spectrum", "trend", "candidates", "enough_signal" }),
              "top-level keys");
    LQ_CHECK ((int) j["version"] == 1 && (double) j["sample_rate"] == 48000.0 && (int) j["channels"] == 2, "version, rate, channels");
    LQ_CHECK (std::abs ((double) j["seconds"] - 12.0) < 1e-9, str ("seconds ", (double) j["seconds"]));
    LQ_CHECK (j["enough_signal"].isBool() && (bool) j["enough_signal"], "enough_signal");

    LQ_CHECK (hasKeys (j["level"], { "rms_dbfs", "peak_dbfs", "crest_db" }), "level keys");
    LQ_CHECK (isRounded (j["level"]["rms_dbfs"], 0.1) && isRounded (j["level"]["peak_dbfs"], 0.1) && isRounded (j["level"]["crest_db"], 0.1), "level rounded to 0.1 dB");

    LQ_CHECK (hasKeys (j["stereo"], { "is_mono", "side_to_mid_db", "correlation" }), "stereo keys");
    LQ_CHECK (j["stereo"]["is_mono"].isBool() && (bool) j["stereo"]["is_mono"], "identical channels are mono");
    LQ_CHECK (isRounded (j["stereo"]["side_to_mid_db"], 0.1) && isRounded (j["stereo"]["correlation"], 0.01), "stereo rounded");

    const auto sp = j["spectrum"];
    LQ_CHECK (hasKeys (sp, { "start_hz", "step_oct", "db" }), "spectrum keys");
    LQ_CHECK ((double) sp["start_hz"] == 20.0 && std::abs ((double) sp["step_oct"] - 0.1667) < 1e-9, "spectrum grid");
    const auto* db = sp["db"].getArray();
    LQ_CHECK (db != nullptr && db->size() == 60, "60 spectrum values");
    if (db != nullptr)
        for (const auto& v : *db)
            LQ_CHECK ((v.isDouble() || v.isInt()) && isRounded (v, 0.1), str ("spectrum value ", (double) v));

    LQ_CHECK (hasKeys (j["trend"], { "slope_db_per_oct", "fit_from_hz", "fit_to_hz" }), "trend keys");
    LQ_CHECK ((double) j["trend"]["fit_from_hz"] == 100.0 && (double) j["trend"]["fit_to_hz"] == 12000.0, "voice fit range");
    LQ_CHECK (isRounded (j["trend"]["slope_db_per_oct"], 0.1), "slope rounded");

    const auto* cs = j["candidates"].getArray();
    LQ_CHECK (cs != nullptr && cs->size() >= 1, "at least one candidate");
    bool foundExcess = false;
    if (cs != nullptr)
        for (int i = 0; i < cs->size(); ++i)
        {
            const auto& c = (*cs)[i];
            LQ_CHECK (hasKeys (c, { "id", "kind", "freq_hz", "magnitude_db", "width_oct" }), "candidate keys");
            LQ_CHECK (c["id"].toString() == "c" + juce::String (i + 1), "candidate id");
            LQ_CHECK (isRounded (c["freq_hz"], 1.0) && isRounded (c["magnitude_db"], 0.1) && isRounded (c["width_oct"], 0.01), "candidate rounded");
            foundExcess = foundExcess || (c["kind"].toString() == "excess" && std::abs (std::log2 ((double) c["freq_hz"] / 250.0)) < 0.2);
        }
    LQ_CHECK (foundExcess, "excess near 250 Hz");

    // Round trip through text: same structure.
    const auto parsed = juce::JSON::parse (assist::toJsonText (s));
    LQ_CHECK (parsed.isObject() && parsed["spectrum"]["db"].size() == 60 && parsed["candidates"].size() == cs->size(), "parses back");

    // Every kind has its contract name.
    using K = analysis::CandidateKind;
    analysis::AnalysisSummary k;
    int n = 0;
    for (auto kind : { K::excess, K::deficit, K::resonance, K::lowRumble, K::sibilance })
        k.candidates.push_back ({ "c" + std::to_string (++n), kind, 100, 3, 0.5 });
    const auto kj = assist::toJson (k);
    const juce::StringArray names { "excess", "deficit", "resonance", "low_rumble", "sibilance" };
    for (int i = 0; i < 5; ++i)
        LQ_CHECK (kj["candidates"][i]["kind"].toString() == names[i], str ("kind ", i));

    // Optional: regenerate the contract example (IAQ8_WRITE_FIXTURES=1).
    if (std::getenv ("IAQ8_WRITE_FIXTURES") != nullptr)
        juce::File (IAQ8_TEST_FIXTURES_DIR).getChildFile ("contract/analysis_voice.json")
            .replaceWithText (assist::toJsonText (s) + "\n");
}

LQ_TEST ("analysis json: compact")
{
    analysis::AnalysisSummary s = excessSummary();
    s.candidates.clear();
    for (int i = 0; i < 8; ++i)
        s.candidates.push_back ({ "c" + std::to_string (i + 1), analysis::CandidateKind::resonance,
                                  1234.5 + 100.0 * i, -12.34 - i, 0.1234 });
    const auto text = assist::toJsonText (s);
    ctx.note (str (text.getNumBytesAsUTF8(), " bytes"));
    LQ_CHECK (text.getNumBytesAsUTF8() < 2500, str (text.getNumBytesAsUTF8(), " bytes"));
    LQ_CHECK (! text.containsChar ('\n'), "one line");

}

LQ_TEST ("analysis json: committed example equals the current output")
{
    // Regenerate with IAQ8_WRITE_FIXTURES=1 (test "shape and rounding").
    const auto example = juce::JSON::parse (juce::File (IAQ8_TEST_FIXTURES_DIR).getChildFile ("contract/analysis_voice.json"));
    LQ_CHECK (example.isObject(), "fixtures/contract/analysis_voice.json parses");
    const auto current = juce::JSON::parse (assist::toJsonText (excessSummary()));
    LQ_CHECK (juce::JSON::toString (example, true, 4) == juce::JSON::toString (current, true, 4),
              "fixture equals the output of the 250 Hz excess case (regenerate with IAQ8_WRITE_FIXTURES=1)");
}

LQ_TEST ("analysis json: text has at most 4 decimals and survives non-finite values")
{
    const auto countLongDecimals = [] (const std::string& t)
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
    };

    const auto text = assist::toJsonText (excessSummary()).toStdString();
    LQ_CHECK (countLongDecimals (text) == 0, "no number with more than 4 decimals");
    LQ_CHECK (text.find ("e-") == std::string::npos && text.find ("-0.0,") == std::string::npos, "no exponent or negative zero");

    // The scanner itself detects the default serialisation's noise.
    LQ_CHECK (countLongDecimals ("[70.599999999999994]") == 1, "scanner detects long decimals");

    auto s = excessSummary();
    const auto nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    s.rmsDbfs = nan; s.peakDbfs = -inf; s.crestDb = inf; s.spectrumDb[3] = nan; s.slopeDbPerOct = nan;
    s.correlation = nan; s.seconds = inf; s.candidates[0].magnitudeDb = nan; s.candidates[0].freqHz = inf;
    const auto j = juce::JSON::parse (assist::toJsonText (s));
    LQ_CHECK (j.isObject(), "still valid JSON");
    LQ_CHECK ((double) j["level"]["rms_dbfs"] == -120.0 && (double) j["level"]["peak_dbfs"] == -120.0, "non-finite levels read -120");
    LQ_CHECK ((double) j["level"]["crest_db"] == 0.0 && (double) j["trend"]["slope_db_per_oct"] == 0.0, "non-finite crest and slope read 0");
    LQ_CHECK ((double) j["spectrum"]["db"][3] == -120.0, "non-finite spectrum value reads -120");
    LQ_CHECK ((double) j["stereo"]["correlation"] == 0.0 && (double) j["seconds"] == 0.0, "other non-finite numbers read 0");
    LQ_CHECK ((double) j["candidates"][0]["magnitude_db"] == 0.0 && (double) j["candidates"][0]["freq_hz"] == 0.0, "candidate non-finite values read 0");
}

LQ_TEST ("contract: source strings round-trip")
{
    const char* expected[] = { "voice", "acoustic_guitar", "electric_guitar", "bass", "drums", "mix", "other" };
    int i = 0;
    for (auto src : { Source::voice, Source::acousticGuitar, Source::electricGuitar, Source::bass, Source::drums, Source::mix, Source::other })
    {
        LQ_CHECK (std::string (assist::toString (src)) == expected[i], str ("name of ", i));
        const auto back = assist::sourceFromString (expected[i]);
        LQ_CHECK (back.has_value() && *back == src, str ("round trip ", expected[i]));
        ++i;
    }
    LQ_CHECK (! assist::sourceFromString ("").has_value() && ! assist::sourceFromString ("Voice").has_value()
                  && ! assist::sourceFromString ("guitar").has_value(), "unknown strings are rejected");
}
