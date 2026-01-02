#ifndef LEDZ_CHAOS_H
#define LEDZ_CHAOS_H
#include "Show.h"
#include "strip/Strip.h"

namespace Show {
    class Chaos : public Show {
        unsigned int iterations = 60;
        unsigned int color_factor = 4;
        const float x_initial = 0.5;
        float Rmin; // r_start - starting R value
        float Rmax; // r_max - maximum R value
        float Rdelta; // r_incr - increment per iteration
        float r; // current R value

        float func(float x) const;

    public:
        /**
         * Create Chaos show with default parameters
         */
        Chaos();

        /**
         * Create Chaos show with custom parameters
         * @param Rmin Starting R value (default: 2.95)
         * @param Rmax Maximum R value (default: 4.0)
         * @param Rdelta R increment per iteration (default: 0.0002)
         */
        Chaos(float Rmin, float Rmax, float Rdelta);

        void execute(Strip::Strip &strip, Iteration iteration) override;
    };
}

#endif //LEDZ_CHAOS_H