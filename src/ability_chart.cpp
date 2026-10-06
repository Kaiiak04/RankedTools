#include "ability_chart.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace rankedpractice {
namespace {
using Color = std::array<uint8_t, 4>;
constexpr Color background{12, 20, 36, 255}, grid{40, 57, 78, 255};
constexpr Color labels{211, 226, 240, 255}, lineColor{0, 211, 255, 255};

// Small bitmap glyphs keep texture generation independent of Unity/font assets.
std::array<uint8_t, 7> Glyph(char c) {
    switch (c) {
        case '0': return {14,17,19,21,25,17,14}; case '1': return {4,12,4,4,4,4,14};
        case '2': return {14,17,1,2,4,8,31}; case '3': return {30,1,1,14,1,1,30};
        case '4': return {2,6,10,18,31,2,2}; case '5': return {31,16,16,30,1,1,30};
        case '6': return {14,16,16,30,17,17,14}; case '7': return {31,1,2,4,8,8,8};
        case '8': return {14,17,17,14,17,17,14}; case '9': return {14,17,17,15,1,1,14};
        case '%': return {17,2,4,8,16,17,0}; case '.': return {0,0,0,0,0,12,12};
        case 'A': return {14,17,17,31,17,17,17}; case 'C': return {14,17,16,16,16,17,14};
        case 'E': return {31,16,16,30,16,16,31}; case 'F': return {31,16,16,30,16,16,16};
        case 'I': return {14,4,4,4,4,4,14}; case 'R': return {30,17,17,30,20,18,17};
        case 'S': return {15,16,16,14,1,1,30}; case 'T': return {31,4,4,4,4,4,4};
        case 'U': return {17,17,17,17,17,17,14}; case 'Y': return {17,17,10,4,4,4,4};
        default: return {};
    }
}

class Canvas {
public:
    ChartBitmap bitmap;
    Canvas() : bitmap{} {
        bitmap.pixels.resize(bitmap.width * bitmap.height * 4);
        for (int y = 0; y < bitmap.height; ++y)
            for (int x = 0; x < bitmap.width; ++x) Pixel(x, y, background);
    }
    void Pixel(int x, int y, Color color) {
        if (x < 0 || y < 0 || x >= bitmap.width || y >= bitmap.height) return;
        const auto i = (y * bitmap.width + x) * 4;
        for (int channel = 0; channel < 4; ++channel) bitmap.pixels[i + channel] = color[channel];
    }
    void Dot(int x, int y, int radius, Color color) {
        for (int dy = -radius; dy <= radius; ++dy)
            for (int dx = -radius; dx <= radius; ++dx)
                if (dx * dx + dy * dy <= radius * radius) Pixel(x + dx, y + dy, color);
    }
    void Line(int x0, int y0, int x1, int y1, Color color, int radius = 0, bool dashed = false) {
        const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
        for (int i = 0; i <= steps; ++i) {
            const double t = steps ? static_cast<double>(i) / steps : 0.0;
            const int x = std::lround(x0 + (x1 - x0) * t);
            if (!dashed || (x / 12) % 2 == 0)
                Dot(x, std::lround(y0 + (y1 - y0) * t), radius, color);
        }
    }
    void Text(int x, int y, const std::string& value, int scale = 3) {
        for (char c : value) {
            const auto glyph = Glyph(c);
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (glyph[row] & (1 << (4 - col)))
                        for (int dy = 0; dy < scale; ++dy)
                            for (int dx = 0; dx < scale; ++dx)
                                Pixel(x + col * scale + dx, y + (6 - row) * scale + dy, labels);
            x += 6 * scale;
        }
    }
};
}

ChartBitmap RenderAbilityChart(const AbilityChart& chart) {
    Canvas canvas;
    constexpr int left = 100, right = 976, bottom = 80, top = 518;
    auto x = [&](double stars) { return left + std::lround((right - left) * stars / chart.maxStars); };
    auto y = [&](double accuracy) { return bottom + std::lround((top - bottom) * (accuracy - chart.minAccuracy) / (1.0 - chart.minAccuracy)); };
    if (!chart.scores.empty()) {
        // Shade areas without observed scores; the dashed line there is extrapolation.
        for (int px = left; px <= right; ++px) {
            const double stars = chart.maxStars * (px - left) / (right - left);
            if (stars < chart.minObservedStars || stars > chart.maxObservedStars)
                for (int py = bottom; py <= top; ++py) canvas.Pixel(px, py, {22, 31, 49, 255});
        }
    }
    for (int percent = std::lround(chart.minAccuracy * 100); percent <= 100; percent += 5) {
        const int py = y(percent / 100.0);
        canvas.Line(left, py, right, py, grid);
        const auto label = std::to_string(percent) + "%";
        canvas.Text(left - 14 - label.size() * 18, py - 9, label);
    }
    const int starStep = std::max(1, static_cast<int>(std::ceil(chart.maxStars / 12.0)));
    for (int stars = 0; stars <= chart.maxStars; stars += starStep) {
        const int px = x(stars);
        canvas.Line(px, bottom, px, top, grid);
        const auto label = std::to_string(stars);
        canvas.Text(px - label.size() * 9, bottom - 32, label);
    }
    canvas.Text(left, top + 25, "ACCURACY", 3);
    canvas.Text((left + right) / 2 - 45, 15, "STARS", 3);
    for (const auto& point : chart.scores) {
        // Fade older scores to communicate their smaller fitting weight.
        const uint8_t shade = static_cast<uint8_t>(100 + 100 * std::clamp(point.weight, 0.0, 1.0));
        canvas.Dot(x(point.stars), y(point.accuracy), 4, {shade, shade, shade, 255});
    }
    for (size_t i = 1; i < chart.curve.size(); ++i) {
        const auto& a = chart.curve[i - 1]; const auto& b = chart.curve[i];
        const double midpoint = (a.stars + b.stars) / 2.0;
        canvas.Line(x(a.stars), y(a.accuracy), x(b.stars), y(b.accuracy), lineColor, 2,
                    midpoint < chart.minObservedStars || midpoint > chart.maxObservedStars);
    }
    return std::move(canvas.bitmap);
}
} // namespace rankedpractice
