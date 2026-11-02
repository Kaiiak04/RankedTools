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
                 "Minimum rough clear estimate for unplayed personal charts and personalized clan suggestions. Legacy clan mode uses its direct clearability cutoff as maximum stars. At 0%, chance adjustment and the legacy star cap are disabled.");
    CONFIG_VALUE(SimpleMode, bool, "Simple Mode", false,
                 "Create one PP Gain playlist per leaderboard, combining unplayed charts and improvements. Applies on refresh.");
    CONFIG_VALUE(RecordLocalAttempts, bool, "Record local attempts", true,
                 "Log solo results under the configured player IDs. Use your own IDs; disable when viewing another player.");
    CONFIG_VALUE(ImportAttemptHistory, bool, "Import attempt history", true,
                 "Use available BeatLeader and ScoreSaber clear/failure history. Cached and local evidence remains available if an API fails.");
    CONFIG_VALUE(EnableBeatLeader, bool, "Create BeatLeader playlists", true);
    CONFIG_VALUE(EnableScoreSaber, bool, "Create ScoreSaber playlists", true);
    CONFIG_VALUE(EnableClanPlaylist, bool, "Create BeatLeader clan playlist", true);
    CONFIG_VALUE(EnableSyncPlaylist, bool, "Create Sync playlist", false);
    CONFIG_VALUE(LegacyClanPlaylists, bool, "Legacy Clan Playlists", false,
                 "Use legacy clan-gap ordering instead of personalized clan impact. Automatically caps maximum stars using Min Clear Rate, with no lower star limit. Omits charts whose eligible PB meets or exceeds predicted PP. Applies on refresh.");
    CONFIG_VALUE(ClanPlaylistAllClans, bool, "Include all clans (off = main clan)", false);
};
