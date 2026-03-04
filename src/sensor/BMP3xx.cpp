#include "BMP3xx.h"

namespace Sensor {

    BMP3xx::BMP3xx(uint8_t address) : I2CSensor(address) {
    }

    bool BMP3xx::begin() {
#ifdef ARDUINO
        Serial.println("BMP3xx: Initializing sensor...");

        if (!bmp.begin_I2C(i2cAddress, &wire)) {
            return false;
        }

        // Configure oversampling and filter
        bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_8X);
        bmp.setPressureOversampling(BMP3_OVERSAMPLING_4X);
        bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
        bmp.setOutputDataRate(BMP3_ODR_50_HZ);

        initialized = true;
        return true;
#else
        initialized = true;
        return true;
#endif
    }

    SensorReading BMP3xx::read() {
        return read({}, {});
    }

    SensorReading BMP3xx::read(const ReadConfig& config, const std::vector<Measurement>& prior) {
        (void) prior;
        SensorReading reading;
        reading.measurements.reserve(measurementCount());
        reading.timestamp = millis();

        if (!initialized || !isConnected()) {
            reading.valid = false;
            return reading;
        }

#ifdef ARDUINO
        if (bmp.performReading()) {
            float pressure = static_cast<float>(bmp.pressure / 100.0);
            reading.measurements.push_back({MeasurementType::Pressure, pressure, getType(), false});

            if (config.elevation > 0.0f) {
                float seaLevel = calcSeaLevelPressure(pressure, static_cast<float>(bmp.temperature), config.elevation);
                reading.measurements.push_back({MeasurementType::SeaLevelPressure, seaLevel, getType(), true});
            }

            reading.valid = true;
        } else {
            reading.valid = false;
        }
#else
        float stationPressure = 1013.25f;
        float temperature = 22.0f;
        reading.measurements.push_back({MeasurementType::Pressure, stationPressure, getType(), false});

        if (config.elevation > 0.0f) {
            float seaLevel = calcSeaLevelPressure(stationPressure, temperature, config.elevation);
            reading.measurements.push_back({MeasurementType::SeaLevelPressure, seaLevel, getType(), true});
        }

        reading.valid = true;
#endif

        return reading;
    }

} // namespace Sensor
