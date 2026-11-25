// iaq8_analyze <file.wav> <source>: prints the analysis JSON of the whole file
// on stdout. Exit 0 on success, 2 on a bad file or an unknown source.

#include "analysis/TrackAnalyzer.h"
#include "assist/Contract.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>
#include <memory>

int main (int argc, char** argv)
{
    const auto usage = [] { std::fprintf (stderr, "usage: iaq8_analyze <file.wav> <voice|acoustic_guitar|electric_guitar|bass|drums|mix|other>\n"); return 2; };
    if (argc != 3)
        return usage();

    const auto source = iaq8::assist::sourceFromString (argv[2]);
    if (! source)
        return usage();

    const juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0 || reader->sampleRate <= 0)
    {
        std::fprintf (stderr, "iaq8_analyze: cannot read audio from '%s'\n", argv[1]);
        return usage();
    }

    const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, 0x7fffffff);
    const auto numChannels = (int) std::min (reader->numChannels, 2u); // first two channels
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, length);
    if (! reader->read (&buffer, 0, length, 0, true, true))
    {
        std::fprintf (stderr, "iaq8_analyze: read failed for '%s'\n", argv[1]);
        return usage();
    }

    std::vector<std::vector<float>> channels;
    for (int c = 0; c < numChannels; ++c)
        channels.emplace_back (buffer.getReadPointer (c), buffer.getReadPointer (c) + length);

    const auto summary = iaq8::analysis::analyse (channels, reader->sampleRate, *source);
    std::puts (iaq8::assist::toJsonText (summary).toRawUTF8());
    return 0;
}
