#pragma once

#include "dsp/EqCore.h"

#include <array>

namespace labq8::dsp
{
// E4b: the three banks and the STEREO <-> MID/SIDE processing mode.
enum class StereoBank { stereo = 0, mid = 1, side = 2 };
enum class StereoMode { stereo = 0, midSide = 1 };

// Owns three independent EqCore instances (each with its own eight bands,
// filter states, ramps, crossfades, detectors and ballistics; nothing is
// shared) and the two signal paths built from them:
//
//   STEREO path  : the STEREO core on L/R in place (1 or 2 channels). With
//                  the mode at Stereo and no transition this is exactly the
//                  call the processor made before E4b, so STEREO is bit for
//                  bit the c9ea214 / UI-2c sound.
//   MID/SIDE path: M = (L+R)/2, S = (L-R)/2 (dsp::midside), the MID core on
//                  M and the SIDE core on S (one channel each; every
//                  detector reads its own bank's input), L = M+S, R = M-S.
//                  On a mono bus (1 channel) M is the signal and S is 0:
//                  the MID core processes the channel in place and the SIDE
//                  bank has no signal and no effect.
//
// Flat MID/SIDE path. When both the MID and SIDE cores are idle (every band
// an exact identity, no dynamics running) the path does not code at all and
// the buffer is left untouched: bit-exact transparency. The decision to
// enter it is taken only on an absolute grid of flatGridSamples counted from
// prepare(), from the cores' state at that sample, so the output does not
// depend on how the host splits its blocks; it is left as soon as a core is
// no longer idle (that only happens when targets change, at a block start).
//
// Standby (E4b, measured: docs/MIDSIDE_E4B.md). The path that is not heard
// keeps running on a copy of the input, so both paths are always in their
// steady state: a mode change never starts filters from a zero state or
// detectors from zero reduction, and needs no precalentamiento. A flat path
// costs almost nothing (idle cores only advance their control grid).
//
// Mode change: a linear crossfade of fadeTimeSeconds from the heard path to
// the other one, out = a + w (b - a) in double, w = k / N per sample (a
// convex combination, like EqBand's type change). A request back during the
// fade reverses it in place (w -> 1 - w), so the output never jumps. The
// mode is read at every call; a call is processed in chunks of at most
// chunkSamples with fixed-size internal buffers.
//
// Real-time safe: no allocation, locks or waits; fixed-size state.
class StereoModeEngine
{
public:
    static constexpr int numBanks = 3;
    static constexpr int maxChannels = 2;
    static constexpr int chunkSamples = 256;
    static constexpr int flatGridSamples = 64;
    static constexpr double fadeTimeSeconds = 0.02;

    static_assert (EqCore::maxChannels == maxChannels);

    // fadeSeconds: measurements only (tests/bench); the plugin uses the default.
    void prepare (double sampleRate, int numChannels, double fadeSeconds = fadeTimeSeconds) noexcept;
    // Every core back to its targets with zero state; the requested mode
    // becomes the heard one at once (no fade).
    void reset() noexcept;

    // Real-time safe. Out-of-range bands are ignored.
    void setMode (StereoMode mode) noexcept { requestedMode = mode; }
    void setBandTargets (StereoBank bank, int bandIndex, const BandSettings& settings) noexcept;
    void setBandDynamics (StereoBank bank, int bandIndex, const DynamicSettings& settings) noexcept;

    // Real-time safe, in place. Channels beyond maxChannels are left alone.
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    StereoMode getRequestedMode() const noexcept { return requestedMode; }
    // The path heard (outgoing one during a fade).
    StereoMode getHeardMode() const noexcept { return heardMode; }
    bool isFading() const noexcept { return fading; }
    int getFadeLengthSamples() const noexcept { return fadeLength; }
    int getFadePosition() const noexcept { return fadePosition; }
    // Whether the last MID/SIDE segment was processed by the flat path.
    bool isMidSideFlat() const noexcept { return midSideFlat; }

    const EqCore& getCore (StereoBank bank) const noexcept;

private:
    static StereoMode other (StereoMode m) noexcept { return m == StereoMode::stereo ? StereoMode::midSide : StereoMode::stereo; }

    EqCore& core (StereoBank bank) noexcept { return cores[(size_t) bank]; }
    void processPath (StereoMode mode, float* const* channels, int numChannels, int numSamples) noexcept;
    void processMidSide (float* const* channels, int numChannels, int numSamples) noexcept;

    std::array<EqCore, numBanks> cores;
    StereoMode requestedMode = StereoMode::stereo, heardMode = StereoMode::stereo;
    bool fading = false;
    int fadePosition = 0, fadeLength = 1;
    int numPreparedChannels = maxChannels;

    int flatGridPosition = 0; // samples since the last flat-grid point, [0, flatGridSamples)
    bool midSideFlat = true;

    using Buffer = std::array<float, chunkSamples>;
    std::array<Buffer, maxChannels> copy {}; // input of the path not heard (standby / incoming)
    Buffer midBuffer {}, sideBuffer {};
};
} // namespace labq8::dsp
