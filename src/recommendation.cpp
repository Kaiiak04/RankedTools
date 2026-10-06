#include "recommendation.hpp"
#include "ability_chart.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <utility>

namespace rankedpractice {
namespace {

std::string Canonical(const std::string& value) {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

bool IsValidSongHash(const std::string& value) {
    return value.size() == 40 && std::all_of(value.begin(), value.end(),
        [](unsigned char c) { return std::isxdigit(c) != 0; });
}

constexpr double kMinimumTrainingAccuracy = 0.55;
constexpr double kWeightDecay = 0.965;

struct AccuracyObservation {
    double stars;
    double accuracy;
    double weight;
};

double ObservationWeight(const MapEntry& entry, double now) {
    if (entry.timepost <= 0.0) return 0.65;
    const double ageDays = std::max(0.0, (now - entry.timepost) / 86400.0);
    return std::exp(-ageDays / 365.0);
}

class AccuracyCurve {
public:
    explicit AccuracyCurve(const std::vector<MapEntry>& scores) {
        const double now = std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::unordered_map<std::string, const MapEntry*> uniqueScores;
        for (const auto& entry : scores) {
            if (entry.noFail || !entry.ranked) continue;
            const auto standard = std::find_if(entry.difficulties.begin(), entry.difficulties.end(),
                [](const Difficulty& difficulty) { return Canonical(difficulty.characteristic) == "standard"; });
            if (entry.hash.empty() || standard == entry.difficulties.end()) continue;
            const auto key = Canonical(entry.hash) + "|standard|" + Canonical(standard->name);
            auto found = uniqueScores.find(key);
            if (found == uniqueScores.end() || entry.pp > found->second->pp) uniqueScores[key] = &entry;
        }
        for (const auto& [_, score] : uniqueScores) {
            const auto& entry = *score;
            const double profileStars = entry.modifiedStars > 0.0 ? entry.modifiedStars : entry.stars;
            if (!std::isfinite(profileStars) || !std::isfinite(entry.accuracy) || !std::isfinite(entry.pp) ||
                profileStars <= 0.0 || entry.accuracy < kMinimumTrainingAccuracy ||
                entry.accuracy > 1.0 || entry.pp <= 0.0) continue;
            const double weight = ObservationWeight(entry, now);
            if (!std::isfinite(weight) || weight <= 0.0) continue;
            observations_.push_back({profileStars, entry.accuracy, weight});
            minStars_ = std::min(minStars_, profileStars);
            maxStars_ = std::max(maxStars_, profileStars);
        }
        if (observations_.empty()) return;

        std::sort(observations_.begin(), observations_.end(), [](const auto& a, const auto& b) {
            if (a.stars != b.stars) return a.stars < b.stars;
            if (a.accuracy != b.accuracy) return a.accuracy < b.accuracy;
            return a.weight < b.weight;
        });
        // Equal star ratings must share one prediction, regardless of chart or input order.
        for (const auto& observation : observations_) {
            if (!knots_.empty() && knots_.back().stars == observation.stars) {
                auto& knot = knots_.back();
                const double weight = knot.weight + observation.weight;
                knot.accuracy = (knot.weight * knot.accuracy + observation.weight * observation.accuracy) / weight;
                knot.weight = weight;
            } else {
                knots_.push_back(observation);
            }
        }

        struct Block {
            size_t first, last;
            double weight, weightedAccuracy;
            double Mean() const { return weightedAccuracy / weight; }
        };
        std::vector<Block> blocks;
        // Pool-adjacent-violators solves weighted least squares subject to accuracy
        // being non-increasing with stars. Merging may expose earlier violations.
        for (size_t i = 0; i < knots_.size(); ++i) {
            blocks.push_back({i, i, knots_[i].weight, knots_[i].weight * knots_[i].accuracy});
            while (blocks.size() >= 2 && blocks[blocks.size() - 2].Mean() < blocks.back().Mean()) {
                const auto right = blocks.back();
                blocks.pop_back();
                auto& left = blocks.back();
                left.last = right.last;
                left.weight += right.weight;
                left.weightedAccuracy += right.weightedAccuracy;
            }
        }
        for (const auto& block : blocks) {
            for (size_t i = block.first; i <= block.last; ++i) knots_[i].accuracy = block.Mean();
        }
    }

    double Predict(double stars) const {
        if (knots_.empty()) return 0.85;
        // Linear interpolation preserves the monotonic fit between observed stars.
        // Hold endpoints for one extra star, then retain the cautious tail adjustments.
        double predicted;
        if (stars <= minStars_) predicted = knots_.front().accuracy;
        else if (stars >= maxStars_) predicted = knots_.back().accuracy;
        else {
            const auto upper = std::upper_bound(knots_.begin(), knots_.end(), stars,
                [](double value, const AccuracyObservation& knot) { return value < knot.stars; });
            const auto& lower = *std::prev(upper);
            const double fraction = (stars - lower.stars) / (upper->stars - lower.stars);
            predicted = lower.accuracy + fraction * (upper->accuracy - lower.accuracy);
        }
        if (stars > maxStars_ + 1.0) predicted -= 0.012 * (stars - maxStars_ - 1.0);
        if (stars < minStars_ - 1.0) predicted += 0.006 * (minStars_ - 1.0 - stars);
        return std::clamp(predicted, 0.55, 0.999);
    }

    bool HasObservations() const { return !observations_.empty(); }
    double MinStars() const { return minStars_; }
    double MaxStars() const { return maxStars_; }
    const auto& Observations() const { return observations_; }
    const auto& Knots() const { return knots_; }
    double LocalSupport(double stars) const {
        double support = 0.0;
        for (const auto& observation : observations_) {
            const double distance = (observation.stars - stars) / 0.8;
            support += observation.weight * std::exp(-0.5 * distance * distance);
        }
        return support;
    }

private:
    std::vector<AccuracyObservation> observations_;
    std::vector<AccuracyObservation> knots_;
    double minStars_{std::numeric_limits<double>::max()};
    double maxStars_{0.0};
};

struct ClearObservation {
    double stars;
    double accuracy;
    double weight;
    bool cleared;
    bool realFailure{false};
    std::string chartKey;
};

class ClearabilityModel {
public:
    explicit ClearabilityModel(const std::vector<MapEntry>& scores, const std::vector<Attempt>& attempts = {}) {
        const double now = std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::unordered_map<std::string, const MapEntry*> clears;
        std::unordered_map<std::string, const MapEntry*> noFailAttempts;
        std::unordered_map<std::string, std::vector<const Attempt*>> actual;
        for (const auto& a : attempts) if (IsTrainingAttempt(a)) actual[AttemptChartKey(a)].push_back(&a);
        for (const auto& entry : scores) {
            const auto standard = std::find_if(entry.difficulties.begin(), entry.difficulties.end(),
                [](const Difficulty& difficulty) { return Canonical(difficulty.characteristic) == "standard"; });
            if (!entry.ranked || entry.hash.empty() || standard == entry.difficulties.end() || entry.stars <= 0.0) continue;
            const auto key = Canonical(entry.hash) + "|standard|" + Canonical(standard->name);
            if (entry.noFail) {
                auto found = noFailAttempts.find(key);
                const double accuracy = entry.baseAccuracy > 0.0 ? entry.baseAccuracy : entry.accuracy;
                if (found == noFailAttempts.end() || accuracy >
                    (found->second->baseAccuracy > 0.0 ? found->second->baseAccuracy : found->second->accuracy)) {
                    noFailAttempts[key] = &entry;
                }
            } else if (entry.accuracy > 0.0) {
                auto found = clears.find(key);
                if (found == clears.end() || entry.pp > found->second->pp) clears[key] = &entry;
            }
        }
        std::vector<double> clearStars;
        clearStars.reserve(clears.size());
        for (const auto& [key, runs] : actual) {
            const auto completion = std::find_if(runs.begin(), runs.end(), [](const Attempt* a) {
                return a->outcome == AttemptOutcome::Clear;
            });
            if (completion != runs.end() && !clears.contains(key)) clearStars.push_back((*completion)->stars);
            double support = 0.0;
            for (const auto* a : runs) support += std::exp(-std::max(0.0, now - a->timestamp) / (86400.0 * 365.0));
            const double cap = support > 4.0 ? 4.0 / support : 1.0;
            for (const auto* a : runs) {
                const double weight = cap * std::exp(-std::max(0.0, now - a->timestamp) / (86400.0 * 365.0));
                observations_.push_back({a->stars, 0.0, weight, a->outcome == AttemptOutcome::Clear,
                                         a->outcome == AttemptOutcome::Fail, key});
            }
        }
        for (const auto& [_, entry] : clears) {
            const double stars = entry->modifiedStars > 0.0 ? entry->modifiedStars : entry->stars;
            clearStars.push_back(stars);
            // A leaderboard PB is fallback evidence, never an extra attempt
            // alongside observed completions of that chart.
            const auto found = actual.find(_);
            if (found != actual.end() && std::any_of(found->second.begin(), found->second.end(),
                [](const Attempt* a) { return a->outcome == AttemptOutcome::Clear; })) continue;
            observations_.push_back({stars, entry->accuracy, ObservationWeight(*entry, now), true, false, _});
        }
        for (const auto& [key, entry] : noFailAttempts) {
            if (clears.contains(key) || actual.contains(key)) continue;
            const double stars = entry->modifiedStars > 0.0 ? entry->modifiedStars : entry->stars;
            const double accuracy = entry->baseAccuracy > 0.0 ? entry->baseAccuracy : entry->accuracy;
            observations_.push_back({stars, accuracy, ObservationWeight(*entry, now), false, false, key});
        }
        if (!clearStars.empty()) {
            std::sort(clearStars.begin(), clearStars.end());
            const auto index = static_cast<size_t>(std::floor(0.9 * (clearStars.size() - 1)));
            supportedStars_ = clearStars[index];
        }
    }

    double Predict(double stars, const std::string& chartKey = {}) const {
        double clears = 0.0;
        double noFails = 0.0;
        double chartClears = 0.0, chartFailures = 0.0;
        for (const auto& observation : observations_) {
            const double distance = (observation.stars - stars) / 0.8;
            double weight = observation.weight * std::exp(-0.5 * distance * distance);
            if (observation.cleared) {
                clears += weight;
            } else {
                // A high-accuracy NF practice run is weaker evidence of an
                // unplayable chart than one with many misses.
                if (!observation.realFailure) weight *= std::clamp((0.75 - observation.accuracy) / 0.4, 0.4, 1.4);
                noFails += weight;
            }
            if (!chartKey.empty() && observation.chartKey == chartKey) {
                if (observation.cleared) chartClears += observation.weight;
                else if (observation.realFailure) chartFailures += observation.weight;
            }
        }
        const double offset = std::clamp((stars - supportedStars_) / 0.7, -30.0, 30.0);
        const double prior = 1.0 / (1.0 + std::exp(offset));
        double chance = (clears + 2.0 * prior) / (clears + noFails + 2.0);
        if (chartClears + chartFailures > 0.0)
            chance = (chartClears + 2.0 * chance) / (chartClears + chartFailures + 2.0);
        return std::clamp(chance, 0.02, 0.98);
    }

    double LocalSupport(double stars) const {
        double support = 0.0;
        for (const auto& observation : observations_) {
            const double distance = (observation.stars - stars) / 0.8;
            support += observation.weight * std::exp(-0.5 * distance * distance);
        }
        return support;
    }

    std::pair<int, int> NearbyCounts(double stars) const {
        int clears = 0;
        int noFails = 0;
        for (const auto& observation : observations_) {
            if (std::abs(observation.stars - stars) > 1.0) continue;
            if (observation.cleared) ++clears;
            else if (!observation.realFailure) ++noFails;
        }
        return {clears, noFails};
    }
    int NearbyFailures(double stars) const {
        return std::count_if(observations_.begin(), observations_.end(), [stars](const auto& a) {
            return a.realFailure && std::abs(a.stars - stars) <= 1.0;
        });
    }

private:
    std::vector<ClearObservation> observations_;
    double supportedStars_{7.0};
};

double RecommendationPriority(double weightedGain, double clearChance, double minClearRate,
                               bool rankByWeightedGain = false) {
    if (weightedGain <= 0.0 || clearChance < minClearRate) return 0.0;
    return minClearRate == 0.0 || rankByWeightedGain ? weightedGain : weightedGain * clearChance;
}

double InterpolateCurve(double accuracy,
                        const std::pair<double, double>* points,
                        size_t count) {
    accuracy = std::clamp(accuracy, points[0].first, points[count - 1].first);
    auto upper = std::lower_bound(points, points + count, accuracy,
        [](const auto& point, double value) { return point.first < value; });
    if (upper == points) return upper->second;
    if (upper == points + count) return points[count - 1].second;
    const auto& right = *upper;
    const auto& left = *(upper - 1);
    const double fraction = (accuracy - left.first) / (right.first - left.first);
    return left.second + fraction * (right.second - left.second);
}

double ScoreSaberPp(double stars, double accuracy) {
    // ScoreSaber's current V3 accuracy curve, expressed as a multiplier
    // relative to 95% accuracy.
    static constexpr std::pair<double, double> points[] = {
        {0.00000, 0.000000}, {0.60000, 0.182232}, {0.65000, 0.586601},
        {0.70000, 0.612557}, {0.75000, 0.645181}, {0.80000, 0.687227},
        {0.82500, 0.715047}, {0.85000, 0.746229}, {0.87500, 0.781693},
        {0.90000, 0.825756}, {0.91000, 0.848838}, {0.92000, 0.872871},
        {0.93000, 0.904000}, {0.94000, 0.941736}, {0.95000, 1.000000},
        {0.95500, 1.038863}, {0.96000, 1.087188}, {0.96500, 1.155212},
        {0.97000, 1.248581}, {0.97250, 1.309033}, {0.97500, 1.380710},
        {0.97750, 1.466473}, {0.98000, 1.570241}, {0.98250, 1.697536},
        {0.98500, 1.856389}, {0.98750, 2.058948}, {0.99000, 2.324507},
        {0.99125, 2.490291}, {0.99250, 2.685668}, {0.99375, 2.919016},
        {0.99500, 3.202202}, {0.99625, 3.552614}, {0.99750, 3.996794},
        {0.99825, 4.325027}, {0.99900, 4.715471}, {0.99950, 5.019544},
        {1.00000, 5.367394}
    };
    return 42.1168 * stars * InterpolateCurve(accuracy, points, std::size(points));
}

double BeatLeaderPp(double accuracy, const Difficulty& difficulty) {
    if (difficulty.accRating <= 0.0 || difficulty.passRating <= 0.0 || difficulty.techRating < 0.0) return 0.0;
    static constexpr std::pair<double, double> points[] = {
        {0.00, 0.000}, {0.60, 0.256}, {0.65, 0.296}, {0.70, 0.345},
        {0.75, 0.404}, {0.80, 0.473}, {0.825, 0.522}, {0.85, 0.581},
        {0.875, 0.650}, {0.90, 0.729}, {0.91, 0.768}, {0.92, 0.813},
        {0.93, 0.867}, {0.94, 0.931}, {0.95, 1.000}, {0.955, 1.039},
        {0.96, 1.094}, {0.965, 1.167}, {0.97, 1.256}, {0.9725, 1.315},
        {0.975, 1.392}, {0.9775, 1.490}, {0.98, 1.618}, {0.9825, 1.786},
        {0.985, 2.007}, {0.9875, 2.303}, {0.99, 2.700}, {0.9925, 3.241},
        {0.995, 4.010}, {0.9975, 5.158}, {0.999, 6.241}, {1.00, 7.424}
    };
    const double passPp = std::max(0.0, 15.2 * std::exp(std::pow(difficulty.passRating, 1.0 / 2.62)) - 30.0);
    const double accPp = InterpolateCurve(accuracy, points, std::size(points)) * difficulty.accRating * 34.0;
    const double techPp = std::exp(1.9 * accuracy) * 1.08 * difficulty.techRating;
    const double raw = passPp + accPp + techPp;
    return std::isfinite(raw) && raw > 0.0 ? std::pow(raw / 650.0, 1.3) * 650.0 : 0.0;
}

std::string ScoreKey(const MapEntry& entry, const Difficulty& difficulty) {
    return Canonical(entry.hash) + "|" + Canonical(difficulty.characteristic) + "|" + Canonical(difficulty.name);
}

struct WeightedProfile {
    std::vector<std::pair<std::string, double>> scores;
    double total{0.0};
};

WeightedProfile MakeWeightedProfile(const std::vector<MapEntry>& profile) {
    std::unordered_map<std::string, double> bestScores;
    for (const auto& score : profile) {
        if (score.noFail || !score.ranked || score.pp <= 0.0) continue;
        for (const auto& difficulty : score.difficulties) {
            const auto key = ScoreKey(score, difficulty);
            bestScores[key] = std::max(bestScores[key], score.pp);
        }
    }
    WeightedProfile result;
    result.scores.reserve(bestScores.size());
    for (const auto& item : bestScores) result.scores.push_back(item);
    std::sort(result.scores.begin(), result.scores.end(), [](const auto& left, const auto& right) {
        return left.second > right.second;
    });
    double weight = 1.0;
    for (const auto& score : result.scores) {
        result.total += score.second * weight;
        weight *= kWeightDecay;
    }
    return result;
}

double WeightedProfileChange(const WeightedProfile& profile,
                             const std::string& key,
                             double newPp,
                             bool insert) {
    std::vector<double> changed;
    changed.reserve(profile.scores.size() + (insert ? 1 : 0));
    bool foundExisting = false;
    double oldPp = 0.0;
    for (const auto& score : profile.scores) {
        if (score.first == key) {
            foundExisting = true;
            oldPp = score.second;
            if (!insert) continue;
        }
        changed.push_back(score.second);
    }
    if (insert && foundExisting) return 0.0;
    if (!insert && foundExisting && newPp <= oldPp) return 0.0;
    const auto position = std::lower_bound(changed.begin(), changed.end(), newPp, std::greater<double>());
    changed.insert(position, newPp);
    double after = 0.0;
    double weight = 1.0;
    for (const double pp : changed) {
        after += pp * weight;
        weight *= kWeightDecay;
    }
    return after - profile.total;
}

double CurrentProfilePp(const WeightedProfile& profile, const std::string& key) {
    const auto found = std::find_if(profile.scores.begin(), profile.scores.end(),
        [&key](const auto& score) { return score.first == key; });
    return found == profile.scores.end() ? 0.0 : found->second;
}

} // namespace

AbilityChart BuildAbilityChart(const std::vector<MapEntry>& scores) {
    const AccuracyCurve fit(scores);
    AbilityChart chart;
    if (!fit.HasObservations()) return chart;
    chart.minObservedStars = fit.MinStars();
    chart.maxObservedStars = fit.MaxStars();
    chart.maxStars = std::max(4.0, std::ceil(fit.MaxStars() + 2.0));
    for (const auto& point : fit.Observations()) {
        chart.scores.push_back({point.stars, point.accuracy, point.weight});
    }
    std::vector<double> positions{0.0, chart.maxStars};
    // Include exact knots and tail boundaries, so narrow drops are not smoothed away.
    for (const auto& knot : fit.Knots()) positions.push_back(knot.stars);
    positions.push_back(std::max(0.0, fit.MinStars() - 1.0));
    positions.push_back(fit.MaxStars() + 1.0);
    for (int i = 1; i < 256; ++i) positions.push_back(chart.maxStars * i / 256.0);
    std::sort(positions.begin(), positions.end());
    positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
    double minimum = 1.0;
    for (double stars : positions) {
        const double accuracy = fit.Predict(stars);
        chart.curve.push_back({stars, accuracy, 0.0});
        minimum = std::min(minimum, accuracy);
    }
    for (const auto& point : chart.scores) minimum = std::min(minimum, point.accuracy);
    chart.minAccuracy = std::max(0.5, std::floor((minimum - 0.025) * 20.0) / 20.0);
    return chart;
}

void ResolveAttemptRatings(std::vector<Attempt>& attempts, const std::vector<MapEntry>& charts) {
    std::unordered_map<std::string, double> ratings;
    for (const auto& chart : charts) {
        if (!chart.ranked) continue;
        for (const auto& difficulty : chart.difficulties) {
            if (difficulty.stars > 0.0) ratings[Canonical(chart.hash) + "|" +
                Canonical(difficulty.characteristic) + "|" + Canonical(difficulty.name)] = difficulty.stars;
        }
    }
    for (auto& a : attempts) {
        if (const auto found = ratings.find(AttemptChartKey(a)); found != ratings.end() && a.normal) a.stars = found->second;
    }
}

int NormalizeMinClearRatePercent(int percent) {
    return (std::clamp(percent, 0, 95) + 2) / 5 * 5;
}

std::vector<MapEntry> SelectRecommendations(std::vector<MapEntry> entries,
                                            ListKind kind,
                                            int limit,
                                            double /*targetStars*/,
                                            const std::vector<MapEntry>& profileScores,
                                            RatingSystem system,
                                            double minClearRate,
                                            bool rankByWeightedGain,
                                            const std::vector<Attempt>& attempts) {
    minClearRate = std::isfinite(minClearRate) ? std::clamp(minClearRate, 0.0, 0.95) : 0.50;
    AccuracyCurve accuracyCurve(profileScores.empty() ? entries : profileScores);
    auto resolvedAttempts = attempts;
    ResolveAttemptRatings(resolvedAttempts, profileScores);
    ResolveAttemptRatings(resolvedAttempts, entries);
    const ClearabilityModel clearability(profileScores.empty() ? entries : profileScores, resolvedAttempts);
    const auto weightedProfile = MakeWeightedProfile(profileScores.empty() ? entries : profileScores);
    std::unordered_map<std::string, MapEntry> unique;
    for (auto& entry : entries) {
        if (kind == ListKind::ToImprove && (entry.noFail || !entry.ranked)) continue;
        if (!IsValidSongHash(entry.hash) || entry.songName.empty() || entry.difficulties.empty()) continue;
        entry.difficulties.erase(std::remove_if(entry.difficulties.begin(), entry.difficulties.end(),
            [](const Difficulty& difficulty) {
                return Canonical(difficulty.characteristic) != "standard" || difficulty.stars <= 0.0;
            }), entry.difficulties.end());
        if (entry.difficulties.empty()) continue;
        if (entry.accuracy > 1.0) entry.accuracy /= 100.0;

        bool hasRecommendation = false;
        MapEntry selected = entry;
        double bestPriority = 0.0;
        for (const auto& difficulty : entry.difficulties) {
            const double predictedAccuracy = accuracyCurve.Predict(difficulty.stars);
            const double clearChance = clearability.Predict(difficulty.stars,
                Canonical(entry.hash) + "|" + Canonical(difficulty.characteristic) + "|" + Canonical(difficulty.name));
            const double predictedPp = system == RatingSystem::ScoreSaber
                ? ScoreSaberPp(difficulty.stars, predictedAccuracy)
                : BeatLeaderPp(predictedAccuracy, difficulty);
            if (predictedPp <= 0.0) continue;

            const auto scoreKey = ScoreKey(entry, difficulty);
            const double weightedGain = WeightedProfileChange(
                weightedProfile, scoreKey, predictedPp, kind == ListKind::NotPlayed);
            const double priority = kind == ListKind::NotPlayed
                ? RecommendationPriority(weightedGain, clearChance, minClearRate, rankByWeightedGain) : weightedGain;
            if (priority <= 0.0) continue;
            if (!hasRecommendation || priority > bestPriority) {
                hasRecommendation = true;
                bestPriority = priority;
                selected = entry;
                selected.difficulties = {difficulty};
                selected.stars = difficulty.stars;
                selected.priority = priority;
                selected.predictedAccuracy = predictedAccuracy;
                selected.predictedPp = predictedPp;
                selected.currentPp = CurrentProfilePp(weightedProfile, scoreKey);
                selected.isImprovement = kind == ListKind::ToImprove;
                selected.weightedPpGain = weightedGain;
                selected.clearChance = clearChance;
                selected.confidence = std::clamp(accuracyCurve.LocalSupport(difficulty.stars) / 3.0,
                                                  0.0, 1.0);
                if (kind == ListKind::NotPlayed) {
                    selected.confidence = std::min(selected.confidence,
                        std::clamp(clearability.LocalSupport(difficulty.stars) / 4.0, 0.0, 1.0));
                }
                const auto [nearbyClears, nearbyNoFail] = clearability.NearbyCounts(difficulty.stars);
                selected.nearbyClears = nearbyClears;
                selected.nearbyNoFail = nearbyNoFail;
                selected.nearbyFailures = clearability.NearbyFailures(difficulty.stars);
            }
        }
        if (!hasRecommendation) continue;

        // One playlist item per map. When the API returns several ranked
        // Standard charts for the same hash, keep the difficulty with the
        // greatest estimated profile PP gain.
        const auto key = Canonical(selected.hash);
        auto found = unique.find(key);
        if (found == unique.end()) {
            unique[key] = std::move(selected);
            continue;
        }
        if (selected.priority > found->second.priority) found->second = std::move(selected);
    }

    std::vector<MapEntry> result;
    result.reserve(unique.size());
    for (auto& [_, entry] : unique) result.push_back(std::move(entry));
    std::sort(result.begin(), result.end(), [](const MapEntry& left, const MapEntry& right) {
        if (left.priority != right.priority) return left.priority > right.priority;
        if (left.songName != right.songName) return left.songName < right.songName;
        return left.hash < right.hash;
    });
    if (limit >= 0 && result.size() > static_cast<size_t>(limit)) result.resize(static_cast<size_t>(limit));
    return result;
}

double EstimateRecommendationTargetStars(const std::vector<MapEntry>& profileScores,
                                         RatingSystem system,
                                         double minClearRate,
                                         const std::vector<Attempt>& attempts) {
    minClearRate = std::isfinite(minClearRate) ? std::clamp(minClearRate, 0.0, 0.95) : 0.50;
    const AccuracyCurve accuracyCurve(profileScores);
    auto resolvedAttempts = attempts;
    ResolveAttemptRatings(resolvedAttempts, profileScores);
    const ClearabilityModel clearability(profileScores, resolvedAttempts);
    const auto weightedProfile = MakeWeightedProfile(profileScores);
    auto cautiousTarget = [&clearability, minClearRate](double stars) {
        // High thresholds may be above every observed clear estimate. Move the
        // search band lower instead of silently reverting to a fixed 7-star band.
        while (stars > 1.0 && clearability.Predict(stars) < minClearRate) {
            stars = std::max(1.0, stars - 0.05);
        }
        return stars;
    };
    if (!accuracyCurve.HasObservations() || weightedProfile.scores.empty()) return cautiousTarget(7.0);

    double targetStars = 0.0;
    double bestGain = -1.0;
    if (system == RatingSystem::ScoreSaber) {
        const double minStars = std::max(1.0, accuracyCurve.MinStars());
        const double maxStars = std::min(20.0, accuracyCurve.MaxStars());
        for (double stars = minStars; stars <= maxStars + 1e-6; stars += 0.05) {
            const double pp = ScoreSaberPp(stars, accuracyCurve.Predict(stars));
            MapEntry hypothetical;
            hypothetical.hash = "rankedtools-target-" + std::to_string(static_cast<int>(stars * 100.0));
            Difficulty difficulty{"Standard", "ExpertPlus", stars};
            const double weightedGain = WeightedProfileChange(
                weightedProfile, ScoreKey(hypothetical, difficulty), pp, true);
            const double gain = RecommendationPriority(weightedGain, clearability.Predict(stars), minClearRate);
            if (gain <= 0.0) continue;
            if (gain > bestGain) {
                bestGain = gain;
                targetStars = stars;
            }
        }
    } else {
        size_t index = 0;
        std::unordered_map<std::string, bool> seen;
        for (const auto& entry : profileScores) {
            if (entry.noFail || !entry.ranked) continue;
            for (const auto& difficulty : entry.difficulties) {
                if (Canonical(difficulty.characteristic) != "standard" || difficulty.stars <= 0.0) continue;
                const auto scoreKey = ScoreKey(entry, difficulty);
                if (seen.contains(scoreKey)) continue;
                seen[scoreKey] = true;
                const double pp = BeatLeaderPp(accuracyCurve.Predict(difficulty.stars), difficulty);
                if (pp <= 0.0) continue;
                MapEntry hypothetical;
                hypothetical.hash = "rankedtools-target-" + std::to_string(index++);
                Difficulty targetDifficulty = difficulty;
                const double weightedGain = WeightedProfileChange(
                    weightedProfile, ScoreKey(hypothetical, targetDifficulty), pp, true);
                const double gain = RecommendationPriority(weightedGain,
                    clearability.Predict(difficulty.stars), minClearRate);
                if (gain <= 0.0) continue;
                if (gain > bestGain) {
                    bestGain = gain;
                    targetStars = difficulty.stars;
                }
            }
        }
    }
    return cautiousTarget(targetStars > 0.0 ? targetStars : 7.0);
}

std::vector<MapEntry> MergeRecommendations(std::vector<MapEntry> notPlayed,
                                           std::vector<MapEntry> toImprove,
                                           int limit) {
    std::unordered_map<std::string, MapEntry> unique;
    auto collect = [&unique](std::vector<MapEntry>& entries, bool improvement) {
        for (auto& entry : entries) {
            if (!IsValidSongHash(entry.hash) || entry.difficulties.empty() ||
                !std::isfinite(entry.weightedPpGain) || entry.weightedPpGain <= 0.0) continue;
            entry.priority = entry.weightedPpGain;
            entry.isImprovement = improvement;
            const auto key = Canonical(entry.hash);
            auto found = unique.find(key);
            if (found == unique.end() || entry.priority > found->second.priority ||
                (entry.priority == found->second.priority && improvement)) unique[key] = std::move(entry);
        }
    };
    collect(notPlayed, false);
    collect(toImprove, true);
    std::vector<MapEntry> result;
    result.reserve(unique.size());
    for (auto& [_, entry] : unique) result.push_back(std::move(entry));
    std::sort(result.begin(), result.end(), [](const MapEntry& left, const MapEntry& right) {
        if (left.priority != right.priority) return left.priority > right.priority;
        if (left.songName != right.songName) return left.songName < right.songName;
        return left.hash < right.hash;
    });
    if (limit >= 0 && result.size() > static_cast<size_t>(limit)) result.resize(static_cast<size_t>(limit));
    return result;
}

std::vector<MapEntry> SelectClanRecommendations(std::vector<MapEntry> entries,
                                                double minStars,
                                                double maxStars,
                                                int limit) {
    if (minStars > maxStars) std::swap(minStars, maxStars);
    std::unordered_map<std::string, MapEntry> unique;
    for (auto& entry : entries) {
        if (!IsValidSongHash(entry.hash) || entry.songName.empty() || entry.leaderboardId.empty() ||
            !std::isfinite(entry.pp) || entry.difficulties.empty()) continue;
        entry.difficulties.erase(std::remove_if(entry.difficulties.begin(), entry.difficulties.end(),
            [minStars, maxStars](const Difficulty& difficulty) {
                return Canonical(difficulty.characteristic) != "standard" ||
                       difficulty.stars < minStars || difficulty.stars > maxStars;
            }), entry.difficulties.end());
        if (entry.difficulties.empty()) continue;

        // The clan endpoint's signed PP field is the amount needed to pass the
        // current clan result; values closer to zero are nearest to conquest.
        entry.priority = entry.pp;
        const auto best = std::max_element(entry.difficulties.begin(), entry.difficulties.end(),
            [](const Difficulty& left, const Difficulty& right) { return left.stars < right.stars; });
        const Difficulty selectedDifficulty = *best;
        entry.difficulties = {selectedDifficulty};
        entry.stars = selectedDifficulty.stars;

        const auto key = Canonical(entry.hash);
        auto found = unique.find(key);
        if (found == unique.end() || entry.priority > found->second.priority) {
            unique[key] = std::move(entry);
        }
    }

    std::vector<MapEntry> result;
    result.reserve(unique.size());
    for (auto& [_, entry] : unique) result.push_back(std::move(entry));
    std::sort(result.begin(), result.end(), [](const MapEntry& left, const MapEntry& right) {
        if (left.priority != right.priority) return left.priority > right.priority;
        if (left.songName != right.songName) return left.songName < right.songName;
        return left.hash < right.hash;
    });
    if (limit >= 0 && result.size() > static_cast<size_t>(limit)) result.resize(static_cast<size_t>(limit));
    return result;
}

std::string PlaylistFileName(const std::string& service, ListKind kind) {
    const auto provider = Canonical(service) == "scoresaber" ? "ScoreSaber" : "BeatLeader";
    return std::string("RankedPractice_") + provider +
           (kind == ListKind::Combined ? "_PPGain.bplist" :
            kind == ListKind::NotPlayed ? "_NotPlayed.bplist" : "_ToImprove.bplist");
}

std::string PlaylistTitle(const std::string& service, ListKind kind) {
    const auto provider = Canonical(service) == "scoresaber" ? "ScoreSaber" : "BeatLeader";
    return std::string(provider) + (kind == ListKind::Combined ? " - PP Gain" :
                                  kind == ListKind::NotPlayed ? " - Not Played" : " - To Improve");
}

std::string ClanPlaylistFileName(const std::string& clanTag, bool perClan) {
    const std::string base = "RankedPractice_BeatLeader_Clan_ToConquer";
    if (!perClan) return base + ".bplist";

    std::string safeTag;
    safeTag.reserve(clanTag.size());
    for (unsigned char character : clanTag) {
        safeTag.push_back(std::isalnum(character) || character == '-' || character == '_'
                              ? static_cast<char>(character)
                              : '_');
    }
    if (safeTag.empty()) safeTag = "clan";
    return base + "_" + safeTag + ".bplist";
}

std::string ClanPlaylistTitle(const std::string& clanScope) {
    return "BeatLeader - Maps to Conquer (" + clanScope + ")";
}

} // namespace rankedpractice
