#pragma once
#include "recommendation.hpp"

namespace rankedpractice {
// Current PPv4.1 formula, for difficulties carrying BeatLeader's noteSpan marker.
double BeatLeaderPpv4(double accuracy, const Difficulty& difficulty, double scoringMultiplier = 1.0);
std::optional<Difficulty> BeatLeaderDifficultyAtSpeed(const Difficulty& difficulty, BeatLeaderSpeed speed);
}
