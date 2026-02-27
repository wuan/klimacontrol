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
        reading.valid = true;

#ifdef ARDUINO
        reading.timestamp = millis();

        // WiFi RSSI (only when connected)
        if (WiFi.status() == WL_CONNECTED) {
            reading.measurements.push_back({"rssi", static_cast<float>(WiFi.RSSI()), "dBm", "ESP32", false});
        }

        // Internal chip temperature
        float chipTemp = temperatureRead();
        if (chipTemp > -40.0f && chipTemp < 125.0f) {
            reading.measurements.push_back({"chip_temperature", chipTemp, "°C", "ESP32", false});
        }

        // Free heap memory (in KB for readability)
        float freeHeapKB = static_cast<float>(ESP.getFreeHeap()) / 1024.0f;
        reading.measurements.push_back({"free_heap", freeHeapKB, "KB", "ESP32", false});

        // Uptime in seconds
        float uptimeSeconds = static_cast<float>(millis() / 1000);
        reading.measurements.push_back({"uptime", uptimeSeconds, "s", "ESP32", false});
#endif

        return reading;
    }

    const char* DeviceSensor::getName() const {
        return "ESP32";
    }

    const char* DeviceSensor::getType() const {
        return "Device";
    }

    bool DeviceSensor::isConnected() {
        return true;  // Always available
    }

} // namespace Sensor
