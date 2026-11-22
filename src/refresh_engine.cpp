#include "refresh_engine.hpp"
#include "playlist_writer.hpp"
#include "refresh_tasks.hpp"
#include "leaderboards.hpp"
#include "platform.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace rankedpractice {
namespace {
class RefreshRun {
    const RefreshObserver &observer;
    std::atomic<bool> filesChanged{false};
    std::mutex diagnosticsMutex;
    std::vector<std::string> events, failures;
    void Log(const std::string &text) {
        {
            std::lock_guard lock(diagnosticsMutex);
            events.push_back(text);
        }
        if (observer.log)
            observer.log(text);
    }
    void SetStatus(const std::string &text) {
        if (observer.status)
            observer.status(text);
        Log(text);
    }
    bool Fail(const std::string &text) {
        {
            std::lock_guard lock(diagnosticsMutex);
            if (std::find(failures.begin(), failures.end(), text) == failures.end()) failures.push_back(text);
        }
        SetStatus(text);
        return false;
    }
    std::string FailureSummary() {
        std::lock_guard lock(diagnosticsMutex);
        std::string result = "Refresh finished with errors:";
        if (failures.empty()) result += "\nA refresh task failed; see the mod log for details.";
        for (size_t i = 0; i < std::min<size_t>(3, failures.size()); ++i) result += "\n" + failures[i];
        if (failures.size() > 3) result += "\n" + std::to_string(failures.size() - 3) + " more error(s) in the saved report.";
        return result;
    }
    bool SaveFailureReport(const RefreshSettings &settings, double elapsed, std::string &error) {
        const auto directory = RuntimePaths().icons;
        const auto destination = directory / "last-refresh-error.log";
        const auto temporary = directory / "last-refresh-error.log.tmp";
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec) { error = ec.message(); return false; }
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) { error = "Could not open the diagnostic file."; return false; }
        const auto utc = UtcTime(std::time(nullptr));
        output << "RankedTools last failed refresh\nUTC: " << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ")
               << "\nDuration: " << elapsed << "s\n"
               << "Accounts: BeatLeader=" << settings.beatLeaderPlayerId << "; ScoreSaber=" << settings.scoreSaberPlayerId
               << "; AccSaber=" << settings.accSaberPlayerId << "\n"
               << "Enabled: BeatLeader=" << settings.enableBeatLeader << "; ScoreSaber=" << settings.enableScoreSaber
               << "; AccSaber=" << settings.enableAccSaber << "; clans=" << settings.enableClanPlaylist
               << "; Sync=" << settings.enableSyncPlaylist << "\n"
               << "Required Sync Scoreboards: " << settings.requiredSyncScoreboards
               << "; AccSaber category: " << settings.accSaberCategory << "; separate tracks=" << settings.separateNewAndRepeatTracks
               << "; import attempts=" << settings.importAttempts << "; limit=" << settings.limit
               << "; minimum clear rate=" << settings.minClearRate << "\n\nRefresh events:\n";
        {
            std::lock_guard lock(diagnosticsMutex);
            for (const auto &event : events) output << event << '\n';
        }
        output.close();
        if (!output) error = "Could not finish writing the diagnostic file.";
        else {
            ReplaceFile(temporary, destination, ec);
            if (!ec) return true;
            error = ec.message();
        }
        std::filesystem::remove(temporary, ec);
        return false;
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
    std::vector<ListKind> PlaylistKinds(bool separateNewAndRepeatTracks) {
        return separateNewAndRepeatTracks ? std::vector{ListKind::NotPlayed, ListKind::ToImprove}
                                          : std::vector{ListKind::Combined};
    }

    bool PruneDisabledServiceLayout(const std::string &service, bool separateNewAndRepeatTracks) {
        std::vector<std::string> allowed;
        for (auto kind : PlaylistKinds(separateNewAndRepeatTracks))
            allowed.push_back(PlaylistFileName(service, kind));
        std::string error;
        if (PruneLeaderboardPlaylists(service, allowed, error)) {
            filesChanged.store(true);
            return true;
        }
        return Fail(service + " obsolete playlist cleanup failed: " + error);
    }

    bool RefreshPlaylist(const std::string &service, const std::string &playerId, int limit, double minClearRate,
                         bool separateNewAndRepeatTracks, ScoreHistory &history, StarRange *beatLeaderNotPlayedRange = nullptr,
                         bool beatLeaderModifiers = false) {
        if (playerId.empty()) {
            PublishAbilityPreview(service, {}, "Enter a player ID in RankedTools settings.");
            return Fail(service + " is enabled, but its player ID is empty.");
        }
        const auto kinds = PlaylistKinds(separateNewAndRepeatTracks);
        std::vector<std::vector<MapEntry>> playlists(kinds.size());
        std::string error;
        // Finish fetching the replacement layout before writing or pruning files.
        for (size_t i = 0; i < kinds.size(); ++i) {
            const bool fetched = FetchRecommendations(service, playerId, kinds[i], limit, playlists[i], error,
                                                      minClearRate, beatLeaderNotPlayedRange, &history, beatLeaderModifiers);
            if (i == 0)
                PublishAbilityPreview(service, history.scores,
                                      history.loaded ? "" : "Profile could not be loaded: " + error);
            if (!fetched) {
                return Fail(PlaylistTitle(service, kinds[i]) + " failed: " + error);
            }
        }
        std::vector<std::string> expected;
        for (size_t i = 0; i < kinds.size(); ++i) {
            if (!WritePlaylist(service, kinds[i], playlists[i], error)) {
                return Fail(service + " playlist could not be saved: " + error);
            }
            filesChanged.store(true);
            expected.push_back(PlaylistFileName(service, kinds[i]));
            PublishRecommendationPreview(service, kinds[i], playlists[i]);
            SetStatus(PlaylistTitle(service, kinds[i]) + " updated with " + std::to_string(playlists[i].size()) +
                      " songs.");
        }
        if (!PruneLeaderboardPlaylists(service, expected, error)) {
            return Fail(service + " playlists were saved, but cleanup failed: " + error);
        }
        filesChanged.store(true);
        return true;
    }

    bool RefreshAccSaber(const RefreshSettings& settings, ScoreHistory& history) {
        const auto kinds = PlaylistKinds(settings.separateNewAndRepeatTracks);
        std::vector<std::vector<MapEntry>> lists(kinds.size());
        std::string error;
        bool success = true;
        for (size_t i = 0; i < kinds.size(); ++i) {
            if (!FetchAccSaberRecommendations(settings.accSaberPlayerId, settings.accSaberCategory,
                kinds[i], settings.limit, settings.minClearRate, history, lists[i], error)) { success = false; break; }
        }
        for (size_t i = 1; i < kAccSaberCategories.size(); ++i) {
            const auto category = std::string(kAccSaberCategories[i]);
            const auto message = !history.loaded ? error : HasAccSaberEvidence(history.scores, category) ? "" :
                "Eligible scores in " + AccSaberCategoryName(category) + " are needed for a personal prediction.";
            PublishAbilityPreview("AccSaber/" + category, history.scores, message);
        }
        if (!success) {
            if (observer.playlistError) observer.playlistError("AccSaber", error);
            return Fail("AccSaber failed; previous playlists preserved. " + error);
        }
        std::vector<std::string> expected;
        for (size_t i = 0; i < kinds.size(); ++i) {
            if (!WriteAccSaberPlaylist(kinds[i], lists[i], error, settings.accSaberCategory)) {
                if (observer.playlistError) observer.playlistError("AccSaber", error);
                return Fail("AccSaber playlist save failed. " + error);
            }
            filesChanged.store(true);
            expected.push_back(PlaylistFileName("AccSaber", kinds[i]));
            PublishRecommendationPreview("AccSaber", kinds[i], lists[i]);
        }
        if (!PruneLeaderboardPlaylists("AccSaber", expected, error)) return Fail("AccSaber playlist cleanup failed: " + error);
        SetStatus("AccSaber updated (" + AccSaberCategoryName(settings.accSaberCategory) + ")." + history.attemptWarning);
        return true;
    }

    bool RefreshClanPlaylist(const RefreshSettings &settings, ScoreHistory &history, std::string &iconWarnings) {
        const auto &playerId = settings.beatLeaderPlayerId;
        const int limit = settings.limit;
        const auto minClearRate = settings.minClearRate;
        if (playerId.empty()) {
            return Fail("BeatLeader clan playlist is enabled, but the BeatLeader player ID is empty.");
        }

        std::vector<ClanPlaylistRecommendations> clanPlaylists;
        std::string error;
        const auto allClans = settings.allClans;
        if (!FetchClanToConquer(playerId, allClans, limit, clanPlaylists, error, minClearRate, &history,
                                settings.legacyClanPlaylists, settings.beatLeaderModifiers)) {
            ClanPlaylistRecommendations failed;
            failed.error = error;
            PublishClanPreviews({failed});
            return Fail("BeatLeader Maps to Conquer failed: " + error);
        }

        std::vector<std::string> expectedFileNames;
        size_t totalSongs = 0;
        bool succeeded = true;
        for (auto &clanPlaylist : clanPlaylists) {
            const auto fileName = ClanPlaylistFileName(clanPlaylist.clanTag, allClans);
            expectedFileNames.push_back(fileName); // Preserve a failed current clan during cleanup.
            if (!clanPlaylist.error.empty()) {
                Fail("Clan " + clanPlaylist.clanTag + " failed: " + clanPlaylist.error);
                succeeded = false;
                continue;
            }
            if (!WriteClanPlaylist(clanPlaylist.clanTag, allClans, clanPlaylist.entries, error, clanPlaylist.image,
                                   clanPlaylist.hypothetical, clanPlaylist.legacy)) {
                clanPlaylist.error = error;
                succeeded = false;
                Fail("BeatLeader Maps to Conquer playlist for " + clanPlaylist.clanTag +
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
            return Fail(
                "BeatLeader Maps to Conquer playlists were written, but obsolete files could not be cleaned up: " +
                error);
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
        ScoreHistory beatLeaderHistory, scoreSaberHistory, accSaberHistory;
        std::string clanIconWarnings;
        beatLeaderHistory.importAttempts = scoreSaberHistory.importAttempts = settings.importAttempts;
        accSaberHistory.importAttempts = settings.importAttempts;
        if (!settings.enableAccSaber)
            allSucceeded = PruneDisabledServiceLayout("AccSaber", settings.separateNewAndRepeatTracks) && allSucceeded;
        if (!settings.enableBeatLeader) {
            // Disabled services are not fetched. Remove only files from the other
            // layout so changing the separation setting cannot leave obsolete playlists visible.
            allSucceeded = PruneDisabledServiceLayout("BeatLeader", settings.separateNewAndRepeatTracks) && allSucceeded;
        }
        if (!settings.enableScoreSaber) {
            allSucceeded = PruneDisabledServiceLayout("ScoreSaber", settings.separateNewAndRepeatTracks) && allSucceeded;
        }

        const auto timed = [&](const char *name, const std::function<bool()> &task) {
            const auto before = GetHttpStatistics();
            const auto start = std::chrono::steady_clock::now();
            bool success = false;
            try { success = task(); }
            catch (const std::exception &exception) { Fail(std::string(name) + " refresh stopped: " + exception.what()); }
            catch (...) { Fail(std::string(name) + " refresh stopped due to an unknown error."); }
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
                                               settings.separateNewAndRepeatTracks, beatLeaderHistory, &beatLeaderNotPlayedRange,
                                               settings.beatLeaderModifiers);
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
                                           settings.separateNewAndRepeatTracks, scoreSaberHistory);
                });
            };
        }
        const auto results = RunRefreshTasks(beatLeaderTask, scoreSaberTask);
        for (size_t i = 0; i < results.size(); ++i) {
            allSucceeded = results[i].succeeded && allSucceeded;
            if (!results[i].error.empty())
                Fail(std::string(i == 0 ? "BeatLeader" : "ScoreSaber") + " refresh stopped: " + results[i].error);
        }
        if (settings.enableAccSaber) {
            attempted = true;
            allSucceeded = timed("AccSaber", [&] { return RefreshAccSaber(settings, accSaberHistory); }) && allSucceeded;
        }
        if (settings.enableSyncPlaylist) {
            attempted = true;
            const bool syncOk = timed("Sync", [&] {
                SetStatus("Sync: checking selected ranked catalogs and posted-score histories...");
                std::vector<MapEntry> entries;
                std::string error;
                const std::vector<SyncAccount> accounts{{"BeatLeader", settings.beatLeaderPlayerId, &beatLeaderHistory},
                    {"ScoreSaber", settings.scoreSaberPlayerId, &scoreSaberHistory}, {"AccSaber", settings.accSaberPlayerId, &accSaberHistory}};
                if (!FetchSyncPlaylist(accounts, settings.requiredSyncScoreboards, entries, error) ||
                    !WritePlaylist("Sync", ListKind::Sync, entries, error)) {
                    if (observer.syncError) observer.syncError(error);
                    return Fail("Sync failed; previous playlist preserved. " + error);
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
        if (!attempted && allSucceeded)
            SetStatus("Enable at least one playlist type in Mod Settings before refreshing.");
        else if (allSucceeded) {
            std::string warning = clanIconWarnings;
            for (const auto *history : {&beatLeaderHistory, &scoreSaberHistory, &accSaberHistory}) {
                if (!history->attemptWarning.empty()) {
                    Log("Attempt history: " + history->attemptWarning);
                    warning += "\n" + history->attemptWarning;
                }
            }
            std::ostringstream status;
            status << "Playlists updated in " << std::fixed << std::setprecision(1) << elapsed << "s." << warning;
            SetStatus(status.str());
        } else {
            auto status = FailureSummary();
            std::string diagnosticError;
            if (SaveFailureReport(settings, elapsed, diagnosticError))
                status += "\nDetails saved to last-refresh-error.log.";
            else {
                Log("Could not save last-refresh-error.log: " + diagnosticError);
                status += "\nCould not save the error report: " + diagnosticError;
            }
            SetStatus(status);
        }
        return {attempted && allSucceeded, filesChanged.load()};
    }
};
} // namespace
RefreshResult RunRefresh(const RefreshSettings &settings, const RefreshObserver &observer) {
    return RefreshRun(observer).RefreshAll(settings);
}
} // namespace rankedpractice
