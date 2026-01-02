#ifndef LEDZ_SMOOTHBLEND_H
#define LEDZ_SMOOTHBLEND_H

#include <vector>
#include "../strip/Strip.h"

namespace Support {
    /**
     * SmoothBlend creates smooth color transitions over time.
     * It interpolates between initial colors and target colors over a 2-second period.
     */
    class SmoothBlend {
    public:
        /**
         * Create a smooth blend transition
         * @param strip The LED strip to animate
         * @param target_colors Vector of target colors for each LED
         * @param duration_ms Duration of the blend in milliseconds (default: 2000ms)
         */
        SmoothBlend(Strip::Strip &strip, const std::vector<Strip::Color> &target_colors,
                    unsigned long duration_ms = 2000);

        /**
         * Create a smooth blend to a single color for all LEDs
         * @param strip The LED strip to animate
         * @param target_color Single target color for all LEDs
         * @param duration_ms Duration of the blend in milliseconds (default: 2000ms)
         */
        SmoothBlend(Strip::Strip &strip, Strip::Color target_color, unsigned long duration_ms = 2000);

        /**
         * Perform one step of the blend animation.
         * Call this repeatedly (e.g., in a loop) to animate the transition.
         * @return true if the blend is still in progress, false if complete
         */
        bool step();

        /**
         * Check if the blend animation is complete
         */
        bool isComplete() const;

    private:
        Strip::Strip &strip;
        std::vector<Strip::Color> initial_colors;
        std::vector<Strip::Color> target_colors;
        unsigned long start_time;
        unsigned long duration_ms;
    };
} // namespace Support

#endif //LEDZ_SMOOTHBLEND_H