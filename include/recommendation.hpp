#pragma once

#include <array>
#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include "attempt_history.hpp"

namespace rankedpractice {

enum class BeatLeaderSpeed { Normal, SlowerSong, FasterSong, SuperFastSong };

struct BeatLeaderSpeedOption {
    std::string_view code;
    std::string_view label;
    double multiplier;
};

// Better Faster and Better Super Fast have event ratings without ranked scores.
inline constexpr std::array<BeatLeaderSpeedOption, 4> kBeatLeaderSpeedOptions{{
    {"", "", 1.0},
    {"ss", "Slower Song", 0.85},
    {"fs", "Faster Song", 1.20},
    {"sf", "Super Fast Song", 1.50}
}};

struct BeatLeaderModifierOption {
    const char* code;
    const char* label;
    bool changesArrows{false}; // Select at most one arrow/visibility modifier.
};

inline constexpr BeatLeaderModifierOption kBeatLeaderStandardModifiers[] = {
    {"da", "Disappearing Arrows", true}, {"gn", "Ghost Notes", true}, {"na", "No Arrows", true},
    {"nb", "No Bombs"}, {"no", "No Walls"}, {"sc", "Small Notes"}, {"pm", "Pro Mode"}, {"sa", "Strict Angles"}
};

inline constexpr double kTargetWindowStars = 1.50;

struct BeatLeaderSpeedRating {
    double stars{0.0};
    double accRating{-1.0};
    double passRating{-1.0};
    double techRating{-1.0};
    double predictedAcc{-1.0};
    std::optional<double> sliderPercentage;
    double legacyAdjustment{0.0};
};

struct Difficulty {
    std::string characteristic{"Standard"};
    std::string name;
    double stars{0.0};
    double accRating{0.0};
    double passRating{0.0};
    double techRating{0.0};
    double predictedAcc{0.0}; // BeatLeader's map-level Acc AI input, not the player's prediction.
    double sliderPercentage{0.0};
    double notes{0.0};
    std::optional<double> noteSpan; // Presence selects PPv4, including an explicit zero.
    std::optional<double> complexity; // AccSaber's independent scale; zero is a valid rating.
    std::array<BeatLeaderSpeedRating, kBeatLeaderSpeedOptions.size() - 1> speedRatings{};
    std::array<double, std::size(kBeatLeaderStandardModifiers)> modifierValues{};
    bool hasSpeedRatings{false};
};

struct ClanImpact {
    double currentClanPp{0.0};
    double rivalPp{0.0};
    double gap{0.0};
    double gain{0.0};
    double gapClosure{0.0};
    double adjustedGain{0.0};
    std::optional<double> requiredPp;
    std::optional<double> requiredAccuracy;
    bool captures{false};
    bool hypothetical{false};
};

struct MapEntry {
    std::string hash;
    std::string songName;
    std::string songSubName;
    std::string songAuthorName;
    std::string levelAuthorName;
    std::string leaderboardId;
    std::vector<Difficulty> difficulties;
    double stars{0.0};
    double modifiedStars{0.0};
    double pp{0.0};
    double accuracy{0.0};
    double baseAccuracy{0.0};
    double timepost{0.0};
    double priority{0.0};
    double predictedAccuracy{0.0};
    double predictedPp{0.0};
    double currentPp{0.0};
    double weightedPpGain{0.0};
    double clearChance{0.0};
    double confidence{0.0};
    int nearbyClears{0};
    int nearbyNoFail{0};
    int nearbyFailures{0};
    bool noFail{false};
    bool ranked{true};
    bool isImprovement{false};
    std::optional<ClanImpact> clanImpact;
    struct SyncTarget {
        std::string characteristic;
        std::string difficulty;
        std::string missingService;
    };
    std::vector<SyncTarget> syncTargets;
    std::string categoryId, categoryCode, categoryLabel;
    std::string modifiers;
    double maxScore{0.0}, scoreNoMods{0.0};
    std::string recommendedModifier;
    double beatLeaderScoringMultiplier{1.0};
};

enum class ListKind { NotPlayed, ToImprove, Combined, Sync };
enum class RatingSystem { ScoreSaber, BeatLeader, AccSaber };
bool IsRecommendationCharacteristic(const std::string& characteristic, RatingSystem system);
// The settings UI uses whole percentages; recommendation code uses fractions.
int NormalizeMinClearRatePercent(int percent);

std::vector<MapEntry> SelectRecommendations(std::vector<MapEntry> entries,
                                            ListKind kind,
                                            int limit,
                                            double targetStars = 0.0,
                                            const std::vector<MapEntry>& profileScores = {},
                                            RatingSystem system = RatingSystem::BeatLeader,
                                            double minClearRate = 0.50,
                                            bool rankByWeightedGain = false,
                                            const std::vector<Attempt>& attempts = {},
                                            bool beatLeaderModifiers = false);
double EstimateRecommendationTargetStars(const std::vector<MapEntry>& profileScores,
                                         RatingSystem system,
                                         double minClearRate = 0.50,
                                         const std::vector<Attempt>& attempts = {});
void ResolveAttemptRatings(std::vector<Attempt>& attempts, const std::vector<MapEntry>& charts,
                           int ratingEpoch = 0);
std::vector<MapEntry> MergeRecommendations(std::vector<MapEntry> notPlayed,
                                           std::vector<MapEntry> toImprove,
                                           int limit);
// Per-chart estimates without personal-profile PP gain filtering or song deduplication.
// With modifiers enabled, returns each evaluated speed with its effective difficulty.
std::vector<MapEntry> EvaluatePlayerPerformance(const std::vector<MapEntry>& charts,
                                               const std::vector<MapEntry>& scores,
                                               const std::vector<Attempt>& attempts = {},
                                               bool beatLeaderModifiers = false);
double PredictBeatLeaderPp(double accuracy, const Difficulty& difficulty, double scoringMultiplier = 1.0);
// Global clearability boundary for legacy clans. Zero rate disables the cap.
double EstimateClanMaxStars(const std::vector<MapEntry>& scores, double minClearRate,
                            const std::vector<Attempt>& attempts = {});
std::string PlaylistFileName(const std::string& service, ListKind kind);
std::string PlaylistTitle(const std::string& service, ListKind kind);
std::string ClanPlaylistFileName(const std::string& clanTag, bool perClan);
std::string ClanPlaylistTitle(const std::string& clanScope);

} // namespace rankedpractice
