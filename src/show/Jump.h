#ifndef LEDZ_JUMP_H
#define LEDZ_JUMP_H
#include <queue>
#include <vector>

#include "Show.h"
#include "strip/Strip.h"

namespace Show {
    class Jump : public Show {
    public:
        class Ball {
            const float peak_factor;
            Strip::Color color;
            unsigned int period = 0;
            bool next = false;

        public:
            Ball(float peak_factor, Strip::Color color);

            Strip::PixelIndex get_position(Iteration iteration, Strip::PixelIndex stripe_size);

            void swap_color(std::queue<Strip::Color> &colors);

            bool is_next();

            unsigned int get_period() const;

            Strip::Color get_color() const;
        };

    private:
        Ball balls[5] = {
            Ball(1.0, 0xff0000),
            Ball(0.5, 0x00ff00),
            Ball(0.75, 0xffff00),
            Ball(0.80, 0xff00ff),
            Ball(0.66, 0x0000ff)
        };

        std::queue<Strip::Color> spare_colors;

    public:
        Jump();

        void execute(Strip::Strip &strip, Iteration iteration) override;
    };
} // Show

#endif //LEDZ_JUMP_H