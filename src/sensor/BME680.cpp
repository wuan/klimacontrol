#include "BME680.h"

#ifdef ARDUINO
#include <Wire.h>
#endif

namespace Sensor {

    BME680::BME680(uint8_t address) : I2CSensor(address) {
    }

    bool BME680::begin() {
#ifdef ARDUINO
        Serial.println("BME680: Initializing sensor...");

        if (!bme.begin(i2cAddress)) {
            Serial.println("BME680: Failed to initialize sensor");
            return false;
        }

        // Configure oversampling and filter
        bme.setTemperatureOversampling(BME680_OS_8X);
        bme.setHumidityOversampling(BME680_OS_2X);
        bme.setPressureOversampling(BME680_OS_4X);
        bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
        bme.setGasHeater(320, 150); // 320°C for 150ms

        initialized = true;
        Serial.println("BME680: Sensor initialized successfully");
        return true;
#else
        initialized = true;
        return true;
#endif
    }

    SensorReading BME680::read() {
        SensorReading reading;
        reading.timestamp = millis();

        if (!initialized || !isConnected()) {
            reading.valid = false;
            return reading;
        }

#ifdef ARDUINO
        if (bme.performReading()) {
            float t = bme.temperature;
            float rh = bme.humidity;
            reading.measurements.push_back({"temperature", t, "°C", "BME680", false});
            reading.measurements.push_back({"relative humidity", rh, "%", "BME680", false});
            reading.measurements.push_back({"dew point", calcDewPoint(t, rh), "°C", "BME680", true});
            reading.measurements.push_back({"pressure", bme.pressure / 100.0f, "hPa", "BME680", false});

            reading.valid = true;
        } else {
            reading.valid = false;
            Serial.println("BME680: Failed to read sensor data");
        }
#else
        float t = 23.0f;
        float rh = 50.0f;
        reading.measurements.push_back({"temperature", t, "°C", "BME680", false});
        reading.measurements.push_back({"humidity", rh, "%", "BME680", false});
        reading.measurements.push_back({"pressure", 1013.25f, "hPa", "BME680", false});

        reading.measurements.push_back({"dew_point", calcDewPoint(t, rh), "°C", "BME680", true});

        reading.valid = true;
#endif

        return reading;
    }

} // namespace Sensor
