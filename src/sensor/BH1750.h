#ifndef BH1750_SENSOR_H
#define BH1750_SENSOR_H

#include "I2CSensor.h"

#ifdef ARDUINO
#include <BH1750.h>
#endif

namespace Sensor {

    class BH1750Sensor : public I2CSensor {
#ifdef ARDUINO
        BH1750 bh;
#endif

    public:
        explicit BH1750Sensor(uint8_t address = 0x23);

        static const char* type() { return "BH1750"; }
        static const uint8_t* addresses() { static const uint8_t a[] = {0x23, 0x5C}; return a; }
        static uint8_t addressCount() { return 2; }

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

#endif // BH1750_SENSOR_H
