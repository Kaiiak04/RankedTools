#pragma once

#include "recommendation.hpp"
#include <string>

namespace rankedpractice {

// Capture on the main thread. The refresh worker owns its copy for the whole run.
struct RefreshSettings {
    std::string beatLeaderPlayerId;
    std::string scoreSaberPlayerId;
    std::string accSaberPlayerId;
    std::string accSaberCategory{"overall"};
    int requiredSyncScoreboards{2};
    int limit{16};
    double minClearRate{0.50};
    bool separateNewAndRepeatTracks{false};
    bool importAttempts{true};
    bool enableBeatLeader{true};
    bool enableScoreSaber{true};
    bool enableAccSaber{false};
    bool enableClanPlaylist{true};
    bool enableSyncPlaylist{false};
    bool legacyClanPlaylists{false};
    bool allClans{false};
    bool beatLeaderModifiers{false};
};

} // namespace rankedpractice
