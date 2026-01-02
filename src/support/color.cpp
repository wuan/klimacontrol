#include "color.h"
#include <cmath>
#include <algorithm>

namespace Support::Color {

    Strip::Color black_body_color(float temperature) {
        // Clamp temperature to valid range
        temperature = std::max(0.0f, std::min(1.0f, temperature));

        // Map normalized temperature (0-1) to Kelvin (800-1800K)
        // 0.0 → 800K (barely visible dull red)
        // 1.0 → 1800K (bright yellow)
        float kelvin = 800.0f + (temperature * 1000.0f);

        // Tanner Helland's algorithm
        // Temperature in Kelvin, divide by 100
        float temp = kelvin / 100.0f;

        // Red (always 255 for temps below 6600K - all wood fire temps)
        uint8_t red = 255;

        // Green: 99.4708025861 * ln(temp) - 161.1195681661
        float green_f = 99.4708025861f * std::log(temp) - 161.1195681661f;
        uint8_t green = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, green_f)));

        // Blue: 138.5177312231 * ln(temp - 10) - 305.0447927307
        // (0 for temp <= 19, i.e., below 1900K)
        float blue_f = (temp <= 19.0f) ? 0.0f : 138.5177312231f * std::log(temp - 10.0f) - 305.0447927307f;
        uint8_t blue = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, blue_f)));

        // Apply brightness scaling for low temperatures
        // At temp=0.0, brightness=0 (black)
        // At temp=0.3+, brightness=1.0 (full color)
        float brightness = std::min(1.0f, temperature / 0.3f);
        red = static_cast<uint8_t>(red * brightness);
        green = static_cast<uint8_t>(green * brightness);
        blue = static_cast<uint8_t>(blue * brightness);

        // Combine into a single color value
        return (static_cast<Strip::Color>(red) << 16) |
               (static_cast<Strip::Color>(green) << 8) |
               static_cast<Strip::Color>(blue);
    }
}
