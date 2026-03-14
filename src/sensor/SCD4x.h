#ifndef SCD4X_H
#define SCD4X_H

#include "I2CSensor.h"

#ifdef ARDUINO
#include <SensirionI2cScd4x.h>
#endif

namespace Sensor {

    class SCD4x : public I2CSensor {
#ifdef ARDUINO
        SensirionI2cScd4x scd;
#endif

        uint16_t co2 = 0;

    public:
        explicit SCD4x(uint8_t address = 0x62);

        static const char* type() { return "SCD4x"; }
        static const uint8_t* addresses() { static const uint8_t a[] = {0x62}; return a; }
        static uint8_t addressCount() { return 1; }

        bool begin() override;
        SensorReading read() override;
        const char* getType() const override { return type(); }
        TypeSpan provides() const override {
            static constexpr MeasurementType types[] = {
                MeasurementType::CO2
            };
            return {types, 1};
        }
    };

} // namespace Sensor

#endif // SCD4X_H
