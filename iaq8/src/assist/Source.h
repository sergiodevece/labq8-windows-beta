#pragma once

namespace iaq8::assist
{
// What the user says the track is. Chooses the analysis fit range and which
// detectors apply (TrackAnalyzer) and, later, the per-source safety limits.
// String conversion lives with the contract (later task), not here.
enum class Source { voice, acousticGuitar, electricGuitar, bass, drums, mix, other };
} // namespace iaq8::assist
