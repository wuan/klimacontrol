#include "Wave.h"
#include "../color.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Show {
    Wave::Wave(float wave_speed, float decay_rate, float brightness_frequency, float wavelength)
        : wave_speed(wave_speed), decay_rate(decay_rate),
          brightness_frequency(brightness_frequency), wavelength(wavelength),
          time(0.0f), color_time(0.0f) {
    }

    void Wave::execute(Strip::Strip &strip, Iteration iteration) {
        // Increment time counters
        time += 0.05f;
        color_time += 0.05f;

        uint16_t num_leds = strip.length();

        // Calculate source brightness using sine wave (oscillates between 0.3 and 1.0)
        float source_brightness = 0.65f + 0.35f * sinf(time * brightness_frequency * 2.0f * M_PI);

        // Clear strip
        for (uint16_t i = 0; i < num_leds; i++) {
            // Create wave pattern: sine wave propagates outward from center
            float wave_position = (float) (i - (time * wave_speed * 10.0f)) / wavelength;
            float wave_brightness = (sinf(wave_position) + 1.0f) / 2.0f; // Normalize to 0-1

            // Calculate when this wave element was at the center (emission time)
            // This determines what color it should have
            float emission_time = color_time - ((float) i / (wave_speed * 10.0f));
            uint8_t color_index = (uint8_t)((int) (emission_time * 20.0f) % 255);
            Strip::Color pixel_color = wheel(color_index);

            // Apply distance-based decay (exponential decay towards the ends)
            float distance_factor = expf(-decay_rate * (float) i / (float) num_leds);

            // Combine source brightness, wave pattern, and distance decay
            float final_brightness = source_brightness * wave_brightness * distance_factor;

            // Apply brightness to color
            uint8_t r = (uint8_t)(red(pixel_color) * final_brightness);
            uint8_t g = (uint8_t)(green(pixel_color) * final_brightness);
            uint8_t b = (uint8_t)(blue(pixel_color) * final_brightness);

            strip.setPixelColor(i, color(r, g, b));
        }
    }
} // namespace Show