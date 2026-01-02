#include "TheaterChase.h"
#include "../color.h"

namespace Show {
    TheaterChase::TheaterChase(unsigned int num_steps_per_cycle)
        : num_steps_per_cycle(num_steps_per_cycle) {
    }

    void TheaterChase::execute(Strip::Strip &strip, Iteration iteration) {
        uint16_t num_leds = strip.length();

        // Calculate color progression through the wheel
        float cycle_position = (float) (index % num_steps_per_cycle) / (float) num_steps_per_cycle;
        uint8_t color_index = (uint8_t)(cycle_position * 255.0f);
        Strip::Color chase_color = wheel(color_index);

        // Apply theater chase pattern
        // Pattern: 2 LEDs dark, 5 LEDs lit in each 7-LED segment
        // The pattern shifts by one position each frame
        for (uint16_t i = 0; i < num_leds; i++) {
            // Calculate segment offset
            unsigned int offset = (i + index) % 7;

            // Set pixel: dark for first 2 positions in each 7-LED segment, colored otherwise
            if (offset < 2) {
                strip.setPixelColor(i, color(0, 0, 0));
            } else {
                strip.setPixelColor(i, chase_color);
            }
        }

        // Increment index for next frame
        index++;
    }
} // namespace Show