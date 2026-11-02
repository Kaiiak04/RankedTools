#include "main.hpp"

#include "bsml/shared/BSML.hpp"
#include "playlist_writer.hpp"
#include "recommendation_api.hpp"
#include "recommendation_view.hpp"
#include "refresh_settings.hpp"
#include "refresh_engine.hpp"
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
    settings.legacyClanPlaylists = getModConfig().LegacyClanPlaylists.GetValue();
    settings.allClans = getModConfig().ClanPlaylistAllClans.GetValue();
    return settings;
}

void SetStatus(const std::string& status) {
    SetViewStatus(status);
    PaperLogger.info("{}", status);
}

static void RefreshWorker(const RefreshSettings settings) noexcept {
    try {
        RefreshObserver observer;
        observer.status = SetViewStatus;
        observer.log = [](const std::string& text) { PaperLogger.info("{}", text); };
        observer.playlist = PublishRecommendationPreview;
        observer.clans = PublishClanPreviews;
        observer.ability = PublishAbilityPreview;
        RunRefresh(settings, observer);
        refreshInProgress.store(false);
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
                                         settings.enableBeatLeader, settings.enableScoreSaber, settings.enableClanPlaylist);
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
    AddConfigValueToggle(parent, getModConfig().LegacyClanPlaylists);
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
