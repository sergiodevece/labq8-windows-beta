#include "assist/Contract.h"

#include <array>
#include <cmath>

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
double round (double v, double scale) { return std::isfinite (v) ? std::round (v * scale) / scale + 0.0 : 0.0; } // + 0.0: no "-0.0"
juce::var r1 (double v) { return round (v, 10.0); }   // dB
juce::var r0 (double v) { return round (v, 1.0); }    // Hz
juce::var r2 (double v) { return round (v, 100.0); }  // octaves, correlation, seconds
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
    level->setProperty ("crest_db", r1 (a.crestDb));
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
    spectrum->setProperty ("step_oct", round (analysis::AnalysisSummary::spectrumStepOct, 10000.0));
    spectrum->setProperty ("db", db);
    root->setProperty ("spectrum", juce::var (spectrum));

    auto* trend = object();
    trend->setProperty ("slope_db_per_oct", r1 (a.slopeDbPerOct));
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
        o->setProperty ("magnitude_db", r1 (c.magnitudeDb));
        o->setProperty ("width_oct", r2 (c.widthOct));
        candidates.add (juce::var (o));
    }
    root->setProperty ("candidates", candidates);
    root->setProperty ("enough_signal", a.enoughSignal);
    return juce::var (root);
}
} // namespace iaq8::assist
