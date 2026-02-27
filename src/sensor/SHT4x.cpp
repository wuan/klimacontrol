#include "SHT4x.h"

#ifdef ARDUINO
#include <Wire.h>
#endif

namespace Sensor {

    SHT4x::SHT4x(uint8_t address) : i2cAddress(address), initialized(false) {
#ifdef ARDUINO
        sht4x = Adafruit_SHT4x();
#endif
    }

    bool SHT4x::begin() {
#ifdef ARDUINO
        Serial.println("SHT4x: Initializing sensor...");

        if (!sht4x.begin(&Wire1)) {
            Serial.println("SHT4x: Failed to initialize sensor");
            return false;
        }

        // Set high precision mode
        sht4x.setPrecision(SHT4X_HIGH_PRECISION);
        sht4x.setHeater(SHT4X_NO_HEATER);

        initialized = true;
        Serial.println("SHT4x: Sensor initialized successfully");
        return true;
#else
        // For native testing, just mark as initialized
        initialized = true;
        return true;
#endif
    }

    SensorReading SHT4x::read() {
        SensorReading reading;
        reading.timestamp = millis();

        if (!initialized || !isConnected()) {
            reading.valid = false;
            return reading;
        }

#ifdef ARDUINO
        sensors_event_t humidity, temp;

        if (sht4x.getEvent(&humidity, &temp)) {
            float t = temp.temperature;
            float rh = humidity.relative_humidity;
            reading.measurements.push_back({"temperature", t, "°C", "SHT4x", false});
            reading.measurements.push_back({"humidity", rh, "%", "SHT4x", false});
            reading.measurements.push_back({"dew_point", calcDewPoint(t, rh), "°C", "SHT4x", true});

            reading.valid = true;
        } else {
            reading.valid = false;
            Serial.println("SHT4x: Failed to read sensor data");
        }
#else
        // For native testing, return some dummy values
        float t = 22.5f;
        float rh = 45.0f;
        reading.measurements.push_back({"temperature", t, "°C", "SHT4x", false});
        reading.measurements.push_back({"humidity", rh, "%", "SHT4x", false});

        reading.measurements.push_back({"dew_point", calcDewPoint(t, rh), "°C", "SHT4x", true});

        reading.valid = true;
#endif

        return reading;
    }

    const char* SHT4x::getName() const {
        return "SHT4x";
    }

    const char* SHT4x::getType() const {
        return "Temperature/Humidity";
    }

    bool SHT4x::isConnected() {
#ifdef ARDUINO
        if (!initialized) return false;

        // Try to read the serial number to check connection
        uint32_t serial = sht4x.readSerial();
        return serial != 0;
#else
        return initialized;
#endif
    }

} // namespace Sensor
