#ifndef SGP40_H
#define SGP40_H

#include "I2CSensor.h"

#ifdef ARDUINO
#include <Adafruit_SGP40.h>
#endif

namespace Sensor {

    class SGP40 : public I2CSensor {
#ifdef ARDUINO
        Adafruit_SGP40 sgp;
#endif

    public:
        explicit SGP40(uint8_t address = 0x59);

        static const char* type() { return "SGP40"; }
        static const uint8_t* addresses() { static const uint8_t a[] = {0x59}; return a; }
        static uint8_t addressCount() { return 1; }

        bool begin() override;
        SensorReading read() override;
        SensorReading read(const ReadConfig& config, const std::vector<Measurement>& prior) override;
        [[nodiscard]] const char* getType() const override { return type(); }
        [[nodiscard]] TypeSpan provides() const override {
            static constexpr MeasurementType types[] = {MeasurementType::VocIndex};
            return {types, 1};
        }
        [[nodiscard]] TypeSpan requires() const override {
            static constexpr MeasurementType types[] = {
                MeasurementType::Temperature, MeasurementType::RelativeHumidity
            };
            return {types, 2};
        }
    };

} // namespace Sensor

#endif // SGP40_H
