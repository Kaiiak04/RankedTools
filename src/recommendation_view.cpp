#include "recommendation_view.hpp"
#include "ability_chart.hpp"

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
#include <sstream>

namespace rankedpractice {
namespace {

struct PreviewList {
    bool ready{false};
    std::vector<MapEntry> entries;
};

std::mutex previewMutex;
std::string refreshStatus{"Open this tab to refresh recommendations."};
double previewMinClearRate{0.50};
std::array<PreviewList, 6> previewLists;
std::vector<int> activeLists{0, 1, 3, 4};
SafePtrUnity<HMUI::CurvedTextMeshPro> statusText;
SafePtrUnity<HMUI::CurvedTextMeshPro> detailsText;
SafePtrUnity<UnityEngine::RectTransform> contentRect;
SafePtrUnity<BSML::ScrollViewContent> scrollContent;
int selectedList = 0;
size_t selectedSong = 0;
struct AbilityPreview {
    bool ready{false};
    std::shared_ptr<const AbilityChart> chart;
    std::string error;
};
std::array<AbilityPreview, 2> abilityPreviews;
std::array<bool, 2> enabledServices{true, true};
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

constexpr std::array<const char*, 6> listNames{
    "BeatLeader - Not Played", "BeatLeader - To Improve", "BeatLeader - PP Gain",
    "ScoreSaber - Not Played", "ScoreSaber - To Improve", "ScoreSaber - PP Gain"
};

int ListIndex(const std::string& service, ListKind kind) {
    const int base = service == "ScoreSaber" ? 3 : 0;
    return base + (kind == ListKind::Combined ? 2 : kind == ListKind::ToImprove ? 1 : 0);
}

std::string ConfidenceLabel(double confidence) {
    if (confidence >= 0.7) return "High";
    if (confidence >= 0.5) return "Medium";
    return "Low";
}

std::string RenderDetails(const PreviewList& list, double minClearRate, bool hasLists) {
    std::ostringstream output;
    if (!hasLists) return "Enable a leaderboard playlist to see song recommendations here. Clan playlists are refreshed separately.";
    output << listNames[selectedList] << "\n";
    if (selectedList % 3 != 1) output << "Min Clear Rate: " << std::fixed << std::setprecision(0)
                                    << minClearRate * 100.0 << "%\n";
    if (selectedList % 3 == 2) output << "Sorted by weighted PP gain\n";
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
    const auto& difficulty = entry.difficulties.front();
    output << "Song " << (selectedSong + 1) << " of " << list.entries.size() << "\n"
           << entry.songName << "\n"
           << difficulty.name << "  |  " << std::fixed << std::setprecision(2)
           << difficulty.stars << " stars\n"
           << "Predicted accuracy: " << std::setprecision(1)
           << entry.predictedAccuracy * 100.0 << "%\n"
           << "Predicted score: " << std::setprecision(1) << entry.predictedPp << " PP\n";
    if (!entry.isImprovement) {
        output << "Rough clear chance: " << std::setprecision(0)
               << entry.clearChance * 100.0 << "%\n"
               << "Weighted PP gain if cleared: +" << std::setprecision(2)
               << entry.weightedPpGain << "\n";
        if (selectedList % 3 == 0 && minClearRate > 0.0) {
            output << "Chance-adjusted gain: +" << entry.weightedPpGain * entry.clearChance << "\n";
        }
    } else {
        output << "Current score: " << std::setprecision(1) << entry.currentPp << " PP\n"
               << "Weighted PP gain if improved: +" << std::setprecision(2)
               << entry.weightedPpGain << "\n";
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
            description << (selectedService == 0 ? "BeatLeader" : "ScoreSaber") << " ability estimate\n";
            if (!enabled) description << "Enable this leaderboard's playlists in settings, then refresh.";
            else if (!ability.ready) description << "Loading profile scores...";
            else if (!ability.error.empty()) description << ability.error;
            else if (!hasChart) description << "No eligible ranked Standard scores to fit yet. No Fail runs and scores below 55% are excluded.";
            else description << ability.chart->scores.size() << " eligible best scores. Played range: "
                             << std::fixed << std::setprecision(2) << ability.chart->minObservedStars
                             << " to " << ability.chart->maxObservedStars << " stars.";
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
    CreateNavigationButton(leaderboards->get_transform(), "BeatLeader", [] { selectedService = 0; UpdateView(); });
    CreateNavigationButton(leaderboards->get_transform(), "ScoreSaber", [] { selectedService = 1; UpdateView(); });
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
        "Dots: best scores (older scores are dimmer). Cyan: recency-weighted monotonic fit. "
        "Dashed / shaded: outside your played range. Stars use modifier-adjusted ratings where available.\n"
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

void ConfigureRecommendationPreviews(double minClearRate, bool simpleMode,
                                      bool enableBeatLeader, bool enableScoreSaber) {
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        for (auto& list : previewLists) list = {};
        abilityPreviews = {};
        enabledServices = {enableBeatLeader, enableScoreSaber};
        selectedService = enableBeatLeader || !enableScoreSaber ? 0 : 1;
        previewMinClearRate = minClearRate;
        activeLists.clear();
        for (int base : {0, 3}) {
            if (!(base == 0 ? enableBeatLeader : enableScoreSaber)) continue;
            if (simpleMode) activeLists.push_back(base + 2);
            else { activeLists.push_back(base); activeLists.push_back(base + 1); }
        }
        selectedList = activeLists.empty() ? 0 : activeLists.front();
        selectedSong = 0;
    }
    ScheduleUpdate();
}

void PublishAbilityPreview(const std::string& service, const std::vector<MapEntry>& scores,
                           const std::string& error) {
    // Pure C++ model construction is safe on the refresh worker; Unity work stays in UpdateView.
    auto chart = error.empty() ? std::make_shared<AbilityChart>(BuildAbilityChart(scores)) : nullptr;
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        abilityPreviews[service == "ScoreSaber" ? 1 : 0] = {true, std::move(chart), error};
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
