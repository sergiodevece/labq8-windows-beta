#include "analysis/OfflineRender.h"

#include "dsp/LoudnessMatcher.h"

#include <algorithm>
#include <cmath>

namespace iaq8::analysis
{
RenderResult render (const std::vector<std::vector<float>>& channels, double sampleRate, const dsp::BandSet& bands)
{
    RenderResult result;
    result.after = channels;
    const auto numChannels = (int) std::min<size_t> (channels.size(), labq8::dsp::EqCore::maxChannels);
    if (numChannels == 0)
        return result;

    labq8::dsp::EqCore core;
    core.prepare (sampleRate, numChannels);
    for (size_t i = 0; i < bands.size(); ++i)
        core.setBandTargets ((int) i, bands[i]);

    // Pre-roll of silence (0.2 s, ten times the 20 ms ramps/crossfades): state
    // stays zero but the bands reach their targets before the first sample.
    constexpr int block = 512;
    const auto preroll = (int) std::lround (0.2 * sampleRate);
    std::vector<float> zeros ((size_t) block, 0.0f);
    std::vector<float*> ptrs ((size_t) numChannels);
    std::vector<std::vector<float>> scratch ((size_t) numChannels);
    for (int done = 0; done < preroll; done += block)
    {
        const auto n = std::min (block, preroll - done);
        for (int c = 0; c < numChannels; ++c)
        {
            scratch[(size_t) c].assign ((size_t) n, 0.0f);
            ptrs[(size_t) c] = scratch[(size_t) c].data();
        }
        core.process (ptrs.data(), numChannels, n);
    }

    size_t length = 0;
    for (const auto& ch : result.after)
        length = std::max (length, ch.size());
    for (size_t pos = 0; pos < length; pos += (size_t) block)
    {
        const auto n = (int) std::min<size_t> ((size_t) block, length - pos);
        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = result.after[(size_t) c];
            ch.resize (length, 0.0f);
            ptrs[(size_t) c] = ch.data() + pos;
        }
        core.process (ptrs.data(), numChannels, n);
    }
    for (int c = 0; c < numChannels; ++c)
        result.after[(size_t) c].resize (channels[(size_t) c].size());

    constexpr double silence = 1.0e-7; // -70 dB, as LoudnessMatcher::silenceDb
    const auto msBefore = dsp::LoudnessMatcher::wholeSignalMeanSquare (channels, sampleRate);
    const auto msAfter = dsp::LoudnessMatcher::wholeSignalMeanSquare (result.after, sampleRate);
    if (msBefore >= silence && msAfter >= silence)
    {
        result.compensationDb = std::clamp (10.0 * std::log10 (msBefore / msAfter),
                                            -dsp::LoudnessMatcher::maxCompensationDb,
                                            dsp::LoudnessMatcher::maxCompensationDb);
        const auto g = (float) std::pow (10.0, result.compensationDb / 20.0);
        for (auto& ch : result.after)
            for (auto& v : ch)
                v *= g;
    }
    return result;
}
} // namespace iaq8::analysis
