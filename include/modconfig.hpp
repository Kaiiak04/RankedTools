#pragma once

#include "config-utils/shared/config-utils.hpp"

#include <string>

DECLARE_CONFIG(ModConfig) {
    CONFIG_VALUE(BeatLeaderPlayerId, std::string, "BeatLeader player ID", "");
    CONFIG_VALUE(ScoreSaberPlayerId, std::string, "ScoreSaber player ID", "");
    CONFIG_VALUE(BeatLeaderPlayerName, std::string, "Selected BeatLeader account", "");
    CONFIG_VALUE(ScoreSaberPlayerName, std::string, "Selected ScoreSaber account", "");
    CONFIG_VALUE(MaxTracksPerPlaylist, int, "Tracks per playlist", 16);
    CONFIG_VALUE(MinClearRate, int, "Min Clear Rate", 50,
                 "Minimum rough clear estimate for unplayed charts. 0% ranks by PP gain alone; higher values are more cautious.");
    CONFIG_VALUE(SimpleMode, bool, "Simple Mode", false,
                 "Create one PP Gain playlist per leaderboard, combining unplayed charts and improvements. Applies on refresh.");
    CONFIG_VALUE(RecordLocalAttempts, bool, "Record local attempts", true,
                 "Log solo results under the configured player IDs. Use your own IDs; disable when viewing another player.");
    CONFIG_VALUE(ImportAttemptHistory, bool, "Import attempt history", true,
                 "Use available BeatLeader and ScoreSaber clear/failure history. Cached and local evidence remains available if an API fails.");
    CONFIG_VALUE(EnableBeatLeader, bool, "Create BeatLeader playlists", true);
    CONFIG_VALUE(EnableScoreSaber, bool, "Create ScoreSaber playlists", true);
    CONFIG_VALUE(EnableClanPlaylist, bool, "Create BeatLeader clan playlist", true);
    CONFIG_VALUE(ClanPlaylistAutoStarFilter, bool, "Auto stars (match BeatLeader Not Played)", true);
    CONFIG_VALUE(ClanPlaylistMinStars, double, "Clan playlist minimum stars", 0.0);
    CONFIG_VALUE(ClanPlaylistMaxStars, double, "Clan playlist maximum stars", 20.0);
    CONFIG_VALUE(ClanPlaylistAllClans, bool, "Include all clans (off = main clan)", false);
};
