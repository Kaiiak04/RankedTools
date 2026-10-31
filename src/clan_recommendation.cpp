#include "clan_recommendation.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace rankedpractice {
namespace {
// .NET Math.Round uses ties-to-even; do not depend on the process rounding mode.
double ClanRound(double value, double scale) {
    const double scaled = value * scale, lower = std::floor(scaled);
    const double fraction = scaled - lower;
    return (fraction < .5 ? lower : fraction > .5 ? lower + 1 :
        (std::fmod(lower, 2.0) == 0 ? lower : lower + 1)) / scale;
}
bool ClanScoreBetter(const ClanScore& a, const ClanScore& b) {
    if (ClanRound(a.pp, 100) != ClanRound(b.pp, 100)) return ClanRound(a.pp, 100) > ClanRound(b.pp, 100);
    if (ClanRound(a.accuracy, 10000) != ClanRound(b.accuracy, 10000))
        return ClanRound(a.accuracy, 10000) > ClanRound(b.accuracy, 10000);
    if (a.timepost != b.timepost) return a.timepost < b.timepost;
    return a.playerId < b.playerId;
}
std::vector<ClanScore> ReplaceClanScore(std::vector<ClanScore> scores, const std::string& id,
                                        const ClanScore* replacement) {
    scores.erase(std::remove_if(scores.begin(), scores.end(), [&](const auto& s) { return s.playerId == id; }), scores.end());
    if (replacement) scores.push_back(*replacement);
    return scores;
}
std::string ClanHash(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}
}

double WeightedClanPp(std::vector<ClanScore> scores) {
    std::sort(scores.begin(), scores.end(), ClanScoreBetter);
    double total = 0, weight = 1;
    for (const auto& score : scores) { total += score.pp * weight; weight *= .8; }
    return total;
}

bool EvaluateClanImpact(ClanMapInput input, const std::string& playerId,
                        double minClearRate, MapEntry& result, std::optional<ClanImpact>* projection) {
    if (projection) projection->reset();
    auto& chart = input.chart;
    minClearRate = std::isfinite(minClearRate) ? std::clamp(minClearRate, 0.0, .95) : .5;
    if (chart.difficulties.size() != 1 || !std::isfinite(chart.predictedPp) || chart.predictedPp <= 0 ||
        !std::isfinite(chart.clearChance) || chart.clearChance < minClearRate ||
        !std::isfinite(input.clan.pp) || !std::isfinite(input.rivalPp)) return false;
    auto scores = input.clan.scores;
    double baseline = input.clan.pp, rival = input.rivalPp;
    const auto& existingScores = input.hypothetical ? input.primaryClan.scores : scores;
    const auto found = std::find_if(existingScores.begin(), existingScores.end(),
        [&](const auto& s) { return s.playerId == playerId; });
    // Associated scores are authoritative for the General-context contribution.
    const ClanScore* existing = found == existingScores.end() ? nullptr : &*found;
    const double currentPp = existing ? existing->pp : chart.currentPp;
    if (input.hypothetical) {
        const double before = WeightedClanPp(scores);
        scores = ReplaceClanScore(std::move(scores), playerId, existing);
        baseline += WeightedClanPp(scores) - before;
        const auto removed = ReplaceClanScore(input.primaryClan.scores, playerId, nullptr);
        rival = std::max(rival, input.primaryClan.pp + WeightedClanPp(removed) - WeightedClanPp(input.primaryClan.scores));
    }
    const double before = WeightedClanPp(scores);
    const auto gainAt = [&](double pp, double accuracy) {
        const ClanScore predicted{playerId, pp, accuracy, std::numeric_limits<double>::max()};
        return WeightedClanPp(ReplaceClanScore(scores, playerId, &predicted)) - before;
    };
    ClanImpact impact;
    impact.hypothetical = input.hypothetical;
    impact.currentClanPp = baseline;
    impact.rivalPp = rival;
    impact.gap = std::max(.01, rival - baseline + .01);
    impact.gain = gainAt(chart.predictedPp, chart.predictedAccuracy);
    impact.captures = impact.gain + 1e-8 >= impact.gap;
    if (projection) *projection = impact;
    if ((!input.hypothetical && input.held) || baseline > rival + 1e-8 || chart.predictedPp <= currentPp) return false;
    if (!std::isfinite(impact.gain) || impact.gain <= 0 || (!impact.captures && impact.gain < 1.0)) return false;
    const double chance = minClearRate == 0 ? 1 : chart.clearChance;
    impact.gapClosure = chance * std::min(1.0, impact.gain / impact.gap);
    impact.adjustedGain = chance * impact.gain;
    // Solve in accuracy space, using each chart's PPv3/PPv4 formula and the same
    // weighted insertion function as the projected score. 1.0 means theoretical maximum.
    const auto& difficulty = chart.difficulties.front();
    if (gainAt(PredictBeatLeaderPp(1.0, difficulty), 1.0) >= impact.gap) {
        double lo = 0, hi = 1;
        for (int i = 0; i < 60; ++i) {
            const double mid = (lo + hi) / 2;
            if (gainAt(PredictBeatLeaderPp(mid, difficulty), mid) >= impact.gap) hi = mid;
            else lo = mid;
        }
        impact.requiredAccuracy = hi;
        impact.requiredPp = PredictBeatLeaderPp(hi, difficulty);
    }
    result = std::move(chart);
    result.currentPp = currentPp;
    result.isImprovement = currentPp > 0;
    result.clanImpact = impact;
    return true;
}

bool ClanRecommendationBetter(const MapEntry& a, const MapEntry& b) {
    const auto& x = *a.clanImpact; const auto& y = *b.clanImpact;
    if (x.captures != y.captures) return x.captures;
    if (x.gapClosure != y.gapClosure) return x.gapClosure > y.gapClosure;
    if (x.adjustedGain != y.adjustedGain) return x.adjustedGain > y.adjustedGain;
    if (a.confidence != b.confidence) return a.confidence > b.confidence;
    if (a.songName != b.songName) return a.songName < b.songName;
    if (a.hash != b.hash) return a.hash < b.hash;
    return a.leaderboardId < b.leaderboardId;
}
std::vector<MapEntry> SelectClanRecommendations(std::vector<MapEntry> entries, int limit) {
    std::unordered_map<std::string, MapEntry> unique;
    for (auto& entry : entries) {
        if (!entry.clanImpact) continue;
        auto [it, inserted] = unique.try_emplace(ClanHash(entry.hash), entry);
        if (!inserted && ClanRecommendationBetter(entry, it->second)) it->second = std::move(entry);
    }
    entries.clear();
    for (auto& [_, entry] : unique) entries.push_back(std::move(entry));
    std::sort(entries.begin(), entries.end(), ClanRecommendationBetter);
    if (limit >= 0 && entries.size() > static_cast<std::size_t>(limit)) entries.resize(limit);
    return entries;
}
int ClanVerificationBudget(int trackLimit) { return 4 * std::clamp(trackLimit, 16, 64); }
std::vector<MapEntry> SelectLegacyClanRecommendations(std::vector<MapEntry> entries, double maxStars, int limit) {
    if (std::isnan(maxStars) || maxStars<=0) return {};
    auto better=[](const auto& a,const auto& b) {
        if (a.priority!=b.priority) return a.priority>b.priority;
        if (a.songName!=b.songName) return a.songName<b.songName;
        if (a.hash!=b.hash) return a.hash<b.hash;
        return a.leaderboardId<b.leaderboardId;
    };
    std::unordered_map<std::string,MapEntry> unique;
    for (auto& entry:entries) {
        if (entry.hash.size()!=40 || !std::all_of(entry.hash.begin(),entry.hash.end(),[](unsigned char c) { return std::isxdigit(c); }) ||
            entry.songName.empty() || entry.leaderboardId.empty() ||
            entry.difficulties.size()!=1 || ClanHash(entry.difficulties[0].characteristic)!="standard" ||
            !std::isfinite(entry.stars) || entry.stars<=0 || entry.stars>maxStars || !std::isfinite(entry.priority) ||
            !std::isfinite(entry.currentPp) || !std::isfinite(entry.predictedPp) || entry.predictedPp<=entry.currentPp) continue;
        const auto key=ClanHash(entry.hash);
        auto found=unique.find(key);
        if (found==unique.end()) unique.emplace(key,std::move(entry));
        else if (better(entry,found->second)) found->second=std::move(entry);
    }
    entries.clear();
    for (auto& [_,entry]:unique) entries.push_back(std::move(entry));
    std::sort(entries.begin(),entries.end(),better);
    if (limit>=0 && entries.size()>static_cast<std::size_t>(limit)) entries.resize(limit);
    return entries;
}
bool ClanEvidenceMatches(const ClanSearchEvidence& evidence, const MapEntry& chart,
                         double clanPp, double rivalPp) {
    return evidence.leaderboardId == chart.leaderboardId &&
        std::abs(evidence.screenClanPp-clanPp) < .01 && std::abs(evidence.screenRivalPp-rivalPp) < .01 &&
        std::abs(evidence.currentPp-chart.currentPp) < .01 && std::abs(evidence.predictedPp-chart.predictedPp) < .01 &&
        std::abs(evidence.predictedAccuracy-chart.predictedAccuracy) < .00001;
}
ClanScreenCandidate ScreenClanCandidate(const MapEntry& chart, double clanPp, double rivalPp,
                                       double minClearRate, const ClanSearchEvidence* evidence) {
    double gain = std::max(0.0,chart.predictedPp-chart.currentPp);
    double gap = rivalPp < 0 ? std::numeric_limits<double>::infinity() : std::max(.01,rivalPp-clanPp+.01);
    if (evidence && ClanEvidenceMatches(*evidence,chart,clanPp,rivalPp)) {
        // Reuse the measured weighted effect for screening only. In particular,
        // stop repeatedly favoring raw personal PP that the clan barely receives.
        gain = evidence->useful ? std::max(0.0,evidence->gain) : 0;
        gap = std::max(.01,evidence->rivalPp-evidence->baselinePp+.01);
    }
    const double chance = minClearRate == 0 ? 1 : chart.clearChance;
    return {chart,chance*std::min(1.0,gain/gap),chance*gain,gain>0 && gain>=gap};
}
std::vector<ClanScreenCandidate> ShortlistClanCandidates(std::vector<ClanScreenCandidate> entries,
                                                        int budget, std::size_t& cursor,
                                                        const std::vector<std::string>& previous) {
    budget = std::max(0, budget);
    auto better = [](const auto& a, const auto& b) {
        if (a.optimisticGapClosure != b.optimisticGapClosure) return a.optimisticGapClosure > b.optimisticGapClosure;
        if (a.optimisticGain != b.optimisticGain) return a.optimisticGain > b.optimisticGain;
        return a.chart.leaderboardId < b.chart.leaderboardId;
    };
    std::sort(entries.begin(), entries.end(), better);
    std::unordered_set<std::string> chosen;
    std::vector<ClanScreenCandidate> result;
    auto add = [&](const auto& entry) {
        if (result.size() < static_cast<std::size_t>(budget) && chosen.insert(entry.chart.leaderboardId).second)
            result.push_back(entry);
    };
    std::unordered_map<std::string,const ClanScreenCandidate*> byId;
    for (const auto& entry : entries) byId.emplace(entry.chart.leaderboardId,&entry);
    for (const auto& id : previous) {
        if (result.size() >= static_cast<std::size_t>(budget/2)) break;
        if (const auto found=byId.find(id); found!=byId.end()) add(*found->second);
    }
    const int remaining = budget-static_cast<int>(result.size());
    const auto captureEnd = result.size()+remaining/2;
    for (const auto& entry : entries) {
        if (result.size() >= captureEnd) break;
        if (entry.optimisticCapture) add(entry);
    }
    const auto progressEnd = result.size() + remaining / 4;
    for (const auto& entry : entries) { if (result.size() >= progressEnd) break; add(entry); }
    std::vector<ClanScreenCandidate> rest;
    for (const auto& entry : entries) if (!chosen.contains(entry.chart.leaderboardId)) rest.push_back(entry);
    std::sort(rest.begin(), rest.end(), [](const auto& a, const auto& b) { return a.chart.leaderboardId < b.chart.leaderboardId; });
    const auto exploration = std::min<std::size_t>(remaining / 4, rest.size());
    if (!rest.empty()) cursor %= rest.size();
    for (std::size_t i = 0; i < exploration; ++i) add(rest[(cursor + i) % rest.size()]);
    if (!rest.empty()) cursor = (cursor + exploration) % rest.size();
    for (const auto& entry : entries) add(entry);
    return result;
}
} // namespace rankedpractice
