#ifndef DPS310_H
#define DPS310_H

#include "I2CSensor.h"

#ifdef ARDUINO
#include <Adafruit_DPS310.h>
#endif

namespace Sensor {

    class DPS310 : public I2CSensor {
#ifdef ARDUINO
        Adafruit_DPS310 dps;
#endif

    public:
        explicit DPS310(uint8_t address = 0x77);

        static const char* type() { return "DPS310"; }
        static const uint8_t* addresses() { static const uint8_t a[] = {0x76, 0x77}; return a; }
        static uint8_t addressCount() { return 2; }

        bool begin() override;
        SensorReading read() override;
        SensorReading read(const ReadConfig& config, const std::vector<Measurement>& prior) override;
        [[nodiscard]] const char* getType() const override { return type(); }
        [[nodiscard]] TypeSpan provides() const override {
            static constexpr MeasurementType types[] = {
                MeasurementType::Pressure, MeasurementType::SeaLevelPressure
            };
            return {types, 2};
        }
    };

} // namespace Sensor

#endif // DPS310_H
