#ifndef LEDZ_STROBOSCOPE_H
#define LEDZ_STROBOSCOPE_H

#include "Show.h"

namespace Show {
    /**
     * Stroboscope - Flashing strobe effect with configurable on/off cycles
     * Flashes a color for a specified number of cycles, then stays black
     */
    class Stroboscope : public Show {
    private:
        uint8_t r, g, b; // Color to flash
        unsigned int on_cycles; // Number of cycles to stay on
        unsigned int off_cycles; // Number of cycles to stay off
        unsigned int current_cycle; // Current cycle counter

    public:
        /**
         * Constructor with configurable parameters
         * @param r Red component (0-255, default: 255)
         * @param g Green component (0-255, default: 255)
         * @param b Blue component (0-255, default: 255)
         * @param on_cycles Number of cycles to flash on (default: 1)
         * @param off_cycles Number of cycles to stay off (default: 10)
         */
        Stroboscope(uint8_t r = 255, uint8_t g = 255, uint8_t b = 255,
                    unsigned int on_cycles = 1, unsigned int off_cycles = 10);

        /**
         * Execute the show - update stroboscope effect
         * @param strip LED strip to control
         * @param iteration Current iteration number
         */
        void execute(Strip::Strip &strip, Iteration iteration) override;

        const char *name() { return "Stroboscope"; }
    };
} // namespace Show

#endif //LEDZ_STROBOSCOPE_H