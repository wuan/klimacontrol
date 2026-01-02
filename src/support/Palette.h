#ifndef LEDZ_SUPPORT_PALETTE_H
#define LEDZ_SUPPORT_PALETTE_H

#include <vector>
#include <algorithm>
#include "../strip/Strip.h"

namespace Support {

    enum class InterpolationType {
        Linear,
        Step,
        Power
    };

    struct ColorPoint {
        float position;
        Strip::Color color;
        InterpolationType interpolation = InterpolationType::Linear;
        float power = 1.0f;

        ColorPoint(float pos, Strip::Color col, InterpolationType interp = InterpolationType::Linear, float pwr = 1.0f)
            : position(pos), color(col), interpolation(interp), power(pwr) {}

        bool operator<(const ColorPoint& other) const {
            return position < other.position;
        }
    };

    class Palette {
    public:
        Palette() = default;
        explicit Palette(std::vector<ColorPoint> points);

        void addPoint(const ColorPoint& point);
        void addPoint(float position, Strip::Color color, InterpolationType interpolation = InterpolationType::Linear, float power = 1.0f);

        [[nodiscard]] Strip::Color get_color(float position) const;

    private:
        std::vector<ColorPoint> points;
        void sortPoints();
    };

} // namespace Support

#endif //LEDZ_SUPPORT_PALETTE_H
