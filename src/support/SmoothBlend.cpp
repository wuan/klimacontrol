#include "SmoothBlend.h"
#include "../color.h"
#include "../Timer.h"
#include <algorithm>
#include <cmath>

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace Support {
    namespace {
        /**
         * Blend a single color component with power function
         * @param start_component Starting component value
         * @param end_component Target component value
         * @param fade_progress Progress from start (1.0) to end (0.0)
         * @param power Power for the blend curve (1.0 = linear)
         * @return Blended component value
         */
        Strip::ColorComponent blend_component(
            Strip::ColorComponent start_component,
            Strip::ColorComponent end_component,
            float fade_progress,
            float power = 1.0f
        ) {
            float start_value = start_component * std::pow(fade_progress, power);
            float end_value = end_component * std::pow(1.0f - fade_progress, power);
            return static_cast<Strip::ColorComponent>(start_value + end_value);
        }

        /**
         * Blend two colors linearly
         * @param start_color Starting color
         * @param end_color Target color
         * @param fade_progress Progress from start (1.0) to end (0.0)
         * @return Blended color
         */
        Strip::Color linear_blend(Strip::Color start_color, Strip::Color end_color, float fade_progress) {
            Strip::ColorComponent r = blend_component(red(start_color), red(end_color), fade_progress);
            Strip::ColorComponent g = blend_component(green(start_color), green(end_color), fade_progress);
            Strip::ColorComponent b = blend_component(blue(start_color), blue(end_color), fade_progress);
            return color(r, g, b);
        }
    }

    SmoothBlend::SmoothBlend(Strip::Strip &strip, const std::vector<Strip::Color> &target_colors,
                             unsigned long duration_ms)
        : strip(strip), target_colors(target_colors), duration_ms(duration_ms) {
        // Capture initial colors from the strip
        initial_colors.reserve(strip.length());
        for (Strip::PixelIndex i = 0; i < strip.length(); i++) {
            initial_colors.push_back(strip.getPixelColor(i));
        }

        // Record start time
#ifdef ARDUINO
        start_time = millis();
#else
        start_time = ::millis();
#endif
    }

    SmoothBlend::SmoothBlend(Strip::Strip &strip, Strip::Color target_color, unsigned long duration_ms)
        : strip(strip), duration_ms(duration_ms) {
        // Fill target_colors with the same color for all LEDs
        target_colors.resize(strip.length(), target_color);

        // Capture initial colors from the strip
        initial_colors.reserve(strip.length());
        for (Strip::PixelIndex i = 0; i < strip.length(); i++) {
            initial_colors.push_back(strip.getPixelColor(i));
        }

        // Record start time
#ifdef ARDUINO
        start_time = millis();
#else
        start_time = ::millis();
#endif
    }

    bool SmoothBlend::step() {
        // Get current time
#ifdef ARDUINO
        unsigned long now = millis();
#else
        unsigned long now = ::millis();
#endif

        // Calculate fade progress (1.0 at start, 0.0 at end)
        float elapsed = static_cast<float>(now - start_time);
        float fade_progress = 1.0f - std::min(elapsed / static_cast<float>(duration_ms), 1.0f);

        // Update each LED
        for (Strip::PixelIndex i = 0; i < strip.length() && i < static_cast<Strip::PixelIndex>(target_colors.size()); i
             ++) {
            Strip::Color blended = linear_blend(initial_colors[i], target_colors[i], fade_progress);
            strip.setPixelColor(i, blended);
        }

        strip.show();

        return !isComplete();
    }

    bool SmoothBlend::isComplete() const {
#ifdef ARDUINO
        unsigned long now = millis();
#else
        unsigned long now = ::millis();
#endif
        return (now - start_time) >= duration_ms;
    }
} // namespace Support