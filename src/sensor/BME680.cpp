#include "BME680.h"

namespace Sensor {

    BME680::BME680(uint8_t address) : I2CSensor(address) {
        bme = std::make_unique<Adafruit_BME680>(&wire);
    }

    bool BME680::begin() {
#ifdef ARDUINO

        if (!bme->begin(i2cAddress)) {
            return false;
        }

        // Configure oversampling and filter
        bme->setTemperatureOversampling(BME680_OS_8X);
        bme->setHumidityOversampling(BME680_OS_2X);
        bme->setPressureOversampling(BME680_OS_4X);
        bme->setIIRFilterSize(BME680_FILTER_SIZE_3);
        bme->setGasHeater(320, 150); // 320°C for 150ms

        initialized = true;
        return true;
#else
        initialized = true;
        return true;
#endif
    }

    SensorReading BME680::read() {
        return read({}, {});
    }

    SensorReading BME680::read(const ReadConfig& config, const std::vector<Measurement>& prior) {
        (void) prior;
        SensorReading reading;
        reading.timestamp = millis();

        if (!initialized || !isConnected()) {
            reading.valid = false;
            return reading;
        }

#ifdef ARDUINO
        if (bme->performReading()) {
            float temperature = bme->temperature;
            float relative_humidity = bme->humidity;
            float stationPressure = bme->pressure / 100.0f;
            reading.measurements.push_back({MeasurementType::Temperature, temperature, getType(), false});
            reading.measurements.push_back({MeasurementType::RelativeHumidity, relative_humidity, getType(), false});
            reading.measurements.push_back({MeasurementType::DewPoint, calcDewPoint(temperature, relative_humidity), getType(), true});
            reading.measurements.push_back({MeasurementType::Pressure, stationPressure, getType(), false});

            if (config.elevation > 0.0f) {
                float seaLevel = calcSeaLevelPressure(stationPressure, temperature, config.elevation);
                reading.measurements.push_back({MeasurementType::SeaLevelPressure, seaLevel, getType(), true});
            }

            reading.valid = true;
        } else {
            reading.valid = false;
        }
#else
        float t = 23.0f;
        float rh = 50.0f;
        float stationPressure = 1013.25f;
        reading.measurements.push_back({MeasurementType::Temperature, t, getType(), false});
        reading.measurements.push_back({MeasurementType::RelativeHumidity, rh, getType(), false});
        reading.measurements.push_back({MeasurementType::Pressure, stationPressure, getType(), false});
        reading.measurements.push_back({MeasurementType::DewPoint, calcDewPoint(t, rh), getType(), true});

        if (config.elevation > 0.0f) {
            float seaLevel = calcSeaLevelPressure(stationPressure, t, config.elevation);
            reading.measurements.push_back({MeasurementType::SeaLevelPressure, seaLevel, getType(), true});
        }

        reading.valid = true;
#endif

        return reading;
    }

} // namespace Sensor
