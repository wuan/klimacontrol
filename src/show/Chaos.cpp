#include "Chaos.h"

#include "color.h"

namespace Show {
    float Chaos::func(float x) const {
        return r * x * (1 - x);
    }

    Chaos::Chaos() : Chaos(2.95f, 4.0f, 0.0002f) {
        // Delegate to parameterized constructor with defaults
    }

    Chaos::Chaos(float Rmin, float Rmax, float Rdelta)
        : Rmin(Rmin), Rmax(Rmax), Rdelta(Rdelta), r(Rmin) {
        // Initialize with provided parameters
    }

    void Chaos::execute(Strip::Strip &strip, Iteration iteration) {
        strip.fill(0x000000);

        auto num_leds = strip.length();

        float pixel_scale;
        if (num_leds > 1) {
            pixel_scale = static_cast<float>(num_leds - 1);
        } else {
            pixel_scale = 1.0f;
        }

        auto x = x_initial;
        for (int i = 0; i < iterations; i++) {
            x = func(x);

            auto led = static_cast<int16_t>(x * pixel_scale);
            Strip::Color color = wheel((i * color_factor) % 255);
            strip.setPixelColor(led, color);
        }

        r += Rdelta;
        if (r > Rmax) {
            r = Rmin;
        }
    }
}