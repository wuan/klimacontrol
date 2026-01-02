#include "Fire.h"

#include <algorithm>
#include <random>
#include <utility>

#include "support/color.h"


namespace Show {
    // Maximum heat transfer per frame - limits how fast heat propagates upward
    constexpr float MAX_SPREAD_PER_FRAME = 0.25f;
    FireState::FireState(std::function<float()> randomFloat, Strip::PixelIndex length) :
        randomFloat(std::move(randomFloat)),
        _length(length),
        temperature(std::make_unique<float[]>(length)),
        prev_temperature(std::make_unique<float[]>(length)) {
        std::fill(temperature.get(), temperature.get() + length, 0.0f);
        std::fill(prev_temperature.get(), prev_temperature.get() + length, 0.0f);
    }

    Strip::PixelIndex FireState::length() const {
        return _length;
    }

    void FireState::cooldown(float value) {
        for (Strip::PixelIndex i = 0; i < length(); i++) {
            temperature[i] = std::max(0.0f, temperature[i] - value);
        }
    }

    void FireState::spread(float spread_rate, float ignition, Strip::PixelIndex spark_range, float spark_amount,
                           const std::vector<float> &weights) {
        // Copy current state to previous buffer for consistent reads during this frame
        std::copy(temperature.get(), temperature.get() + length(), prev_temperature.get());

        for (Strip::PixelIndex i = 0; i < length(); i++) {
            float weighted_previous = 0.0f;
            float available_energy = 0.0f;
            float local_total_weight = 0.0f;

            for (size_t w_idx = 0; w_idx < weights.size(); ++w_idx) {
                int prev_idx = i - 1 - (int) w_idx;
                if (prev_idx >= 0) {
                    local_total_weight += weights[w_idx];
                }
            }

            if (local_total_weight > 0) {
                for (size_t w_idx = 0; w_idx < weights.size(); ++w_idx) {
                    int prev_idx = i - 1 - (int) w_idx;
                    if (prev_idx >= 0) {
                        float w = weights[w_idx] / local_total_weight;
                        // Read from previous frame snapshot
                        weighted_previous += prev_temperature[prev_idx] * w;
                        available_energy += prev_temperature[prev_idx];
                    }
                }
            }

            auto spread_value = std::min(MAX_SPREAD_PER_FRAME, weighted_previous) * spread_rate * randomFloat();
            // We can't take more than what's available in any of the contributing pixels if we want to be safe,
            // but the weighted_previous already gives us a good limit.
            // To ensure energy conservation, we must ensure spread_amount <= sum of contributing temperatures.
            auto spread_amount = std::min(available_energy, spread_value);

            if (spread_amount > 0) {
                temperature[i] += spread_amount;
                for (size_t w_idx = 0; w_idx < weights.size(); ++w_idx) {
                    int prev_idx = i - 1 - (int) w_idx;
                    if (prev_idx >= 0) {
                        float w = weights[w_idx] / local_total_weight;
                        temperature[prev_idx] -= spread_amount * w;
                    }
                }
            }

            if (i < spark_range && randomFloat() <= ignition) {
                temperature[i] += spark_amount;
            }
        }
    }

    float FireState::get_temperature(Strip::PixelIndex pixel_index) const {
        if (pixel_index < 0 || pixel_index >= length()) {
            return 0.0f;
        }
        return temperature[pixel_index];
    }

    void FireState::set_temperature(Strip::PixelIndex pixel_index, float value) {
        if (pixel_index >= 0 && pixel_index < length()) {
            temperature[pixel_index] = value;
        }
    }


    Fire::Fire(float cooling, float spread, float ignition, float spark_amount, std::vector<float> weights,
                Strip::PixelIndex start_offset, Strip::PixelIndex spark_range) :
        cooling(cooling),
        spread(spread), ignition(ignition), spark_amount(spark_amount),
        weights(std::move(weights)),
        start_offset(start_offset),
        spark_range(spark_range),
        randomFloat(0.0f, 1.0f) {
        std::random_device rd;
        gen = std::mt19937(rd());
    }

    void Fire::ensureState(Strip::Strip &strip) {
        if (!state || state->length() != strip.length() + start_offset) {
            state = std::make_unique<FireState>([this] { return randomFloat(gen); }, strip.length() + start_offset);
        }
    }

    void Fire::execute(Strip::Strip &strip, [[maybe_unused]] Iteration iteration) {
        ensureState(strip);

        state->cooldown(cooling * randomFloat(gen));

        state->spread(spread, ignition, spark_range, spark_amount, weights);

        for (Strip::PixelIndex i = 0; i < strip.length(); i++) {
            // Mapping strip index i to state index i + start_offset
            strip.setPixelColor(i, Support::Color::black_body_color(state->get_temperature(i + start_offset)));
        }
    }
} // Show
