#pragma once
#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace rankedpractice {
inline constexpr std::array<std::string_view, 3> kLeaderboardServices{"BeatLeader", "ScoreSaber", "AccSaber"};
inline constexpr std::array<std::string_view, 4> kAccSaberCategories{"overall", "true_acc", "standard_acc", "tech_acc"};
// Existing ability slots 0 and 1 are part of the native ABI. Append categories.
inline constexpr std::array<std::string_view, 5> kAbilityServices{
    "BeatLeader", "ScoreSaber", "AccSaber/true_acc", "AccSaber/standard_acc", "AccSaber/tech_acc"};
inline int NormalizeRequiredSyncScoreboards(int value) {
    return std::clamp(value, 2, static_cast<int>(kLeaderboardServices.size()));
}
inline std::string NormalizeAccSaberCategory(std::string_view category) {
    for (auto code : kAccSaberCategories) if (category == code) return std::string(code);
    return "overall";
}
inline std::string AccSaberCategoryName(std::string_view category) {
    return category == "true_acc" ? "True Acc" : category == "standard_acc" ? "Standard Acc" :
           category == "tech_acc" ? "Tech Acc" : "Overall";
}
inline int AbilitySlot(std::string_view service) {
    for (size_t i = 0; i < kAbilityServices.size(); ++i) if (service == kAbilityServices[i]) return static_cast<int>(i);
    return -1;
}
inline std::string AbilityName(int slot) {
    return slot < 2 ? std::string(kAbilityServices.at(slot)) :
        "AccSaber - " + AccSaberCategoryName(kAccSaberCategories.at(slot - 1));
}
inline bool AbilityEnabled(int slot, bool beatLeader, bool scoreSaber, bool accSaber, std::string_view category) {
    return slot == 0 ? beatLeader : slot == 1 ? scoreSaber :
        accSaber && (category == "overall" || category == kAccSaberCategories.at(slot - 1));
}
} // namespace rankedpractice
