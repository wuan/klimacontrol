#ifndef BME680_H
#define BME680_H

#include "I2CSensor.h"

#ifdef ARDUINO
#include <Adafruit_BME680.h>
#endif

namespace Sensor {

    class BME680 : public I2CSensor {
#ifdef ARDUINO
        std::unique_ptr<Adafruit_BME680> bme;
#endif

    public:
        explicit BME680(uint8_t address = 0x77);

        static const char* type() { return "BME680"; }
        static const uint8_t* addresses() { static const uint8_t a[] = {0x76, 0x77}; return a; }
        static uint8_t addressCount() { return 2; }

        bool begin() override;
        SensorReading read() override;
        SensorReading read(const ReadConfig& config, const std::vector<Measurement>& prior) override;
        [[nodiscard]] const char* getType() const override { return type(); }
        [[nodiscard]] TypeSpan providesMeasurements() const override {
            static constexpr MeasurementType types[] = {
                MeasurementType::Temperature, MeasurementType::RelativeHumidity,
                MeasurementType::DewPoint, MeasurementType::Pressure,
                MeasurementType::GasResistance, MeasurementType::SeaLevelPressure
            };
            return {types, 6};
        }
    };

} // namespace Sensor

#endif // BME680_H
