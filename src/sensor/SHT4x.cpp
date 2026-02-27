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

        Wire1.setPins(SDA1, SCL1); // Zwingend erforderlich für ESP32-S2!

        if (!Wire1.begin()) {
            Serial.println("SHT4x: Failed to initialize I2C");
            return false;
        }

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
            reading.measurements.push_back({"temperature", temp.temperature, "°C", "SHT4x", false});
            reading.measurements.push_back({"humidity", humidity.relative_humidity, "%", "SHT4x", false});
            reading.valid = true;
        } else {
            reading.valid = false;
            Serial.println("SHT4x: Failed to read sensor data");
        }
#else
        // For native testing, return some dummy values
        reading.measurements.push_back({"temperature", 22.5f, "°C", "SHT4x", false});
        reading.measurements.push_back({"humidity", 45.0f, "%", "SHT4x", false});
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
