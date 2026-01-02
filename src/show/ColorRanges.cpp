#include "ColorRanges.h"
#include "../color.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace Show {
    ColorRanges::ColorRanges(const std::vector<Strip::Color> &colors,
                             const std::vector<float> &ranges,
                             bool gradient)
        : colors(colors), ranges(ranges), gradient(gradient) {
    }

    void ColorRanges::execute(Strip::Strip &strip, Iteration iteration) {
        // Initialize color ranges on first run
        if (!initialized) {
#ifdef ARDUINO
            Serial.print("ColorRanges: colors=[");
            for (auto c: colors) {
                Serial.printf("RGB(%d,%d,%d), ", red(c), green(c), blue(c));
            }
            Serial.print("], ranges=[");
            for (auto range: ranges) {
                Serial.printf("%.1f%%, ", range);
            }
            Serial.printf("], gradient=%s\n", gradient ? "true" : "false");
#endif

            uint16_t num_leds = strip.length();
            std::vector<Strip::Color> target_colors;
            target_colors.reserve(num_leds);

            // Build boundary indices
            std::vector<uint16_t> boundaries;
            boundaries.push_back(0); // Always start at 0

            // Validate ranges if provided
            bool use_custom_ranges = !ranges.empty();
            if (use_custom_ranges && ranges.size() != colors.size() - 1) {
#ifdef ARDUINO
                Serial.printf(
                    "ColorRanges WARNING: Expected %zu ranges for %zu colors, got %zu. Using equal distribution.\n",
                    colors.size() - 1, colors.size(), ranges.size());
#endif
                use_custom_ranges = false;
            }


            if (!use_custom_ranges) {
                // Equal distribution
#ifdef ARDUINO
                Serial.printf("ColorRanges: Using equal distribution for %zu colors\n", colors.size());
#endif
                for (size_t i = 1; i < colors.size(); i++) {
                    uint16_t boundary = (uint16_t)((float) num_leds * i / colors.size());
                    boundaries.push_back(boundary);
                }
            } else {
                // Custom ranges (percentages)
#ifdef ARDUINO
                Serial.printf("ColorRanges: Using custom ranges for %zu colors: ", colors.size());
                for (size_t i = 0; i < ranges.size(); i++) {
                    Serial.printf("%.1f%% ", ranges[i]);
                }
                Serial.println();
#endif
                for (float range: ranges) {
                    uint16_t boundary = (uint16_t)((float) num_leds * range / 100.0f);
                    boundaries.push_back(boundary);
                }
            }
            boundaries.push_back(num_leds); // Always end at num_leds

            if (gradient && colors.size() > 1) {
                // Gradient mode: colors are waypoints, interpolate between them
                // Waypoints are evenly distributed: 0%, 1/(N-1), 2/(N-1), ..., 100%
                for (uint16_t led = 0; led < num_leds; led++) {
                    float position = (num_leds > 1)
                        ? (float)led / (float)(num_leds - 1)
                        : 0.0f;

                    // Find which segment this LED is in
                    float segment_size = 1.0f / (float)(colors.size() - 1);
                    size_t segment = (size_t)(position / segment_size);
                    if (segment >= colors.size() - 1) segment = colors.size() - 2;

                    // Calculate position within segment (0.0 to 1.0)
                    float segment_start = (float)segment * segment_size;
                    float ratio = (position - segment_start) / segment_size;
                    if (ratio > 1.0f) ratio = 1.0f;

                    // Interpolate between adjacent waypoint colors
                    Strip::Color colorA = colors[segment];
                    Strip::Color colorB = colors[segment + 1];

                    uint8_t r = (uint8_t)(red(colorA) * (1.0f - ratio) + red(colorB) * ratio);
                    uint8_t g = (uint8_t)(green(colorA) * (1.0f - ratio) + green(colorB) * ratio);
                    uint8_t b = (uint8_t)(blue(colorA) * (1.0f - ratio) + blue(colorB) * ratio);

                    target_colors.push_back(color(r, g, b));
                }
            } else {
                // Solid mode: colors fill sections with sharp boundaries
                for (uint16_t led = 0; led < num_leds; led++) {
                    // Find which color range this LED belongs to
                    size_t color_index = 0;
                    for (size_t i = 0; i < boundaries.size() - 1; i++) {
                        if (led >= boundaries[i] && led < boundaries[i + 1]) {
                            color_index = i;
                            break;
                        }
                    }

                    // Make sure we don't go out of bounds
                    if (color_index >= colors.size()) {
                        color_index = colors.size() - 1;
                    }

                    target_colors.push_back(colors[color_index]);
                }
            }

            // Create SmoothBlend with the color range pattern
            blend = std::make_unique<Support::SmoothBlend>(strip, target_colors);
            initialized = true;
        }

        // Step through the smooth blend transition
        if (blend != nullptr && !blend->isComplete()) {
            blend->step();
        }
    }

    bool ColorRanges::isComplete() const {
        return initialized && (blend == nullptr || blend->isComplete());
    }
} // namespace Show