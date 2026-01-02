#ifndef LEDZ_FIRE_H
#define LEDZ_FIRE_H
#include <random>
#include <functional>
#include <memory>

#include <vector>
#include "Show.h"

namespace Show {
    class FireState {
        Strip::PixelIndex _length;
        std::function<float()> randomFloat;
        std::unique_ptr<float[]> temperature;
        std::unique_ptr<float[]> prev_temperature;

    public:
        explicit FireState(std::function<float()> randomFloat,
                  Strip::PixelIndex length);

        ~FireState() = default;
        FireState(const FireState&) = delete;
        FireState& operator=(const FireState&) = delete;
        FireState(FireState&&) = default;
        FireState& operator=(FireState&&) = default;

        [[nodiscard]] Strip::PixelIndex length() const;

        void cooldown(float value);

        void spread(float spread_rate, float ignition, Strip::PixelIndex spark_range, float spark_amount,
                    const std::vector<float> &weights = {1.0f});

        float get_temperature(Strip::PixelIndex pixel_index) const;
        void set_temperature(Strip::PixelIndex pixel_index, float value);
    };

    class Fire : public Show {
        std::unique_ptr<FireState> state;
        std::mt19937 gen;
        std::uniform_real_distribution<float> randomFloat;

        float cooling;
        float spread;
        float ignition;
        float spark_amount;
        std::vector<float> weights;
        Strip::PixelIndex start_offset;
        Strip::PixelIndex spark_range;

    public:
        Fire(float cooling = 0.1f, float spread = 10.0f, float ignition = .5f, float spark_amount = 0.5f,
             std::vector<float> weights = {1.0f}, Strip::PixelIndex start_offset = 5,
             Strip::PixelIndex spark_range = 5);

        void ensureState(Strip::Strip &strip);

        void execute(Strip::Strip &strip, Iteration iteration) override;
    };
} // Show

#endif //LEDZ_FIRE_H
