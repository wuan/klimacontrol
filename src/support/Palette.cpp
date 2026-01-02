#include "Palette.h"
#include "../color.h"
#include <cmath>

namespace Support {

    namespace {
        Strip::Color linear_blend(Strip::Color start_color, Strip::Color end_color, float progress) {
            auto r = static_cast<Strip::ColorComponent>(red(start_color) * (1.0f - progress) + red(end_color) * progress);
            auto g = static_cast<Strip::ColorComponent>(green(start_color) * (1.0f - progress) + green(end_color) * progress);
            auto b = static_cast<Strip::ColorComponent>(blue(start_color) * (1.0f - progress) + blue(end_color) * progress);
            return color(r, g, b);
        }
    }

    Palette::Palette(std::vector<ColorPoint> points) : points(std::move(points)) {
        sortPoints();
    }

    void Palette::addPoint(const ColorPoint& point) {
        points.push_back(point);
        sortPoints();
    }

    void Palette::addPoint(float position, Strip::Color color, InterpolationType interpolation, float power) {
        points.emplace_back(position, color, interpolation, power);
        sortPoints();
    }

    void Palette::sortPoints() {
        std::sort(points.begin(), points.end());
    }

    Strip::Color Palette::get_color(float position) const {
        if (points.empty()) {
            return 0;
        }

        if (position <= points.front().position) {
            return points.front().color;
        }

        if (position >= points.back().position) {
            return points.back().color;
        }

        for (size_t i = 0; i < points.size() - 1; ++i) {
            if (position >= points[i].position && position < points[i + 1].position) {
                const auto& p1 = points[i];
                const auto& p2 = points[i + 1];

                if (p1.interpolation == InterpolationType::Step) {
                    return p1.color;
                }

                float range = p2.position - p1.position;
                float progress = (position - p1.position) / range;

                if (p1.interpolation == InterpolationType::Power) {
                    progress = std::pow(progress, p1.power);
                }

                return linear_blend(p1.color, p2.color, progress);
            }
        }

        return points.back().color;
    }

} // namespace Support
