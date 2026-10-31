#pragma once
#include "recommendation.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace rankedpractice {
struct ClanScore {
    std::string playerId;
    double pp{0.0};
    double accuracy{0.0};
    double timepost{0.0};
};
struct ClanMapState {
    double pp{0.0};
    std::vector<ClanScore> scores;
};
struct ClanMapInput {
    MapEntry chart;
    ClanMapState clan;
    ClanMapState primaryClan;
    // Highest unaffected rival. For a secondary clan, excludes the current primary clan.
    double rivalPp{0.0};
    bool hypothetical{false};
    bool held{false};
};
struct ClanScreenCandidate {
    MapEntry chart;
    double optimisticGapClosure{0.0};
    double optimisticGain{0.0};
    bool optimisticCapture{false};
};
// Previous verification is a search hint only. Recommendations always require
// fresh rankings and contributions; no score lists are stored here.
struct ClanSearchEvidence {
    std::string leaderboardId;
    double screenClanPp{0.0}, screenRivalPp{-1.0};
    double currentPp{0.0}, predictedPp{0.0}, predictedAccuracy{0.0};
    double baselinePp{0.0}, rivalPp{0.0}, gain{0.0};
    bool useful{false};
};
struct ClanSearchState {
    std::size_t cursor{0};
    int primaryClanId{0};
    std::vector<std::string> recommendations;
    std::vector<ClanSearchEvidence> evidence;
};
double WeightedClanPp(std::vector<ClanScore> scores);
bool ClanRecommendationBetter(const MapEntry& left, const MapEntry& right);
bool EvaluateClanImpact(ClanMapInput input, const std::string& playerId,
                        double minClearRate, MapEntry& result,
                        std::optional<ClanImpact>* projection = nullptr);
ClanScreenCandidate ScreenClanCandidate(const MapEntry& chart, double clanPp, double rivalPp,
                                       double minClearRate, const ClanSearchEvidence* evidence = nullptr);
bool ClanEvidenceMatches(const ClanSearchEvidence& evidence, const MapEntry& chart,
                         double clanPp, double rivalPp);
std::vector<MapEntry> SelectClanRecommendations(std::vector<MapEntry> entries, int limit);
// Entries carry the legacy endpoint's signed clan gap in priority, never in pp.
std::vector<MapEntry> SelectLegacyClanRecommendations(std::vector<MapEntry> entries, double maxStars, int limit);
int ClanVerificationBudget(int trackLimit);
std::vector<ClanScreenCandidate> ShortlistClanCandidates(std::vector<ClanScreenCandidate> entries,
                                                        int budget, std::size_t& cursor,
                                                        const std::vector<std::string>& previous = {});
} // namespace rankedpractice
