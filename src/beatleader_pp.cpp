#include "beatleader_pp.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace rankedpractice {
namespace {
// PPv4 reference: BeatLeader/beatleader-website, commit
// 606b7f9d33fec7dcf2427cea1d1151b74a5ec006, src/utils/beatleader/rebalanced-pp.js.
// Keep the sampled curve (including six-decimal knot rounding) consistent with
// upstream: evaluating the analytic curve directly changes interpolated PP.
constexpr std::pair<double, double> v4Base[] = {
    {0, 0},         {.6, .307},     {.65, .355},    {.7, .414},    {.75, .485},    {.8, .568},     {.825, .605},
    {.85, .649},    {.875, .699},   {.9, .753},     {.91, .781},   {.92, .813},    {.93, .867},    {.94, .931},
    {.95, 1},       {.955, 1.039},  {.96, 1.094},   {.965, 1.167}, {.97, 1.256},   {.9725, 1.315}, {.975, 1.392},
    {.9775, 1.490}, {.98, 1.618},   {.9825, 1.786}, {.985, 2.007}, {.9875, 2.303}, {.99, 2.7},     {.9925, 3.241},
    {.995, 4.01},   {.9975, 5.158}, {.999, 6.241},  {1, 7.424}};

double V4BaseValue(double x, bool smooth) {
    constexpr size_t n = std::size(v4Base);
    static const auto slopes = [] {
        std::array<double, n> result{};
        std::array<double, n - 1> h{}, d{};
        for (size_t i = 0; i + 1 < n; ++i) {
            h[i] = v4Base[i + 1].first - v4Base[i].first;
            d[i] = (v4Base[i + 1].second - v4Base[i].second) / h[i];
        }
        result.front() = d.front();
        result.back() = d.back();
        for (size_t i = 1; i + 1 < n; ++i) {
            if (d[i - 1] * d[i] <= 0)
                continue;
            const double w1 = 2 * h[i] + h[i - 1], w2 = h[i] + 2 * h[i - 1];
            result[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i]);
        }
        return result;
    }();
    if (x <= 0)
        return 0;
    if (x >= 1)
        return v4Base[n - 1].second;
    size_t i = 0;
    while (v4Base[i + 1].first < x)
        ++i;
    const auto [left, y0] = v4Base[i];
    const auto [right, y1] = v4Base[i + 1];
    const double h = right - left, t = (x - left) / h;
    if (!smooth)
        return y0 + t * (y1 - y0);
    const double t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * y0 + (t3 - 2 * t2 + t) * h * slopes[i] + (-2 * t3 + 3 * t2) * y1 +
           (t3 - t2) * h * slopes[i + 1];
}

struct V4Parameters {
    double predictedAcc, a, c, k, w, k2, p2;
};
constexpr V4Parameters v4Parameters[] = {
    {.955, 1, -5.3, 4.86, .6, 2.88, 2.46},    {.960, .99, -3, 4.3, .66, 4.8, 1.85},
    {.965, .988, -2.4, 4.5, .64, 4.5, 1.95},  {.970, .986, -2.2, 4.7, .62, 4.2, 2.05},
    {.975, .985, -1.8, 4.86, .6, 3.8, 2.15},  {.980, .985, -1.4, 4.86, .6, 3.35, 2.3},
    {.985, .985, -1.2, 4.86, .6, 2.88, 2.46}, {.990, .9915, -1.2, 4.86, .6, 2.88, 2.46},
    {.995, .9915, -1.2, 4.86, .6, 2.88, 2.46}};
V4Parameters V4ParamsAt(double predictedAcc) {
    if (predictedAcc <= v4Parameters[0].predictedAcc)
        return v4Parameters[0];
    for (size_t i = 1; i < std::size(v4Parameters); ++i) {
        const auto &p = v4Parameters[i - 1];
        const auto &q = v4Parameters[i];
        if (predictedAcc > q.predictedAcc)
            continue;
        const double t = (predictedAcc - p.predictedAcc) / (q.predictedAcc - p.predictedAcc);
        auto mix = [t](double x, double y) { return x + t * (y - x); };
        return {predictedAcc,  mix(p.a, q.a),   mix(p.c, q.c),  mix(p.k, q.k),
                mix(p.w, q.w), mix(p.k2, q.k2), mix(p.p2, q.p2)};
    }
    return v4Parameters[std::size(v4Parameters) - 1];
}

double V4ModifiableValue(double x, const V4Parameters &p) {
    constexpr double y0 = .256;
    const double m = (1 - y0) / .35 * (1 - p.w + p.w * p.k * std::exp(p.k) / (std::exp(p.k) - 1));
    auto f2 = [&](double v) {
        return 1 + m * (v - .95) +
               (6.424 - .05 * m) * (std::exp(p.k2 * std::pow((v - .95) / .05, p.p2)) - 1) / (std::exp(p.k2) - 1);
    };
    if (x <= .6)
        return y0 / .6 * x;
    if (x <= .95) {
        const double u = (x - .6) / .35;
        return y0 + (1 - y0) * ((1 - p.w) * u + p.w * (std::exp(p.k * u) - 1) / (std::exp(p.k) - 1));
    }
    if (x <= p.a)
        return f2(x);
    const double u = (p.a - .95) / .05;
    const double s = m + (6.424 - .05 * m) * std::exp(p.k2 * std::pow(u, p.p2)) / (std::exp(p.k2) - 1) * p.k2 * p.p2 /
                             .05 * std::pow(u, p.p2 - 1);
    const double s1 = s * (1 - p.c);
    if (std::abs(p.a - 1) < 1e-9)
        return f2(p.a) + s1 * (x - p.a);
    return f2(p.a) + s1 * (x - p.a) + (s - s1) * (1 - p.a) * (1 - std::exp(-(x - p.a) / (1 - p.a)));
}

double V4Nerf(double value, double start, double reference, double minimum) {
    if (value <= 0 || value >= start)
        return 1;
    return std::clamp(1 - (1 - minimum) * std::log(start / value) / std::log(start / reference), minimum, 1.0);
}

class V4Curve {
  public:
    explicit V4Curve(const Difficulty &d) : prediction_(d.predictedAcc), params_(V4ParamsAt(prediction_)) {
        const bool active = prediction_ > .6 && prediction_ < .9995;
        modified_ = active && !(params_.a >= .9995 && params_.k == 4.86 && params_.w == .6 && params_.k2 == 2.88 &&
                                params_.p2 == 2.46);
        tilt_ = active ? .2 * d.sliderPercentage : 0;
        nerf_ = V4Nerf(d.notes, 115, 20, .95) * V4Nerf(d.noteSpan.value_or(0), 75, 12, .9);
    }

    double Value(double x) const {
        if (!modified_ && tilt_ == 0)
            return V4BaseValue(x, false) * nerf_;
        // Only evaluate the two knots surrounding the requested accuracy. This
        // is identical to building upstream's full curve, without a per-map cache.
        double left = 0, right = 1;
        auto consider = [&](double knot) {
            if (knot <= x)
                left = std::max(left, knot);
            if (knot >= x)
                right = std::min(right, knot);
        };
        auto round6 = [](double v) { return std::round(v * 1e6) / 1e6; };
        for (const auto &point : v4Base)
            consider(round6(point.first));
        for (int i = 0; i < 71; ++i)
            consider(round6(.6 + i * .005));
        for (int i = 0; i < 101; ++i)
            consider(round6(.95 + i * .0005));
        consider(round6(modified_ ? params_.a : 1));
        const double lo = Raw(left);
        return (left == right ? lo : lo + (x - left) / (right - left) * (Raw(right) - lo)) * nerf_;
    }

  private:
    double Raw(double x) const {
        double value = V4BaseValue(x, modified_);
        if (modified_) {
            const V4Parameters standard{0, 1, 1, 4.86, .6, 2.88, 2.46};
            const double reference = V4ModifiableValue(x, standard);
            if (reference > 0)
                value = std::min(value, value * V4ModifiableValue(x, params_) / reference);
        }
        return value * std::exp(tilt_ * std::clamp(x - prediction_, -.015, .015) * 100);
    }
    double prediction_, tilt_{0}, nerf_{1};
    V4Parameters params_;
    bool modified_{false};
};

double V4LowStarMultiplier(double stars) {
    return stars >= 8.6454 ? 1 : 1 + .0063371 * std::pow(8.6454 - std::max(stars, 0.0), 1.36);
}
double V4Stars(double ppAt96) {
    auto fromPp = [](double pp) { return pp > 0 ? 1.1653 * std::pow(pp / 83.6925, 1.17507) : pp / 83.6925; };
    const double stars = fromPp(ppAt96);
    if (ppAt96 <= 0 || stars >= 8.6454)
        return stars;
    double lo = stars, hi = 8.6454;
    while (hi - lo > 1e-6) {
        const double mid = (lo + hi) / 2;
        if (mid < fromPp(ppAt96 * V4LowStarMultiplier(mid)))
            lo = mid;
        else
            hi = mid;
    }
    return (lo + hi) / 2;
}
double V4CurvePower(double stars) {
    if (stars < 4.803) {
        if (stars <= 2.8818)
            return 1.08;
        return 1.08 + (1 - 1.08) * (stars - 2.8818) / (4.803 - 2.8818);
    }
    if (stars >= 9.606)
        return .9;
    if (stars <= 6.7242)
        return 1;
    return 1 + (.9 - 1) * (stars - 6.7242) / (9.606 - 6.7242);
}
} // namespace

double BeatLeaderPpv4(double accuracy, const Difficulty &d) {
    if (!std::isfinite(accuracy) || accuracy <= 0 || accuracy > 1 || !std::isfinite(d.accRating) ||
        !std::isfinite(d.passRating) || !std::isfinite(d.techRating) || !std::isfinite(d.predictedAcc) ||
        !std::isfinite(d.sliderPercentage) || !std::isfinite(d.notes) || !d.noteSpan || !std::isfinite(*d.noteSpan) ||
        *d.noteSpan < 0 || d.accRating < 0 || d.passRating < 0 || d.techRating < 0)
        return 0;
    const V4Curve curve(d);
    const double pass = d.passRating > 13 ? 13 + (d.passRating - 13) * .5 : d.passRating;
    const double passPp = std::max(0.0, 12.3 * std::exp(.85966 * std::pow(pass, 1 / 2.62)) - 30);
    const double tech = d.techRating > 12 ? 12 + (d.techRating - 12) * .33 : d.techRating;
    const double techFactor = tech > 0 ? .1612 * std::pow(tech, 1.2477) : 0;
    const double accFactor = d.accRating > 0 ? 21.584 * std::pow(d.accRating, 1.2163) : 0;
    const double at96 = curve.Value(.96);
    const double stars = V4Stars(passPp + at96 * accFactor + std::exp(3.5 * .96) * techFactor);
    double value = curve.Value(accuracy);
    if (value > 0 && at96 > 0)
        value = at96 * std::pow(value / at96, V4CurvePower(stars));
    const double total =
        1.023 * (passPp + value * accFactor + std::exp(3.5 * accuracy) * techFactor) * V4LowStarMultiplier(stars);
    return std::isfinite(total) ? total : 0;
}
} // namespace rankedpractice
