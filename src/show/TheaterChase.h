#ifndef LEDZ_THEATERCHASE_H
#define LEDZ_THEATERCHASE_H

#include "Show.h"

namespace Show {
    /**
     * TheaterChase - Classic marquee-style LED animation with rotating rainbow colors
     * Creates a chase pattern where groups of LEDs light up in sequence with smooth color transitions
     */
    class TheaterChase : public Show {
    private:
        unsigned int num_steps_per_cycle; // Steps needed for one complete color rotation
        unsigned int index = 0; // Current animation step

    public:
        /**
         * Constructor with configurable parameters
         * @param num_steps_per_cycle Steps per complete color rotation (default: 21, should be multiple of 7)
         */
        TheaterChase(unsigned int num_steps_per_cycle = 21);

        /**
         * Execute the show - update theater chase animation
         * @param strip LED strip to control
         * @param iteration Current iteration number
         */
        void execute(Strip::Strip &strip, Iteration iteration) override;

        const char *name() { return "TheaterChase"; }
    };
} // namespace Show

#endif //LEDZ_THEATERCHASE_H