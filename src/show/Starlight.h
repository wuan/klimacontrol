#ifndef LEDZ_STARLIGHT_H
#define LEDZ_STARLIGHT_H

#include "Show.h"
#include <map>

namespace Show {
    /**
     * Starlight - Creates a twinkling stars effect
     * LEDs randomly activate with fade-in, hold, and fade-out phases
     */
    class Starlight : public Show {
    private:
        float probability; // Probability of spawning a new star each frame (0.0-1.0)
        unsigned long length_ms; // Duration at full brightness (milliseconds)
        unsigned long fade_ms; // Fade-in/fade-out duration (milliseconds)
        Strip::Color star_color; // Color of the stars

        // Track active stars: LED index -> start time (milliseconds)
        std::map<uint16_t, unsigned long> active_stars;

        /**
         * Calculate brightness for a star based on elapsed time
         * @param elapsed_ms Time since star started (milliseconds)
         * @return Brightness factor (0.0 to 1.0)
         */
        float calculateBrightness(unsigned long elapsed_ms);

        /**
         * Get total star lifetime (fade-in + hold + fade-out)
         * @return Total lifetime in milliseconds
         */
        unsigned long getTotalLifetime() const {
            return fade_ms + length_ms + fade_ms;
        }

    public:
        /**
         * Constructor with configurable parameters
         * @param probability Probability of spawning new star per frame (0.0-1.0, default: 0.1)
         * @param length_ms Duration at full brightness in ms (default: 5000ms = 5 seconds)
         * @param fade_ms Fade-in/out duration in ms (default: 1000ms = 1 second)
         * @param r Red component of star color (default: 255)
         * @param g Green component of star color (default: 180)
         * @param b Blue component of star color (default: 50)
         */
        Starlight(float probability = 0.01f,
                  unsigned long length_ms = 5000,
                  unsigned long fade_ms = 1000,
                  uint8_t r = 255,
                  uint8_t g = 180,
                  uint8_t b = 50);

        /**
         * Execute the show - update twinkling stars
         * @param strip LED strip to control
         * @param iteration Current iteration number
         */
        void execute(Strip::Strip &strip, Iteration iteration) override;

        const char *name() { return "Starlight"; }
    };
} // namespace Show

#endif //LEDZ_STARLIGHT_H