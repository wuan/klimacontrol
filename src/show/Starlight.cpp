#include "Starlight.h"
#include "../color.h"

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <cstdlib>  // For rand(), RAND_MAX
#endif

namespace Show {
    Starlight::Starlight(float probability, unsigned long length_ms, unsigned long fade_ms,
                         uint8_t r, uint8_t g, uint8_t b)
        : probability(probability), length_ms(length_ms), fade_ms(fade_ms),
          star_color(color(r, g, b)) {
    }

    float Starlight::calculateBrightness(unsigned long elapsed_ms) {
        // Phase 1: Fade-in (0 to fade_ms)
        if (elapsed_ms < fade_ms) {
            return (float) elapsed_ms / (float) fade_ms;
        }

        // Phase 2: Hold at full brightness (fade_ms to fade_ms + length_ms)
        unsigned long hold_end = fade_ms + length_ms;
        if (elapsed_ms < hold_end) {
            return 1.0f;
        }

        // Phase 3: Fade-out (hold_end to hold_end + fade_ms)
        unsigned long fade_out_start = hold_end;
        unsigned long fade_out_end = fade_out_start + fade_ms;
        if (elapsed_ms < fade_out_end) {
            unsigned long fade_out_elapsed = elapsed_ms - fade_out_start;
            return 1.0f - ((float) fade_out_elapsed / (float) fade_ms);
        }

        // Star has completed its lifecycle
        return 0.0f;
    }

    void Starlight::execute(Strip::Strip &strip, Iteration iteration) {
#ifdef ARDUINO
        unsigned long current_time = millis();
#else
        // For native builds, use iteration count as time proxy (10ms per iteration)
        unsigned long current_time = iteration * 10;
#endif
        uint16_t num_leds = strip.length();

        // Spawn new stars based on probability
        // Use a random float between 0.0 and 1.0
#ifdef ARDUINO
        float spawn_chance = (float) random(1000) / 1000.0f;
#else
        float spawn_chance = (float) rand() / (float) RAND_MAX;
#endif
        if (spawn_chance < probability) {
            // Pick a random LED that's not already an active star
#ifdef ARDUINO
            uint16_t led = random(num_leds);
#else
            uint16_t led = rand() % num_leds;
#endif
            if (active_stars.find(led) == active_stars.end()) {
                active_stars[led] = current_time;
#ifdef ARDUINO
                // Debug: Only log occasionally to avoid spam
                if (active_stars.size() <= 5) {
                    Serial.printf("Starlight: New star at LED %u (total active: %zu)\n",
                                  led, active_stars.size());
                }
#endif
            }
        }

        // Clear the strip
        for (uint16_t i = 0; i < num_leds; i++) {
            strip.setPixelColor(i, color(0, 0, 0));
        }

        // Update and render all active stars
        auto it = active_stars.begin();
        while (it != active_stars.end()) {
            uint16_t led = it->first;
            unsigned long start_time = it->second;
            unsigned long elapsed = current_time - start_time;

            // Check if star has expired
            if (elapsed >= getTotalLifetime()) {
                it = active_stars.erase(it);
                continue;
            }

            // Calculate brightness and apply to LED
            float brightness = calculateBrightness(elapsed);
            uint8_t r = (uint8_t)(red(star_color) * brightness);
            uint8_t g = (uint8_t)(green(star_color) * brightness);
            uint8_t b = (uint8_t)(blue(star_color) * brightness);

            strip.setPixelColor(led, color(r, g, b));
            ++it;
        }
    }
} // namespace Show