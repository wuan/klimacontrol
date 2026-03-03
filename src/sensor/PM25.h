#ifndef PM25_H
#define PM25_H

#include "I2CSensor.h"

#ifdef ARDUINO
#include <Adafruit_PM25AQI.h>
#endif

namespace Sensor {

    class PM25 : public I2CSensor {
#ifdef ARDUINO
        Adafruit_PM25AQI aqi;
#endif

    public:
        explicit PM25(uint8_t address = 0x12);

        static const char* type() { return "PM25"; }
        static const uint8_t* addresses() { static const uint8_t a[] = {0x12}; return a; }
        static uint8_t addressCount() { return 1; }

        bool begin() override;
        SensorReading read() override;
        [[nodiscard]] const char* getType() const override { return type(); }
        [[nodiscard]] TypeSpan provides() const override {
            static constexpr MeasurementType types[] = {
                MeasurementType::Particles03, MeasurementType::Particles05,
                MeasurementType::Particles10, MeasurementType::Particles25,
                MeasurementType::Particles50, MeasurementType::Particles100,
                MeasurementType::PM10Concentration, MeasurementType::PM25Concentration,
                MeasurementType::PM100Concentration
            };
            return {types, 9};
        }
    };

} // namespace Sensor

#endif // PM25_H
