#include "DeviceSensor.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#endif

namespace Sensor {

    bool DeviceSensor::begin() {
        return true;
    }

    SensorReading DeviceSensor::read() {
        SensorReading reading;
        reading.measurements.reserve(measurementCount());
        reading.valid = true;

#ifdef ARDUINO
        reading.timestamp = millis();

        // WiFi RSSI (only when connected)
        if (WiFi.status() == WL_CONNECTED) {
            reading.measurements.push_back({MeasurementType::Rssi, (int32_t)WiFi.RSSI(), getType(), false});
            reading.measurements.push_back({MeasurementType::Channel, WiFi.channel(), getType(), false});
        }

        // Internal chip temperature
        float chipTemp = temperatureRead();
        if (chipTemp > -40.0f && chipTemp < 125.0f) {
            reading.measurements.push_back({MeasurementType::System, chipTemp, getType(), false});
        }

        // Free heap memory (in bytes)
        reading.measurements.push_back({MeasurementType::FreeHeap, static_cast<float>(ESP.getFreeHeap()/1024.0), getType(), false});

        // Uptime in seconds
        reading.measurements.push_back({MeasurementType::Uptime, (int32_t)(millis() / 1000), getType(), false});
#endif

        return reading;
    }

    bool DeviceSensor::isConnected() {
        return true;  // Always available
    }

} // namespace Sensor
