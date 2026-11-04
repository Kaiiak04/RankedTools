#include "refresh_engine.hpp"
#include "playlist_writer.hpp"
#include "refresh_tasks.hpp"
#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace rankedpractice {
namespace {
class RefreshRun {
    const RefreshObserver &observer;
    std::atomic<bool> filesChanged{false};
    void Log(const std::string &text) {
        if (observer.log)
            observer.log(text);
    }
    void SetStatus(const std::string &text) {
        if (observer.status)
            observer.status(text);
        Log(text);
    }
    void PublishRecommendationPreview(const std::string &service, ListKind kind, const std::vector<MapEntry> &entries) {
        if (observer.playlist)
            observer.playlist(service, kind, entries);
    }
    void PublishClanPreviews(const std::vector<ClanPlaylistRecommendations> &lists) {
        if (observer.clans)
            observer.clans(lists);
    }
    void PublishAbilityPreview(const std::string &service, const std::vector<MapEntry> &scores,
                               const std::string &error) {
        if (observer.ability)
            observer.ability(service, scores, error);
    }

  public:
    explicit RefreshRun(const RefreshObserver &value) : observer(value) {}
    std::vector<ListKind> PlaylistKinds(bool simpleMode) {
        return simpleMode ? std::vector{ListKind::Combined} : std::vector{ListKind::NotPlayed, ListKind::ToImprove};
    }

    bool PruneDisabledServiceLayout(const std::string &service, bool simpleMode) {
        std::vector<std::string> allowed;
        for (auto kind : PlaylistKinds(simpleMode))
            allowed.push_back(PlaylistFileName(service, kind));
        std::string error;
        if (PruneLeaderboardPlaylists(service, allowed, error)) {
            filesChanged.store(true);
            return true;
        }
        SetStatus(service + " obsolete playlist cleanup failed: " + error);
        return false;
    }

    bool RefreshPlaylist(const std::string &service, const std::string &playerId, int limit, double minClearRate,
                         bool simpleMode, ScoreHistory &history, StarRange *beatLeaderNotPlayedRange = nullptr) {
        if (playerId.empty()) {
            PublishAbilityPreview(service, {}, "Enter a player ID in RankedTools settings.");
            SetStatus(service + " is enabled, but its player ID is empty.");
            return false;
        }
        const auto kinds = PlaylistKinds(simpleMode);
        std::vector<std::vector<MapEntry>> playlists(kinds.size());
        std::string error;
        // Finish fetching the replacement layout before writing or pruning files.
        for (size_t i = 0; i < kinds.size(); ++i) {
            const bool fetched = FetchRecommendations(service, playerId, kinds[i], limit, playlists[i], error,
                                                      minClearRate, beatLeaderNotPlayedRange, &history);
            if (i == 0)
                PublishAbilityPreview(service, history.scores,
                                      history.loaded ? "" : "Profile could not be loaded: " + error);
            if (!fetched) {
                SetStatus(PlaylistTitle(service, kinds[i]) + " failed: " + error);
                return false;
            }
        }
        std::vector<std::string> expected;
        for (size_t i = 0; i < kinds.size(); ++i) {
            if (!WritePlaylist(service, kinds[i], playlists[i], error)) {
                SetStatus(service + " playlist could not be saved: " + error);
                return false;
            }
            filesChanged.store(true);
            expected.push_back(PlaylistFileName(service, kinds[i]));
            PublishRecommendationPreview(service, kinds[i], playlists[i]);
            SetStatus(PlaylistTitle(service, kinds[i]) + " updated with " + std::to_string(playlists[i].size()) +
                      " songs.");
        }
        if (!PruneLeaderboardPlaylists(service, expected, error)) {
            SetStatus(service + " playlists were saved, but cleanup failed: " + error);
            return false;
        }
        filesChanged.store(true);
        return true;
    }

    bool RefreshClanPlaylist(const RefreshSettings &settings, ScoreHistory &history, std::string &iconWarnings) {
        const auto &playerId = settings.beatLeaderPlayerId;
        const int limit = settings.limit;
        const auto minClearRate = settings.minClearRate;
        if (playerId.empty()) {
            SetStatus("BeatLeader clan playlist is enabled, but the BeatLeader player ID is empty.");
            return false;
        }

        std::vector<ClanPlaylistRecommendations> clanPlaylists;
        std::string error;
        const auto allClans = settings.allClans;
        if (!FetchClanToConquer(playerId, allClans, limit, clanPlaylists, error, minClearRate, &history,
                                settings.legacyClanPlaylists)) {
            ClanPlaylistRecommendations failed;
            failed.error = error;
            PublishClanPreviews({failed});
            SetStatus("BeatLeader Maps to Conquer failed: " + error);
            return false;
        }

        std::vector<std::string> expectedFileNames;
        size_t totalSongs = 0;
        bool succeeded = true;
        for (auto &clanPlaylist : clanPlaylists) {
            const auto fileName = ClanPlaylistFileName(clanPlaylist.clanTag, allClans);
            expectedFileNames.push_back(fileName); // Preserve a failed current clan during cleanup.
            if (!clanPlaylist.error.empty()) {
                SetStatus("Clan " + clanPlaylist.clanTag + " failed: " + clanPlaylist.error);
                succeeded = false;
                continue;
            }
            if (!WriteClanPlaylist(clanPlaylist.clanTag, allClans, clanPlaylist.entries, error, clanPlaylist.image,
                                   clanPlaylist.hypothetical, clanPlaylist.legacy)) {
                clanPlaylist.error = error;
                succeeded = false;
                SetStatus("BeatLeader Maps to Conquer playlist for " + clanPlaylist.clanTag +
                          " could not be saved: " + error);
                continue;
            }
            filesChanged.store(true);
            if (clanPlaylist.cursorReady &&
                !SaveClanSearchState(playerId, clanPlaylist.clanTag, clanPlaylist.searchState, error))
                clanPlaylist.warning += " " + error;
            if (!clanPlaylist.warning.empty())
                iconWarnings += "\nClan " + clanPlaylist.clanTag + ": " + clanPlaylist.warning;
            totalSongs += clanPlaylist.entries.size();
            if (!clanPlaylist.iconWarning.empty()) {
                Log(clanPlaylist.iconWarning);
                iconWarnings += "\n" + clanPlaylist.iconWarning;
            }
        }
        PublishClanPreviews(clanPlaylists);
        // A layout/membership change can rename the prior output. Keep that prior
        // layout too when any clan failed, while still saving the successful clans.
        if (succeeded && !PruneClanPlaylists(expectedFileNames, error)) {
            SetStatus(
                "BeatLeader Maps to Conquer playlists were written, but obsolete files could not be cleaned up: " +
                error);
            return false;
        }
        if (succeeded)
            filesChanged.store(true);
        SetStatus("BeatLeader Maps to Conquer updated " + std::to_string(clanPlaylists.size()) +
                  " clan playlist(s) with " + std::to_string(totalSongs) + " songs total; " +
                  (settings.legacyClanPlaylists ? "legacy gap selection." : "automatic skill and impact selection."));
        return succeeded;
    }

    RefreshResult RefreshAll(const RefreshSettings &settings) {
        const auto started = std::chrono::steady_clock::now();
        const int limit = settings.limit;
        const auto minClearRate = settings.minClearRate;
        bool attempted = false;
        bool allSucceeded = true;
        StarRange beatLeaderNotPlayedRange;
        ScoreHistory beatLeaderHistory, scoreSaberHistory;
        std::string clanIconWarnings;
        beatLeaderHistory.importAttempts = scoreSaberHistory.importAttempts = settings.importAttempts;
        if (!settings.enableBeatLeader) {
            // Disabled services are not fetched. Remove only files from the other
            // layout so Simple Mode cannot leave old split playlists visible.
            allSucceeded = PruneDisabledServiceLayout("BeatLeader", settings.simpleMode) && allSucceeded;
        }
        if (!settings.enableScoreSaber) {
            allSucceeded = PruneDisabledServiceLayout("ScoreSaber", settings.simpleMode) && allSucceeded;
        }

        const auto timed = [&](const char *name, const std::function<bool()> &task) {
            const auto before = GetHttpStatistics();
            const auto start = std::chrono::steady_clock::now();
            const bool success = task();
            const auto after = GetHttpStatistics();
            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::ostringstream message;
            message << name << " refresh: " << elapsed << "s, " << after.requests - before.requests
                    << " HTTPS requests, " << after.connections - before.connections << " new connections, "
                    << after.seconds - before.seconds << "s in HTTPS.";
            Log(message.str());
            return success;
        };
        std::function<bool()> beatLeaderTask, scoreSaberTask;
        if (settings.enableBeatLeader || settings.enableClanPlaylist) {
            attempted = true;
            beatLeaderTask = [&] {
                bool success = true;
                if (settings.enableBeatLeader)
                    success = timed("BeatLeader", [&] {
                        return RefreshPlaylist("BeatLeader", settings.beatLeaderPlayerId, limit, minClearRate,
                                               settings.simpleMode, beatLeaderHistory, &beatLeaderNotPlayedRange);
                    });
                if (settings.enableClanPlaylist)
                    success =
                        timed("BeatLeader clans",
                              [&] { return RefreshClanPlaylist(settings, beatLeaderHistory, clanIconWarnings); }) &&
                        success;
                return success;
            };
        }
        if (settings.enableScoreSaber) {
            attempted = true;
            scoreSaberTask = [&] {
                return timed("ScoreSaber", [&] {
                    return RefreshPlaylist("ScoreSaber", settings.scoreSaberPlayerId, limit, minClearRate,
                                           settings.simpleMode, scoreSaberHistory);
                });
            };
        }
        const auto results = RunRefreshTasks(beatLeaderTask, scoreSaberTask);
        for (size_t i = 0; i < results.size(); ++i) {
            allSucceeded = results[i].succeeded && allSucceeded;
            if (!results[i].error.empty())
                SetStatus(std::string(i == 0 ? "BeatLeader" : "ScoreSaber") + " refresh stopped: " + results[i].error);
        }
        if (settings.enableSyncPlaylist) {
            attempted = true;
            const bool syncOk = timed("Sync", [&] {
                SetStatus("Sync: checking both ranked catalogs and posted-score histories...");
                std::vector<MapEntry> entries;
                std::string error;
                if (!FetchSyncPlaylist(settings.beatLeaderPlayerId, settings.scoreSaberPlayerId, entries, error,
                                       &beatLeaderHistory, &scoreSaberHistory) ||
                    !WritePlaylist("Sync", ListKind::Sync, entries, error)) {
                    if (observer.syncError) observer.syncError(error);
                    SetStatus("Sync failed; previous playlist preserved. " + error);
                    return false;
                }
                filesChanged.store(true);
                PublishRecommendationPreview("Sync", ListKind::Sync, entries);
                SetStatus("Sync updated with " + std::to_string(entries.size()) + " songs (all matching charts).");
                return true;
            });
            allSucceeded = syncOk && allSucceeded;
        }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        Log("Recommendation refresh finished in " + std::to_string(elapsed) +
            "s (success: " + (allSucceeded ? "true" : "false") + ").");
        if (!attempted)
            SetStatus("Enable at least one playlist type in Mod Settings before refreshing.");
        else if (allSucceeded) {
            std::string warning = clanIconWarnings;
            for (const auto *history : {&beatLeaderHistory, &scoreSaberHistory}) {
                if (!history->attemptWarning.empty()) {
                    Log("Attempt history: " + history->attemptWarning);
                    warning += "\n" + history->attemptWarning;
                }
            }
            std::ostringstream status;
            status << "Playlists updated in " << std::fixed << std::setprecision(1) << elapsed << "s." << warning;
            SetStatus(status.str());
        } else
            SetStatus("Refresh finished with errors. See the mod log for details.");
        return {attempted && allSucceeded, filesChanged.load()};
    }
};
} // namespace
RefreshResult RunRefresh(const RefreshSettings &settings, const RefreshObserver &observer) {
    return RefreshRun(observer).RefreshAll(settings);
}
} // namespace rankedpractice
