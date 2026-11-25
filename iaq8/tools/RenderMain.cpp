// iaq8_render <input.wav> <reply.json> <output_prefix> [--source <source>] [--analysis <analysis.json>]
//
// Offline before/after render of a proposal. <reply.json> is a contract reply
// of kind "proposal" or a full request (its "proposal" object is used). The
// bands go through assist::sanitize (source by --source, default "other"; the
// candidates of --analysis, an iaq8_analyze output, so a notch on a resonance
// candidate keeps its Q up to 18 as in the plugin; without it, no analysis),
// then through labq8's EqCore with whole-file K-weighted loudness matching
// (+-6 dB, as in the plugin). Writes <prefix>_antes.wav (the input, first two
// channels) and <prefix>_despues.wav, both 24-bit at the input sample rate;
// prints the adjustment lines, "compensation_db=", "peak_before=",
// "peak_after=" (linear, before headroom) and "headroom_db=" on stdout. When a
// peak would pass 0.98, one common headroom gain is applied to BOTH files so
// neither clips: _antes is then not level-identical to the source (a fair A/B).
// Exit 0 on success, 2 on bad arguments, files or reply.

#include "analysis/OfflineRender.h"
#include "assist/Contract.h"
#include "assist/ProposalLimits.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>
#include <memory>
#include <optional>
#include <string>

namespace
{
int fail (const char* message)
{
    std::fprintf (stderr, "iaq8_render: %s\n", message);
    std::fprintf (stderr, "usage: iaq8_render <input.wav> <reply.json> <output_prefix> [--source voice|acoustic_guitar|electric_guitar|bass|drums|mix|other] [--analysis <analysis.json>]\n");
    return 2;
}

bool writeWav (const juce::File& file, const std::vector<std::vector<float>>& channels, double sampleRate)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels ((int) channels.size())
                                                   .withBitsPerSample (24));
    if (writer == nullptr)
        return false;
    std::vector<const float*> ptrs;
    for (const auto& ch : channels)
        ptrs.push_back (ch.data());
    return writer->writeFromFloatArrays (ptrs.data(), (int) ptrs.size(), (int) channels[0].size());
}
} // namespace

int main (int argc, char** argv)
{
    auto source = iaq8::assist::Source::other;
    std::optional<iaq8::analysis::AnalysisSummary> analysis;
    std::vector<const char*> positional;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string (argv[i]) == "--source")
        {
            const auto s = i + 1 < argc ? iaq8::assist::sourceFromString (argv[i + 1]) : std::nullopt;
            if (! s)
                return fail ("--source needs a valid source");
            source = *s;
            ++i;
        }
        else if (std::string (argv[i]) == "--analysis")
        {
            if (i + 1 >= argc)
                return fail ("--analysis needs a file");
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (argv[i + 1]);
            if (! file.existsAsFile())
                return fail ("cannot read the analysis file");
            analysis = iaq8::assist::analysisCandidatesFromJson (file.loadFileAsString());
            if (! analysis)
                return fail ("the analysis file has no valid candidates");
            ++i;
        }
        else
            positional.push_back (argv[i]);
    }
    if (positional.size() != 3)
        return fail ("expected 3 arguments");

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto input = cwd.getChildFile (positional[0]);
    const auto replyFile = cwd.getChildFile (positional[1]);
    if (! replyFile.existsAsFile())
        return fail ("cannot read the reply file");

    // Reply: a proposal reply as is, else a request with a "proposal" object.
    const auto text = replyFile.loadFileAsString();
    auto reply = iaq8::assist::parseReply (200, text);
    if (! std::holds_alternative<iaq8::assist::Proposal> (reply))
    {
        const auto parsed = juce::JSON::parse (text);
        const auto* obj = parsed.getDynamicObject();
        const auto proposal = obj != nullptr ? obj->getProperty ("proposal") : juce::var();
        const auto* proposalObj = proposal.getDynamicObject();
        if (proposalObj == nullptr || ! proposalObj->hasProperty ("bands"))
            return fail (std::holds_alternative<iaq8::assist::Question> (reply)
                             ? "the reply is a question, not a proposal"
                             : "no proposal found in the reply file");
        auto wrapped = new juce::DynamicObject();
        wrapped->setProperty ("kind", "proposal");
        wrapped->setProperty ("explanation", "");
        wrapped->setProperty ("bands", proposalObj->getProperty ("bands"));
        wrapped->setProperty ("quick_replies", juce::var (juce::Array<juce::var>()));
        reply = iaq8::assist::parseReply (200, juce::JSON::toString (juce::var (wrapped), true));
        if (! std::holds_alternative<iaq8::assist::Proposal> (reply))
            return fail ("the proposal in the request is not valid");
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (input));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0 || reader->sampleRate <= 0)
        return fail ("cannot read audio from the input file");

    const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, 0x7fffffff);
    const auto numChannels = (int) std::min (reader->numChannels, 2u); // first two channels
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, length);
    if (! reader->read (&buffer, 0, length, 0, true, true))
        return fail ("read failed");
    std::vector<std::vector<float>> channels;
    for (int c = 0; c < numChannels; ++c)
        channels.emplace_back (buffer.getReadPointer (c), buffer.getReadPointer (c) + length);

    iaq8::assist::LimitReport report;
    const auto bands = iaq8::assist::sanitize (std::get<iaq8::assist::Proposal> (reply), source, analysis ? &*analysis : nullptr, report);
    const auto result = iaq8::analysis::render (channels, reader->sampleRate, bands);

    const juce::String prefix (positional[2]);
    if (! writeWav (cwd.getChildFile (prefix + "_antes.wav"), result.before, reader->sampleRate)
        || ! writeWav (cwd.getChildFile (prefix + "_despues.wav"), result.after, reader->sampleRate))
        return fail ("cannot write the output files");

    for (const auto& line : report.adjustments)
        std::printf ("%s\n", line.c_str());
    std::printf ("compensation_db=%.2f\n", result.compensationDb);
    std::printf ("peak_before=%.4f\npeak_after=%.4f\nheadroom_db=%.2f\n", result.peakBefore, result.peakAfter, result.headroomDb);
    return 0;
}
