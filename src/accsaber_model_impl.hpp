// Included by recommendation.cpp inside rankedpractice, sharing the fitted
// accuracy/clearability algorithms without sharing PP formulas or star scales.
namespace {
double AccRound(double value, double scale = 1e6) { return std::floor(value * scale + .5) / scale; }
bool AccSpeedModified(const MapEntry& entry) {
    const std::string mods = "," + entry.modifiers + ",";
    return mods.find(",FS,") != std::string::npos || mods.find(",SF,") != std::string::npos ||
           mods.find(",SS,") != std::string::npos;
}
std::vector<MapEntry> AccCategoryScores(const std::vector<MapEntry>& scores, const std::string& category, bool training) {
    std::vector<MapEntry> result;
    for (auto entry : scores) {
        if (!entry.ranked || entry.categoryCode != category || entry.difficulties.size() != 1 ||
            !entry.difficulties[0].complexity || (training && (entry.noFail || AccSpeedModified(entry)))) continue;
        entry.stars = *entry.difficulties[0].complexity;
        entry.modifiedStars = 0;
        entry.difficulties[0].stars = entry.stars;
        result.push_back(std::move(entry));
    }
    return result;
}
double AccTotal(std::vector<double> values, const AccSaberCurve& weight) {
    std::sort(values.begin(), values.end(), std::greater<double>());
    double total = 0;
    for (size_t i = 0; i < values.size(); ++i) total += AccRound(values[i] * AccSaberPositionWeight(i, weight));
    return AccRound(total);
}
} // namespace

double AccSaberAp(double accuracy, double complexity, const AccSaberCurve& curve) {
    if (!std::isfinite(accuracy) || accuracy < 0 || accuracy > 1 || !std::isfinite(complexity) ||
        complexity < 0 || curve.points.empty()) return 0;
    const auto upper = std::lower_bound(curve.points.begin(), curve.points.end(), accuracy,
        [](const auto& point, double x) { return point.first < x; });
    double normalized;
    if (upper == curve.points.begin()) normalized = upper->second;
    else if (upper == curve.points.end()) normalized = curve.points.back().second;
    else {
        const auto& lower = *std::prev(upper);
        normalized = lower.second + (accuracy - lower.first) * (upper->second - lower.second) / (upper->first - lower.first);
    }
    const double value = normalized * (complexity - curve.shift) * curve.scale;
    return std::isfinite(value) && value > 0 ? AccRound(value) : 0;
}

double AccSaberPositionWeight(size_t position, const AccSaberCurve& curve) {
    const double denominator = curve.y1 * std::exp(curve.k * curve.x1) - 1;
    if (curve.k <= 0 || curve.y1 <= 0 || curve.y1 >= 1 || denominator <= 0) return 0;
    const double x0 = -std::log((1 - curve.y1) / denominator) / curve.k;
    const double exponent = curve.k * (static_cast<double>(position) - x0);
    return exponent > 700 ? 0 : (1 + std::exp(-curve.k * x0)) / (1 + std::exp(exponent));
}

bool HasAccSaberEvidence(const std::vector<MapEntry>& scores, const std::string& category) {
    return AccuracyCurve(AccCategoryScores(scores, category, true), RatingSystem::AccSaber, true).HasObservations();
}

AbilityChart BuildAccSaberAbilityChart(const std::vector<MapEntry>& scores, const std::string& category) {
    return BuildAbilityChart(AccCategoryScores(scores, category, true), true, RatingSystem::AccSaber);
}

std::vector<MapEntry> SelectAccSaberRecommendations(const std::vector<MapEntry>& catalog,
    const std::vector<MapEntry>& scores, const std::vector<AccSaberCategory>& categories,
    const std::vector<Attempt>& attempts, const std::string& filter, ListKind kind, int limit, double minClearRate) {
    minClearRate = std::isfinite(minClearRate) ? std::clamp(minClearRate, 0.0, .95) : .5;
    std::vector<MapEntry> eligible;
    for (const auto& category : categories) {
        if (filter == "overall" ? !category.countForOverall : category.code != filter) continue;
        const auto training = AccCategoryScores(scores, category.code, true);
        const AccuracyCurve accuracy(training, RatingSystem::AccSaber, true);
        if (!accuracy.HasObservations()) continue;
        const auto baseline = AccCategoryScores(scores, category.code, false);
        std::unordered_map<std::string, MapEntry> best;
        for (const auto& score : baseline) {
            const auto key = ScoreKey(score, score.difficulties[0]);
            if (!best.contains(key) || score.scoreNoMods > best[key].scoreNoMods) best[key] = score;
        }
        std::vector<double> currentValues;
        for (const auto& [key, score] : best) currentValues.push_back(score.pp);
        const double before = AccTotal(currentValues, category.weightCurve);
        // Clearability's zero means unknown stars. Shift this private copy by
        // one so valid complexity zero retains exactly the same distance metric.
        auto clearScores = AccCategoryScores(scores, category.code, false);
        clearScores.erase(std::remove_if(clearScores.begin(), clearScores.end(), AccSpeedModified), clearScores.end());
        for (auto& score : clearScores) { score.stars += 1; score.difficulties[0].stars += 1; }
        std::unordered_map<std::string, double> ratings;
        for (const auto& chart : catalog) if (chart.categoryCode == category.code && chart.ranked &&
            chart.difficulties.size() == 1 && chart.difficulties[0].complexity)
            ratings[ScoreKey(chart, chart.difficulties[0])] = *chart.difficulties[0].complexity + 1;
        std::vector<Attempt> categoryAttempts;
        for (auto attempt : attempts) if (const auto it = ratings.find(AttemptChartKey(attempt)); it != ratings.end()) {
            attempt.stars = it->second;
            categoryAttempts.push_back(std::move(attempt));
        }
        const ClearabilityModel clearability(clearScores, categoryAttempts, RatingSystem::AccSaber);
        for (auto chart : catalog) {
            if (!chart.ranked || chart.categoryCode != category.code || chart.difficulties.size() != 1 ||
                !chart.difficulties[0].complexity || Canonical(chart.difficulties[0].characteristic) != "standard" ||
                !IsValidSongHash(chart.hash) || chart.maxScore <= 0) continue;
            const auto key = ScoreKey(chart, chart.difficulties[0]);
            const auto existing = best.find(key);
            const bool played = existing != best.end() && !existing->second.noFail;
            if ((kind == ListKind::NotPlayed && played) || (kind == ListKind::ToImprove && !played)) continue;
            const double complexity = *chart.difficulties[0].complexity;
            const double predictedScore = std::floor(accuracy.Predict(complexity) * chart.maxScore);
            if (existing != best.end() && predictedScore <= existing->second.scoreNoMods) continue;
            chart.predictedAccuracy = AccRound(predictedScore / chart.maxScore, 1e10);
            chart.predictedPp = AccSaberAp(chart.predictedAccuracy, complexity, category.scoreCurve);
            chart.currentPp = existing == best.end() ? 0 : existing->second.pp;
            std::vector<double> changed;
            for (const auto& [scoreKey, score] : best) if (scoreKey != key) changed.push_back(score.pp);
            changed.push_back(chart.predictedPp);
            chart.weightedPpGain = AccRound(AccTotal(changed, category.weightCurve) - before);
            if (chart.weightedPpGain <= 0 || chart.predictedPp <= 0) continue;
            chart.clearChance = clearability.Predict(complexity + 1, key);
            chart.isImprovement = played;
            chart.priority = played ? chart.weightedPpGain : RecommendationPriority(chart.weightedPpGain,
                chart.clearChance, minClearRate, kind == ListKind::Combined);
            if (chart.priority <= 0) continue;
            chart.confidence = std::clamp(accuracy.LocalSupport(complexity) / 3.0, 0.0, 1.0);
            if (!played) chart.confidence = std::min(chart.confidence, std::clamp(clearability.LocalSupport(complexity + 1) / 4.0, 0.0, 1.0));
            const auto [clears, noFail] = clearability.NearbyCounts(complexity + 1);
            chart.nearbyClears = clears; chart.nearbyNoFail = noFail;
            chart.nearbyFailures = clearability.NearbyFailures(complexity + 1);
            chart.stars = complexity; chart.difficulties[0].stars = complexity;
            chart.categoryLabel = filter == "overall" ? "" : AccSaberCategoryName(category.code);
            eligible.push_back(std::move(chart));
        }
    }
    std::sort(eligible.begin(), eligible.end(), [](const auto& a, const auto& b) {
        if (a.priority != b.priority) return a.priority > b.priority;
        if (a.songName != b.songName) return a.songName < b.songName;
        return a.leaderboardId < b.leaderboardId;
    });
    std::unordered_map<std::string, bool> emitted;
    std::vector<MapEntry> result;
    for (auto& chart : eligible) if (emitted.emplace(Canonical(chart.hash), true).second) {
        result.push_back(std::move(chart));
        if (limit >= 0 && result.size() >= static_cast<size_t>(limit)) break;
    }
    return result;
}
