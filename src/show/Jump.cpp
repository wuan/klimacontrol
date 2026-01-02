#include "Jump.h"

#include <cmath>
#ifdef ARDUINO
#include <USBCDC.h>
#endif

namespace Show {
    void Jump::execute(Strip::Strip &strip, Iteration iteration) {
        strip.fill(0x000000);

        for (Ball &ball: balls) {
            auto pos = ball.get_position(iteration, strip.length());
            strip.setPixelColor(pos, ball.get_color());

            if (ball.is_next()) {
                ball.swap_color(spare_colors);
            }
        }
    }

    Jump::Jump() {
        spare_colors.push(0xffff00);
    }

    Jump::Ball::Ball(const float peak_factor, Strip::Color color) : peak_factor(peak_factor), color(color) {
    }


    Strip::PixelIndex Jump::Ball::get_position(Iteration iteration, Strip::PixelIndex stripe_size) {
        auto factor = 10.0f;
        float amplitude = peak_factor * stripe_size;
        auto duration = 2.0f * std::sqrt(amplitude) * factor;
        auto center = duration / 2.0f;
        auto period_length = static_cast<uint>(duration);
        unsigned int current_period = iteration / period_length;

        if (period != current_period && !next) {
            period = current_period;
            next = true;
        }

        unsigned int position = iteration % period_length;

        return static_cast<Strip::PixelIndex>(amplitude - std::pow((position - center) / factor, 2));
    }

    void Jump::Ball::swap_color(std::queue<Strip::Color> &colors) {
        colors.push(color);
        auto old_color = color;
        color = colors.front();
        colors.pop();
        // Serial.printf("swap color %x -> %x, period: %d, next: %d\n", old_color, color, period, next);
    }

    bool Jump::Ball::is_next() {
        if (next) {
            next = false;
            return true;
        }
        return false;
    }

    unsigned int Jump::Ball::get_period() const {
        return period;
    }

    Strip::Color Jump::Ball::get_color() const {
        return color;
    }
} // Show