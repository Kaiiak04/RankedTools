#include "main.hpp"

#include "bsml/shared/BSML.hpp"
#include "playlist_writer.hpp"
#include "recommendation_api.hpp"
#include "recommendation_view.hpp"
#include "refresh_settings.hpp"
#include "refresh_tasks.hpp"
#include "account_view.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iomanip>
#include <sstream>
#include <thread>
#include <utility>

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};

namespace rankedpractice {

std::atomic<bool> refreshInProgress{false};

static RefreshSettings CaptureRefreshSettings() {
    RefreshSettings settings;
    settings.beatLeaderPlayerId = getModConfig().BeatLeaderPlayerId.GetValue();
    settings.scoreSaberPlayerId = getModConfig().ScoreSaberPlayerId.GetValue();
    settings.limit = std::clamp(getModConfig().MaxTracksPerPlaylist.GetValue(), 1, 100);
    settings.minClearRate = NormalizeMinClearRatePercent(getModConfig().MinClearRate.GetValue()) / 100.0;
    settings.simpleMode = getModConfig().SimpleMode.GetValue();
    settings.importAttempts = getModConfig().ImportAttemptHistory.GetValue();
    settings.enableBeatLeader = getModConfig().EnableBeatLeader.GetValue();
    settings.enableScoreSaber = getModConfig().EnableScoreSaber.GetValue();
    settings.enableClanPlaylist = getModConfig().EnableClanPlaylist.GetValue();
    settings.clanAutoStars = getModConfig().ClanPlaylistAutoStarFilter.GetValue();
    settings.allClans = getModConfig().ClanPlaylistAllClans.GetValue();
    settings.clanMinStars = getModConfig().ClanPlaylistMinStars.GetValue();
    settings.clanMaxStars = getModConfig().ClanPlaylistMaxStars.GetValue();
    return settings;
}

void SetStatus(const std::string& status) {
    SetViewStatus(status);
    PaperLogger.info("{}", status);
}

static std::vector<ListKind> PlaylistKinds(bool simpleMode) {
    return simpleMode ? std::vector{ListKind::Combined}
                      : std::vector{ListKind::NotPlayed, ListKind::ToImprove};
}

static bool PruneDisabledServiceLayout(const std::string& service, bool simpleMode) {
    std::vector<std::string> allowed;
    for (auto kind : PlaylistKinds(simpleMode)) allowed.push_back(PlaylistFileName(service, kind));
    std::string error;
    if (PruneLeaderboardPlaylists(service, allowed, error)) return true;
    SetStatus(service + " obsolete playlist cleanup failed: " + error);
    return false;
}

static bool RefreshPlaylist(const std::string& service,
                            const std::string& playerId,
                            int limit,
                            double minClearRate,
                            bool simpleMode,
                            ScoreHistory& history,
                            StarRange* beatLeaderNotPlayedRange = nullptr) {
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
        const bool fetched = FetchRecommendations(service, playerId, kinds[i], limit, playlists[i], error, minClearRate,
                                  beatLeaderNotPlayedRange, &history);
        if (i == 0) PublishAbilityPreview(service, history.scores,
            history.loaded ? "" : "Profile could not be loaded: " + error);
        if (!fetched) {
            SetStatus(PlaylistTitle(service, kinds[i]) + " failed: " + error); return false;
        }
    }
    std::vector<std::string> expected;
    for (size_t i = 0; i < kinds.size(); ++i) {
        if (!WritePlaylist(service, kinds[i], playlists[i], error)) {
            SetStatus(service + " playlist could not be saved: " + error); return false;
        }
        expected.push_back(PlaylistFileName(service, kinds[i]));
        PublishRecommendationPreview(service, kinds[i], playlists[i]);
        SetStatus(PlaylistTitle(service, kinds[i]) + " updated with " +
                  std::to_string(playlists[i].size()) + " songs.");
    }
    if (!PruneLeaderboardPlaylists(service, expected, error)) {
        SetStatus(service + " playlists were saved, but cleanup failed: " + error); return false;
    }
    return true;
}

static bool RefreshClanPlaylist(const RefreshSettings& settings,
                               ScoreHistory& history,
                               const StarRange* beatLeaderNotPlayedRange,
                               std::string& iconWarnings) {
    const auto& playerId = settings.beatLeaderPlayerId;
    const int limit = settings.limit;
    const auto minClearRate = settings.minClearRate;
    if (playerId.empty()) {
        SetStatus("BeatLeader clan playlist is enabled, but the BeatLeader player ID is empty.");
        return false;
    }

    std::vector<ClanPlaylistRecommendations> clanPlaylists;
    std::string error;
    double minStars = settings.clanMinStars;
    double maxStars = settings.clanMaxStars;
    if (settings.clanAutoStars) {
        StarRange automaticRange;
        if (beatLeaderNotPlayedRange && beatLeaderNotPlayedRange->valid) {
            automaticRange = *beatLeaderNotPlayedRange;
        } else if (!FetchBeatLeaderNotPlayedStarRange(playerId, minClearRate, automaticRange, error, &history)) {
            SetStatus("Could not calculate the automatic BeatLeader star range: " + error);
            return false;
        }
        minStars = automaticRange.minStars;
        maxStars = automaticRange.maxStars;
    }
    const auto allClans = settings.allClans;
    if (!FetchClanToConquer(playerId, allClans, minStars, maxStars, limit,
                            clanPlaylists, error)) {
        SetStatus("BeatLeader Maps to Conquer failed: " + error);
        return false;
    }

    std::vector<std::string> expectedFileNames;
    size_t totalSongs = 0;
    for (const auto& clanPlaylist : clanPlaylists) {
        const auto fileName = ClanPlaylistFileName(clanPlaylist.clanTag, allClans);
        if (!WriteClanPlaylist(clanPlaylist.clanTag, allClans, clanPlaylist.entries, error, clanPlaylist.image)) {
            SetStatus("BeatLeader Maps to Conquer playlist for " + clanPlaylist.clanTag +
                      " could not be saved: " + error);
            return false;
        }
        expectedFileNames.push_back(fileName);
        totalSongs += clanPlaylist.entries.size();
        if (!clanPlaylist.iconWarning.empty()) {
            PaperLogger.warn("{}", clanPlaylist.iconWarning);
            iconWarnings += "\n" + clanPlaylist.iconWarning;
        }
    }
    if (!PruneClanPlaylists(expectedFileNames, error)) {
        SetStatus("BeatLeader Maps to Conquer playlists were written, but obsolete files could not be cleaned up: " + error);
        return false;
    }
    SetStatus("BeatLeader Maps to Conquer updated " + std::to_string(clanPlaylists.size()) +
              " clan playlist(s) with " + std::to_string(totalSongs) + " songs total (" +
              std::to_string(minStars) + "–" + std::to_string(maxStars) + " stars).");
    return true;
}

static void RefreshAll(const RefreshSettings& settings) {
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

    const auto timed = [](const char* name, const std::function<bool()>& task) {
        const auto before = GetHttpStatistics();
        const auto start = std::chrono::steady_clock::now();
        const bool success = task();
        const auto after = GetHttpStatistics();
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        PaperLogger.info("{} refresh: {:.2f}s, {} HTTPS requests, {} new connections, {:.2f}s in HTTPS.",
            name, elapsed, after.requests - before.requests, after.connections - before.connections,
            after.seconds - before.seconds);
        return success;
    };
    std::function<bool()> beatLeaderTask, scoreSaberTask;
    if (settings.enableBeatLeader || settings.enableClanPlaylist) {
        attempted = true;
        beatLeaderTask = [&] {
            bool success = true;
            if (settings.enableBeatLeader) success = timed("BeatLeader", [&] {
                return RefreshPlaylist("BeatLeader", settings.beatLeaderPlayerId, limit, minClearRate,
                    settings.simpleMode, beatLeaderHistory, &beatLeaderNotPlayedRange);
            });
            if (settings.enableClanPlaylist) success = timed("BeatLeader clans", [&] {
                return RefreshClanPlaylist(settings, beatLeaderHistory, &beatLeaderNotPlayedRange, clanIconWarnings);
            }) && success;
            return success;
        };
    }
    if (settings.enableScoreSaber) {
        attempted = true;
        scoreSaberTask = [&] { return timed("ScoreSaber", [&] {
            return RefreshPlaylist("ScoreSaber", settings.scoreSaberPlayerId, limit, minClearRate,
                settings.simpleMode, scoreSaberHistory);
        }); };
    }
    const auto results = RunRefreshTasks(beatLeaderTask, scoreSaberTask);
    for (size_t i = 0; i < results.size(); ++i) {
        allSucceeded = results[i].succeeded && allSucceeded;
        if (!results[i].error.empty())
            SetStatus(std::string(i == 0 ? "BeatLeader" : "ScoreSaber") + " refresh stopped: " + results[i].error);
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    PaperLogger.info("Recommendation refresh finished in {:.2f}s (success: {}).", elapsed, allSucceeded);
    if (!attempted) SetStatus("Enable at least one playlist type in Mod Settings before refreshing.");
    else if (allSucceeded) {
        std::string warning = clanIconWarnings;
        for (const auto* history : {&beatLeaderHistory, &scoreSaberHistory}) {
            if (!history->attemptWarning.empty()) {
                PaperLogger.warn("Attempt history: {}", history->attemptWarning);
                warning += "\n" + history->attemptWarning;
            }
        }
        std::ostringstream status;
        status << "Playlists updated in " << std::fixed << std::setprecision(1) << elapsed << "s." << warning;
        SetStatus(status.str());
    }
    else SetStatus("Refresh finished with errors. See the mod log for details.");
    refreshInProgress.store(false);
}

static void RefreshWorker(const RefreshSettings settings) noexcept {
    try {
        RefreshAll(settings);
    } catch (const std::exception& exception) {
        SetStatus(std::string("Refresh stopped unexpectedly: ") + exception.what());
        refreshInProgress.store(false);
    } catch (...) {
        SetStatus("Refresh stopped unexpectedly due to an unknown error.");
        refreshInProgress.store(false);
    }
}

void StartRefresh() {
    bool expected = false;
    if (!refreshInProgress.compare_exchange_strong(expected, true)) {
        SetStatus("A recommendation refresh is already running.");
        return;
    }
    try {
        auto settings = CaptureRefreshSettings();
        ConfigureRecommendationPreviews(settings.minClearRate, settings.simpleMode,
                                         settings.enableBeatLeader, settings.enableScoreSaber);
        SetStatus("Fetching leaderboard data…");
        std::thread(RefreshWorker, std::move(settings)).detach();
    } catch (const std::exception& exception) {
        refreshInProgress.store(false);
        SetStatus(std::string("Could not start refresh: ") + exception.what());
    }
}

} // namespace rankedpractice

static void DidActivate(HMUI::ViewController* self,
                        bool firstActivation,
                        bool addedToHierarchy,
                        bool screenSystemEnabling) {
    (void)addedToHierarchy;
    (void)screenSystemEnabling;
    if (firstActivation) {
        rankedpractice::BuildRecommendationView(self);
    }
    rankedpractice::StartRefresh();
}

static void SettingsDidActivate(HMUI::ViewController* self,
                                bool firstActivation,
                                bool addedToHierarchy,
                                bool screenSystemEnabling) {
    (void)addedToHierarchy;
    (void)screenSystemEnabling;
    if (!firstActivation) { rankedpractice::RefreshAccountSettings(); return; }

    auto* container = BSML::Lite::CreateScrollableSettingsContainer(self->get_transform());
    auto parent = container->get_transform();
    rankedpractice::BuildAccountSettings(parent);
    AddConfigValueIncrementInt(parent, getModConfig().MaxTracksPerPlaylist, 1, 1, 100);
    auto& clearRate = getModConfig().MinClearRate;
    clearRate.SetValue(rankedpractice::NormalizeMinClearRatePercent(clearRate.GetValue()));
    auto* slider = AddConfigValueSlider(parent, clearRate, 0, 5.0f, 0.0f, 95.0f);
    slider->isInt = true;
    slider->formatter = [](float value) { return StringW(std::to_string(rankedpractice::NormalizeMinClearRatePercent(static_cast<int>(value))) + "%"); };
    slider->set_Value(static_cast<float>(clearRate.GetValue()));
    AddConfigValueToggle(parent, getModConfig().SimpleMode);
    AddConfigValueToggle(parent, getModConfig().RecordLocalAttempts);
    AddConfigValueToggle(parent, getModConfig().ImportAttemptHistory);
    AddConfigValueToggle(parent, getModConfig().EnableBeatLeader);
    AddConfigValueToggle(parent, getModConfig().EnableScoreSaber);
    AddConfigValueToggle(parent, getModConfig().EnableClanPlaylist);
    AddConfigValueToggle(parent, getModConfig().ClanPlaylistAutoStarFilter);
    AddConfigValueIncrementDouble(parent, getModConfig().ClanPlaylistMinStars, 1, 0.5, 0.0, 20.0);
    AddConfigValueIncrementDouble(parent, getModConfig().ClanPlaylistMaxStars, 1, 0.5, 0.0, 20.0);
    AddConfigValueToggle(parent, getModConfig().ClanPlaylistAllClans);
}

MOD_EXTERN_FUNC void setup(CModInfo* info) noexcept {
    *info = modInfo.to_c();
    Paper::Logger::RegisterFileContextId(PaperLogger.tag);
    PaperLogger.info("RankedTools loaded.");
}

MOD_EXTERN_FUNC void late_load() noexcept {
    il2cpp_functions::Init();
    getModConfig().Init(modInfo);
    rankedpractice::InstallAttemptRecorder();
    BSML::Init();
    if (!BSML::Register::RegisterSettingsMenu("RankedTools", SettingsDidActivate, false)) {
        PaperLogger.error("Failed to register RankedTools settings menu.");
    }
    BSML::Register::RegisterMainMenuViewControllerMethod(
        "RankedTools", "RankedTools", "Refresh ranked and clan playlists", DidActivate);
    PaperLogger.info("RankedTools menu registered.");
}
