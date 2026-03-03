#ifndef VEML7700_H
#define VEML7700_H

#include "I2CSensor.h"

#ifdef ARDUINO
#include <Adafruit_VEML7700.h>
#endif

namespace Sensor {

    class VEML7700 : public I2CSensor {
#ifdef ARDUINO
        Adafruit_VEML7700 veml;
#endif

    public:
        explicit VEML7700(uint8_t address = 0x10);

        static const char* type() { return "VEML7700"; }
        static const uint8_t* addresses() { static const uint8_t a[] = {0x10}; return a; }
        static uint8_t addressCount() { return 1; }

        bool begin() override;
        SensorReading read() override;
        [[nodiscard]] const char* getType() const override { return type(); }
        [[nodiscard]] TypeSpan provides() const override {
            static constexpr MeasurementType types[] = {
                MeasurementType::Illuminance
            };
            return {types, 1};
        }
    };

} // namespace Sensor

#endif // VEML7700_H
