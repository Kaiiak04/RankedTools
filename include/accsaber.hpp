#pragma once
#include "recommendation.hpp"
#include "ability_chart.hpp"
#include <utility>

namespace rankedpractice {
struct AccSaberCurve {
    std::string id;
    std::vector<std::pair<double, double>> points;
    double scale{0.0}, shift{0.0};
    double k{0.0}, y1{0.0}, x1{0.0};
};
struct AccSaberCategory {
    std::string id, code;
    bool countForOverall{true};
    AccSaberCurve scoreCurve, weightCurve;
};
double AccSaberAp(double accuracy, double complexity, const AccSaberCurve& curve);
double AccSaberPositionWeight(size_t position, const AccSaberCurve& curve);
bool HasAccSaberEvidence(const std::vector<MapEntry>& scores, const std::string& category);
AbilityChart BuildAccSaberAbilityChart(const std::vector<MapEntry>& scores, const std::string& category);
std::vector<MapEntry> SelectAccSaberRecommendations(const std::vector<MapEntry>& catalog,
    const std::vector<MapEntry>& scores, const std::vector<AccSaberCategory>& categories,
    const std::vector<Attempt>& attempts, const std::string& category, ListKind kind, int limit, double minClearRate);
} // namespace rankedpractice
