#pragma once

#include "recommendation.hpp"
#include <cstdint>

namespace rankedpractice {

struct AbilityPoint {
    double stars{}, accuracy{}, weight{};
};

struct AbilityChart {
    std::vector<AbilityPoint> scores;
    std::vector<AbilityPoint> curve;
    double minObservedStars{}, maxObservedStars{}, maxStars{4.0}, minAccuracy{0.5};
};

// Exports the recommendation model itself, including its training filters and tails.
// Empty histories produce no curve: the default 85% fallback is not a personal fit.
AbilityChart BuildAbilityChart(const std::vector<MapEntry>& scores);

struct ChartBitmap {
    int width{1024}, height{576};
    // RGBA, bottom row first, matching Unity's texture upload order.
    std::vector<uint8_t> pixels;
};
ChartBitmap RenderAbilityChart(const AbilityChart& chart);

} // namespace rankedpractice
