#include "recommendation_view.hpp"
#include "ability_chart.hpp"
#include "leaderboards.hpp"
#include "accsaber.hpp"
#include "recommendation_api.hpp"

#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "bsml/shared/BSML/Components/ScrollViewContent.hpp"
#include "beatsaber-hook/shared/utils/typedefs-wrappers.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/TextureFormat.hpp"
#include "UnityEngine/Color32.hpp"
#include "UnityEngine/Sprite.hpp"
#include "UnityEngine/TextAnchor.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/LayoutRebuilder.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <iomanip>
#include <mutex>
#include <memory>
#include <cstring>
#include <cmath>
#include <sstream>

namespace rankedpractice {
namespace {

struct PreviewList {
    bool ready{false};
    std::vector<MapEntry> entries;
    std::string title, note, error;
    bool clan{false};
    bool legacy{false};
};

std::mutex previewMutex;
std::string refreshStatus{"Open this tab to refresh recommendations."};
double previewMinClearRate{0.50};
int previewRequiredSyncScoreboards{2};
std::vector<PreviewList> previewLists(10);
std::vector<int> activeLists{2, 5};
SafePtrUnity<HMUI::CurvedTextMeshPro> statusText;
SafePtrUnity<HMUI::CurvedTextMeshPro> detailsText;
SafePtrUnity<UnityEngine::RectTransform> contentRect;
SafePtrUnity<BSML::ScrollViewContent> scrollContent;
int selectedList = 2;
size_t selectedSong = 0;
struct AbilityPreview {
    bool ready{false};
    std::shared_ptr<const AbilityChart> chart;
    std::string error;
};
std::array<AbilityPreview, 5> abilityPreviews;
std::array<bool, 5> enabledServices{true, true, false, false, false};
bool showAbility = false;
int selectedService = 0;
SafePtrUnity<UnityEngine::GameObject> playlistNavigation, songNavigation, leaderboardNavigation;
SafePtrUnity<HMUI::CurvedTextMeshPro> chartNotes;
SafePtrUnity<HMUI::ImageView> chartImage;
SafePtrUnity<UnityEngine::Texture2D> chartTexture;
SafePtrUnity<UnityEngine::Sprite> chartSprite;
std::shared_ptr<const AbilityChart> renderedChart;

void ReleaseChartTexture() {
    if (chartImage) chartImage->set_sprite(nullptr);
    if (chartSprite) UnityEngine::Object::Destroy(chartSprite.ptr());
    if (chartTexture) UnityEngine::Object::Destroy(chartTexture.ptr());
    chartSprite = nullptr;
    chartTexture = nullptr;
    renderedChart.reset();
}

void UploadChart(const std::shared_ptr<const AbilityChart>& chart) {
    if (chart == renderedChart && chartTexture && chartSprite) return;
    ReleaseChartTexture();
    const auto bitmap = RenderAbilityChart(*chart);
    ArrayW<UnityEngine::Color32> pixels(il2cpp_array_size_t(bitmap.width * bitmap.height));
    static_assert(sizeof(UnityEngine::Color32) == 4);
    std::memcpy(pixels->_values, bitmap.pixels.data(), bitmap.pixels.size());
    chartTexture = UnityEngine::Texture2D::New_ctor(bitmap.width, bitmap.height, UnityEngine::TextureFormat::RGBA32, false);
    chartTexture->SetPixels32(pixels);
    chartTexture->Apply(false, true);
    chartSprite = BSML::Lite::TextureToSprite(chartTexture.ptr());
    chartImage->set_sprite(chartSprite.ptr());
    renderedChart = chart;
}

constexpr std::array<const char*, 10> listNames{
    "BeatLeader - Not Played", "BeatLeader - To Improve", "BeatLeader - PP Gain",
    "ScoreSaber - Not Played", "ScoreSaber - To Improve", "ScoreSaber - PP Gain", "Sync",
    "AccSaber - Not Played", "AccSaber - To Improve", "AccSaber - AP Gain"
};

int ListIndex(const std::string& service, ListKind kind) {
    if (kind == ListKind::Sync) return 6;
    const int base = service == "AccSaber" ? 7 : service == "ScoreSaber" ? 3 : 0;
    return base + (kind == ListKind::Combined ? 2 : kind == ListKind::ToImprove ? 1 : 0);
}

std::string ConfidenceLabel(double confidence) {
    if (confidence >= 0.7) return "High";
    if (confidence >= 0.5) return "Medium";
    return "Low";
}

std::string RenderDetails(const PreviewList& list, double minClearRate, bool hasLists) {
    std::ostringstream output;
    const bool acc = selectedList >= 7 && selectedList < 10;
    const auto unit = acc ? "AP" : "PP";
    const int layoutKind = acc ? selectedList - 7 : selectedList % 3;
    if (!hasLists) return "Enable a leaderboard, clan or Sync playlist to see songs here.";
    output << (!list.title.empty() ? list.title : listNames[selectedList]) << "\n";
    if (!list.note.empty()) output << list.note << "\n";
    if (!list.error.empty()) { output << "Previous playlist preserved. " << list.error; return output.str(); }
    if (selectedList != 6 && (list.clan || layoutKind != 1)) output << "Min Clear Rate: " << std::fixed << std::setprecision(0)
                                    << minClearRate * 100.0 << "%\n";
    if (!list.clan && layoutKind == 2) output << "Sorted by weighted " << unit << " gain\n";
    if (!list.ready) {
        output << "Waiting for recommendations...";
        return output.str();
    }
    if (list.entries.empty()) {
        output << "No eligible songs found for this playlist.";
        return output.str();
    }
    selectedSong = std::min(selectedSong, list.entries.size() - 1);
    const auto& entry = list.entries[selectedSong];
    if (selectedList == 6) {
        output << "Song " << (selectedSong + 1) << " of " << list.entries.size() << "\n" << entry.songName << "\n"
               << "Ranked on at least " << previewRequiredSyncScoreboards << " selected scoreboards. All matching charts; no song limit.\n";
        for (const auto& target : entry.syncTargets)
            output << target.characteristic << " / " << target.difficulty << ": missing on " << target.missingService << "\n";
        return output.str();
    }
    const auto& difficulty = entry.difficulties.front();
    const auto& modifier = entry.recommendedModifier;
    output << "Song " << (selectedSong + 1) << " of " << list.entries.size() << "\n"
           << entry.songName << "\n"
           << (difficulty.characteristic == "OneSaber" ? "OneSaber / " : "")
           << difficulty.name << "  |  " << std::fixed << std::setprecision(2)
           << (modifier.empty() ? difficulty.stars : entry.stars)
           << (acc ? " complexity\n" : modifier.empty() ? " stars\n" : " effective stars\n");
    if (!modifier.empty()) output << "Recommended modifier: " << modifier << "\n";
    output << "Predicted accuracy: " << std::setprecision(1)
           << entry.predictedAccuracy * 100.0 << "%\n"
           << "Predicted score: " << std::setprecision(1) << entry.predictedPp << " " << unit << "\n";
    if (acc) output << "Category: " << AccSaberCategoryName(entry.categoryCode) << "\n";
    if (list.legacy) {
        output << (entry.isImprovement ? "Improve an existing score" : "New contribution") << "\n"
               << "Current personal score: " << std::setprecision(1) << entry.currentPp << " " << unit << "\n"
               << "Rough clear chance: " << std::setprecision(0) << entry.clearChance*100 << "%\n"
               << "Current clan gap: " << std::setprecision(2) << std::max(0.0,-entry.priority) << " weighted PP\n"
               << "Sorted by the current clan gap; personal PP filters completed targets.\n";
    } else if (entry.clanImpact) {
        const auto& impact = *entry.clanImpact;
        output << (entry.isImprovement ? "Improve an existing score" : "New contribution") << "\n"
               << "Current personal score: " << std::setprecision(1) << entry.currentPp << " " << unit << "\n"
               << "Rough clear chance: " << std::setprecision(0) << entry.clearChance * 100 << "%\n"
               << "Remaining clan gap: " << std::setprecision(2) << impact.gap << " weighted PP\n"
               << "Projected clan gain: +" << impact.gain << " weighted PP\n"
               << (impact.captures ? "Capture at predicted score" : "Useful progress toward capture") << "\n";
        if (impact.requiredPp && impact.requiredAccuracy)
            output << "Capture target: " << std::setprecision(2) << std::ceil(*impact.requiredPp * 100) / 100 << " personal PP / "
                   << std::ceil(*impact.requiredAccuracy * 10000) / 100 << "% accuracy"
                   << (modifier.empty() ? " (unmodified)\n" : " with the recommended modifier\n");
        else output << (modifier.empty() ? "Capture exceeds the theoretical unmodified maximum.\n"
                                        : "Capture exceeds the maximum with the recommended modifier.\n");
        output << "Capture is an estimate, not a calibrated probability.\n";
    } else if (!entry.isImprovement) {
        output << "Rough clear chance: " << std::setprecision(0)
               << entry.clearChance * 100.0 << "%\n"
               << "Weighted " << unit << " gain if cleared: +" << std::setprecision(2)
               << entry.weightedPpGain << "\n";
        if (layoutKind == 0 && minClearRate > 0.0) {
            output << "Chance-adjusted gain: +" << entry.weightedPpGain * entry.clearChance << "\n";
        }
    } else {
        output << "Current score: " << std::setprecision(1) << entry.currentPp << " " << unit << "\n"
               << "Weighted " << unit << " gain if improved: +" << std::setprecision(2)
               << entry.weightedPpGain << "\n";
        if (!modifier.empty()) output << "Rough clear chance: " << std::setprecision(0) << entry.clearChance * 100 << "%\n";
    }
    output << "Confidence: " << ConfidenceLabel(entry.confidence) << "\n"
           << "Nearby history: " << entry.nearbyClears << " clears, "
           << entry.nearbyFailures << " failures, " << entry.nearbyNoFail << " NF runs";
    return output.str();
}

void UpdateView() {
    std::string status;
    PreviewList selected;
    double minClearRate;
    bool hasLists;
    AbilityPreview ability;
    bool enabled;
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        status = refreshStatus;
        selected = previewLists[selectedList];
        minClearRate = previewMinClearRate;
        hasLists = !activeLists.empty();
        ability = abilityPreviews[selectedService];
        enabled = enabledServices[selectedService];
    }
    if (statusText) statusText->set_text(StringW(status));
    if (playlistNavigation) playlistNavigation->SetActive(!showAbility);
    if (songNavigation) songNavigation->SetActive(!showAbility);
    if (leaderboardNavigation) leaderboardNavigation->SetActive(showAbility);
    const bool hasChart = enabled && ability.ready && ability.error.empty() && ability.chart && !ability.chart->scores.empty();
    if (chartImage) {
        chartImage->get_gameObject()->SetActive(showAbility && hasChart);
        if (showAbility && hasChart) UploadChart(ability.chart);
        else if (!hasChart) ReleaseChartTexture();
    }
    if (chartNotes) chartNotes->get_gameObject()->SetActive(showAbility && hasChart);
    if (detailsText) {
        if (!showAbility) detailsText->set_text(StringW(RenderDetails(selected, minClearRate, hasLists)));
        else {
            std::ostringstream description;
            description << AbilityName(selectedService) << " ability estimate\n";
            if (!enabled) description << "Enable this leaderboard's playlists in settings, then refresh.";
            else if (!ability.ready) description << "Loading profile scores...";
            else if (!ability.error.empty()) description << ability.error;
            else if (!hasChart) description << "No eligible ranked "
                << (selectedService == 0 ? "Standard or OneSaber" : "Standard")
                << " scores to fit yet. No Fail runs and scores below 55% are excluded.";
            else description << ability.chart->scores.size() << " eligible best scores. Played range: "
                             << std::fixed << std::setprecision(2) << ability.chart->minObservedStars
                             << " to " << ability.chart->maxObservedStars << (selectedService >= 2 ? " complexity." : " stars.");
            detailsText->set_text(StringW(description.str()));
        }
    }
    // Wrapped text can change height after a refresh or when switching songs.
    // Refresh the scroll extent after layout, rather than retaining its initial size.
    if (contentRect) UnityEngine::UI::LayoutRebuilder::ForceRebuildLayoutImmediate(contentRect.ptr());
    if (scrollContent) scrollContent->UpdateScrollView();
}

void ScheduleUpdate() {
    BSML::MainThreadScheduler::Schedule([] { UpdateView(); });
}

void ChangeList(int direction) {
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        if (activeLists.empty()) return;
        const auto found = std::find(activeLists.begin(), activeLists.end(), selectedList);
        const int index = found == activeLists.end() ? 0 : static_cast<int>(found - activeLists.begin());
        const int count = static_cast<int>(activeLists.size());
        selectedList = activeLists[(index + direction + count) % count];
    }
    selectedSong = 0;
    UpdateView();
}

void ChangeSong(int direction) {
    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        count = previewLists[selectedList].entries.size();
    }
    if (count == 0) return;
    selectedSong = direction < 0 ? (selectedSong + count - 1) % count
                                 : (selectedSong + 1) % count;
    UpdateView();
}

void PrepareText(HMUI::CurvedTextMeshPro* text) {
    text->set_enableWordWrapping(true);
    text->set_richText(false);
    text->set_alignment(TMPro::TextAlignmentOptions::TopLeft);
    // CreateText already attaches a LayoutElement. Reuse it, and let TMP report
    // preferred height after wrapping instead of reserving a fixed number of lines.
    auto* layout = text->get_gameObject()->GetComponent<UnityEngine::UI::LayoutElement*>();
    layout->set_minWidth(0.0f);
    layout->set_preferredWidth(0.0f);
    layout->set_flexibleWidth(1.0f);
    layout->set_minHeight(-1.0f);
    layout->set_preferredHeight(-1.0f);
    layout->set_flexibleHeight(0.0f);
}

void PrepareNavigationRow(UnityEngine::UI::HorizontalLayoutGroup* row) {
    // The parent layout owns the row's size; remove the helper's competing fitter.
    auto* fitter = row->get_gameObject()->GetComponent<UnityEngine::UI::ContentSizeFitter*>();
    fitter->set_horizontalFit(UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
    fitter->set_verticalFit(UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
    row->set_childControlWidth(true);
    row->set_childControlHeight(true);
    row->set_childForceExpandWidth(true);
    row->set_childForceExpandHeight(false);
    row->set_spacing(2.0f);
    row->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
    auto* layout = row->get_gameObject()->GetComponent<UnityEngine::UI::LayoutElement*>();
    layout->set_minWidth(0.0f);
    layout->set_preferredWidth(0.0f);
    layout->set_flexibleWidth(1.0f);
}

void CreateNavigationButton(UnityEngine::Transform* parent, const char* label, std::function<void()> onClick) {
    auto* button = BSML::Lite::CreateUIButton(parent, label, std::move(onClick));
    // BSML buttons also fit their own width/height. The row must own those sizes
    // for the two buttons to share its available width without layout conflicts.
    auto* fitter = button->get_gameObject()->GetComponent<UnityEngine::UI::ContentSizeFitter*>();
    fitter->set_horizontalFit(UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
    fitter->set_verticalFit(UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
    auto* layout = button->get_gameObject()->GetComponent<UnityEngine::UI::LayoutElement*>();
    layout->set_minWidth(0.0f);
    layout->set_preferredWidth(0.0f);
    layout->set_flexibleWidth(1.0f);
}

} // namespace

void BuildRecommendationView(HMUI::ViewController* controller) {
    ReleaseChartTexture();
    auto* container = BSML::Lite::CreateScrollableSettingsContainer(controller->get_transform());
    auto* layout = container->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>();
    layout->set_childControlWidth(true);
    layout->set_childControlHeight(true);
    layout->set_childForceExpandWidth(true);
    layout->set_childForceExpandHeight(false);
    layout->set_childAlignment(UnityEngine::TextAnchor::UpperLeft);
    layout->set_spacing(2.0f);
    auto parent = container->get_transform();
    // The settings helper fits content horizontally to its preferred width by
    // default. For prose this expands to the longest line, beyond the viewport.
    auto wrapper = parent->get_parent()->get_gameObject();
    wrapper->GetComponent<UnityEngine::UI::ContentSizeFitter*>()->set_horizontalFit(
        UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
    wrapper->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>()->set_childForceExpandWidth(true);
    contentRect = wrapper->GetComponent<UnityEngine::RectTransform*>();
    scrollContent = wrapper->GetComponent<BSML::ScrollViewContent*>();
    statusText = BSML::Lite::CreateText(parent, "Fetching leaderboard data...", TMPro::FontStyles::Normal, 2.8f);
    PrepareText(statusText.ptr());
    auto* modes = BSML::Lite::CreateHorizontalLayoutGroup(parent);
    PrepareNavigationRow(modes);
    CreateNavigationButton(modes->get_transform(), "Song details", [] { showAbility = false; UpdateView(); });
    CreateNavigationButton(modes->get_transform(), "Ability chart", [] { showAbility = true; UpdateView(); });
    auto* playlists = BSML::Lite::CreateHorizontalLayoutGroup(parent);
    playlistNavigation = playlists->get_gameObject();
    PrepareNavigationRow(playlists);
    CreateNavigationButton(playlists->get_transform(), "Previous playlist", [] { ChangeList(-1); });
    CreateNavigationButton(playlists->get_transform(), "Next playlist", [] { ChangeList(1); });
    auto* songs = BSML::Lite::CreateHorizontalLayoutGroup(parent);
    songNavigation = songs->get_gameObject();
    PrepareNavigationRow(songs);
    CreateNavigationButton(songs->get_transform(), "Previous song", [] { ChangeSong(-1); });
    CreateNavigationButton(songs->get_transform(), "Next song", [] { ChangeSong(1); });
    auto* leaderboards = BSML::Lite::CreateHorizontalLayoutGroup(parent);
    leaderboardNavigation = leaderboards->get_gameObject();
    PrepareNavigationRow(leaderboards);
    const auto changeAbility = [](int direction) {
        for (size_t i = 0; i < enabledServices.size(); ++i) {
            selectedService = (selectedService + direction + static_cast<int>(enabledServices.size())) % static_cast<int>(enabledServices.size());
            if (enabledServices[selectedService]) break;
        }
        UpdateView();
    };
    CreateNavigationButton(leaderboards->get_transform(), "Previous chart", [changeAbility] { changeAbility(-1); });
    CreateNavigationButton(leaderboards->get_transform(), "Next chart", [changeAbility] { changeAbility(1); });
    detailsText = BSML::Lite::CreateText(parent, "Waiting for recommendations...", TMPro::FontStyles::Normal, 3.0f);
    PrepareText(detailsText.ptr());
    chartImage = BSML::Lite::CreateImage(parent, nullptr);
    chartImage->set_preserveAspect(true);
    chartImage->set_raycastTarget(false);
    auto* chartLayout = chartImage->get_gameObject()->GetComponent<UnityEngine::UI::LayoutElement*>();
    chartLayout->set_minWidth(0.0f);
    chartLayout->set_preferredWidth(0.0f);
    chartLayout->set_flexibleWidth(1.0f);
    chartLayout->set_minHeight(60.0f);
    chartLayout->set_preferredHeight(60.0f);
    chartLayout->set_flexibleHeight(0.0f);
    chartNotes = BSML::Lite::CreateText(parent,
        "Dots: best scores (older scores are dimmer). Cyan: 30-day sigmoid accuracy fit. "
        "Dashed / shaded: outside your played range. AccSaber uses a separate complexity fit for each category.\n"
        "This predicts accuracy on a scored run, not your chance of clearing. Map style and sparse history can affect reliability.",
        TMPro::FontStyles::Normal, 2.6f);
    PrepareText(chartNotes.ptr());
    auto* help = BSML::Lite::CreateText(parent,
        "After refreshing, use PlaylistManager's Refresh Playlists button to load the songs.",
        TMPro::FontStyles::Normal, 2.6f);
    PrepareText(help);
    UpdateView();
}

void SetViewStatus(const std::string& status) {
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        refreshStatus = status;
    }
    ScheduleUpdate();
}

void ConfigureRecommendationPreviews(double minClearRate, bool separateNewAndRepeatTracks,
                                      bool enableBeatLeader, bool enableScoreSaber, bool enableClans, bool enableSync,
                                      bool enableAccSaber, const std::string& accCategory, int requiredSyncScoreboards) {
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        previewLists.assign(10, {});
        abilityPreviews = {};
        for (size_t i = 0; i < enabledServices.size(); ++i)
            enabledServices[i] = AbilityEnabled(static_cast<int>(i), enableBeatLeader, enableScoreSaber, enableAccSaber, accCategory);
        selectedService = 0;
        for (size_t i = 0; i < enabledServices.size(); ++i) if (enabledServices[i]) { selectedService = static_cast<int>(i); break; }
        previewMinClearRate = minClearRate;
        previewRequiredSyncScoreboards = requiredSyncScoreboards;
        activeLists.clear();
        for (int base : {0, 3, 7}) {
            if (!(base == 0 ? enableBeatLeader : base == 3 ? enableScoreSaber : enableAccSaber)) continue;
            if (!separateNewAndRepeatTracks) activeLists.push_back(base + 2);
            else { activeLists.push_back(base); activeLists.push_back(base + 1); }
        }
        for (int i = 7; i < 10; ++i) previewLists[i].title = std::string(listNames[i]) +
            (accCategory == "overall" ? "" : " (" + AccSaberCategoryName(accCategory) + ")");
        if (enableSync) activeLists.push_back(6);
        if (enableClans) {
            PreviewList clan; clan.clan = true; clan.title = "BeatLeader - Maps to Conquer";
            previewLists.push_back(std::move(clan)); activeLists.push_back(10);
        }
        selectedList = activeLists.empty() ? 0 : activeLists.front();
        selectedSong = 0;
    }
    ScheduleUpdate();
}

void PublishClanPreviews(const std::vector<ClanPlaylistRecommendations>& playlists) {
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        previewLists.resize(10);
        activeLists.erase(std::remove_if(activeLists.begin(), activeLists.end(), [](int i) { return i >= 10; }), activeLists.end());
        for (const auto& playlist : playlists) {
            PreviewList list; list.ready = true; list.clan = true; list.entries = playlist.entries;
            list.legacy=playlist.legacy;
            list.title = ClanPlaylistTitle(playlist.clanTag+(playlist.legacy?"; legacy":""));
            list.note = playlist.hypothetical ? "What-if primary clan: requires changing your primary clan.\n" : "";
            if (playlist.legacy) {
                std::ostringstream note;
                note << "Legacy clan-gap order. ";
                if (std::isfinite(playlist.legacyMaxStars))
                    note << "Automatic maximum: " << std::fixed << std::setprecision(2)
                         << std::floor(playlist.legacyMaxStars*100)/100 << " stars; no lower star limit.\n";
                else note << "Maximum stars disabled at 0% Min Clear Rate.\n";
                note << "Keeps unplayed charts and predicted personal improvements.\n";
                if (playlist.hypothetical) note << "Order uses current clan totals before any primary-clan transfer.\n";
                list.note+=note.str();
            } else {
                list.note += "Estimated captures first, then useful progress.\nVerified " +
                    std::to_string(playlist.checkedCharts - playlist.failedCharts) + " of " + std::to_string(playlist.eligibleCharts) +
                    " eligible charts. Targeted search may miss opportunities.";
                list.note += "\nRechecked "+std::to_string(playlist.recheckedCharts)+" previous picks; learned clan impact for "+
                    std::to_string(playlist.learnedCharts)+" candidates.";
            }
            if (!playlist.warning.empty()) list.note += "\n" + playlist.warning;
            list.error = playlist.error;
            activeLists.push_back(static_cast<int>(previewLists.size())); previewLists.push_back(std::move(list));
        }
        if (selectedList >= static_cast<int>(previewLists.size()) ||
            std::find(activeLists.begin(), activeLists.end(), selectedList) == activeLists.end())
            selectedList = activeLists.empty() ? 0 : activeLists.front();
    }
    ScheduleUpdate();
}

void PublishAbilityPreview(const std::string& service, const std::vector<MapEntry>& scores,
                           const std::string& error) {
    // Pure C++ model construction is safe on the refresh worker; Unity work stays in UpdateView.
    const int slot = AbilitySlot(service);
    if (slot < 0) return;
    auto chart = error.empty() ? std::make_shared<AbilityChart>(slot < 2 ? BuildAbilityChart(scores, false,
        slot == 0 ? RatingSystem::BeatLeader : RatingSystem::ScoreSaber) :
        BuildAccSaberAbilityChart(scores, std::string(kAccSaberCategories[slot - 1]))) : nullptr;
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        abilityPreviews[slot] = {true, std::move(chart), error};
    }
    ScheduleUpdate();
}

void PublishPlaylistError(const std::string& service, const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        for (auto kind : {ListKind::NotPlayed, ListKind::ToImprove, ListKind::Combined}) {
            auto& list = previewLists[ListIndex(service, kind)]; list.ready = true; list.error = error;
        }
    }
    ScheduleUpdate();
}

void PublishSyncError(const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        previewLists[6].ready = true;
        previewLists[6].error = error;
    }
    ScheduleUpdate();
}

void PublishRecommendationPreview(const std::string& service,
                                  ListKind kind,
                                  const std::vector<MapEntry>& entries) {
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        auto& list = previewLists[ListIndex(service, kind)];
        list.ready = true;
        list.entries = entries;
    }
    ScheduleUpdate();
}

} // namespace rankedpractice
