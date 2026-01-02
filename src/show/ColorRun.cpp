#include "ColorRun.h"

#include <algorithm>
#include <random>
#include <sstream>
#ifdef ARDUINO
#include <USBCDC.h>
#endif

namespace Show {
    ColorRun::ColorRun() : randomPercent(0, 99), randomSpeed(20, 60) {
        this->phases = {0x000000, 0x0000FF, 0x00FF00, 0x00FFFF, 0xFF0000, 0xFF00FF, 0xFFFF00, 0xFFFFFF};
        this->states = std::vector<State>();

        auto state = State{0, 0.5, 0xFF0000};
        states.push_back(state);

        this->randomPhase = std::uniform_int_distribution<>(0, phases.size() - 1);

        std::random_device rd;
        this->gen = std::mt19937(rd());
    }

    void ColorRun::update_state(Iteration iteration) {
        if (randomPercent(gen) >= 95) {
            auto speed = randomSpeed(gen) / 100.0f;
            auto color = phases[randomPhase(gen)];
            auto state = State{iteration, speed, color};
            states.push_back(state);
        }
    }

    void ColorRun::execute(Strip::Strip &strip, Iteration iteration) {
        update_state(iteration);

        strip.fill(0x000000);

        for (auto state: states) {
            strip.setPixelColor(state.position(iteration), state.color);
        }

        clean_up_state(strip.length(), iteration);
    }

    void ColorRun::clean_up_state(Strip::PixelIndex length, Iteration iteration) {
        states.erase(
            std::remove_if(states.begin(), states.end(), [&](const State &state) {
                return state.position(iteration) >= length;
            }),
            states.end()
        );
    }


    Strip::PixelIndex ColorRun::State::position(Iteration iteration) const {
        return speed * (iteration - start);
    }
} // Show