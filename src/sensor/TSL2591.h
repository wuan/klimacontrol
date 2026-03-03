#ifndef TSL2591_H
#define TSL2591_H

#include "I2CSensor.h"

#ifdef ARDUINO
#include <Adafruit_TSL2591.h>
#endif

namespace Sensor {

    class TSL2591 : public I2CSensor {
#ifdef ARDUINO
        Adafruit_TSL2591 tsl;
#endif

    public:
        explicit TSL2591(uint8_t address = 0x29);

        static const char* type() { return "TSL2591"; }
        static const uint8_t* addresses() { static const uint8_t a[] = {0x29}; return a; }
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

#endif // TSL2591_H
