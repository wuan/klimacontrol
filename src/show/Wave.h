#ifndef LEDZ_WAVE_H
#define LEDZ_WAVE_H

#include "Show.h"

namespace Show {
    /**
     * Wave - Creates a propagating wave effect with color cycling
     * Wave emanates from the start with changing brightness and exponential decay
     */
    class Wave : public Show {
    private:
        float wave_speed; // Speed of wave propagation (higher = faster)
        float decay_rate; // Rate of brightness decay towards ends (higher = faster decay)
        float brightness_frequency; // Frequency of brightness oscillation at source
        float wavelength; // Wavelength of the wave pattern (higher = longer waves)

        float time; // Time counter for wave position
        float color_time; // Time counter for color cycling

    public:
        /**
         * Constructor with configurable parameters
         * @param wave_speed Speed of wave propagation (default: 1.0)
         * @param decay_rate Rate of brightness decay (default: 2.0)
         * @param brightness_frequency Frequency of source brightness oscillation (default: 0.1)
         * @param wavelength Wavelength of wave pattern (default: 6.0)
         */
        Wave(float wave_speed = 1.0f,
             float decay_rate = 2.0f,
             float brightness_frequency = 0.1f,
             float wavelength = 6.0f);

        /**
         * Execute the show - update wave animation
         * @param strip LED strip to control
         * @param iteration Current iteration number
         */
        void execute(Strip::Strip &strip, Iteration iteration) override;

        const char *name() { return "Wave"; }
    };
} // namespace Show

#endif //LEDZ_WAVE_H