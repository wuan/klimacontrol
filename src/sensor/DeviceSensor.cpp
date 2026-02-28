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
            reading.measurements.push_back({"rssi", (int32_t)WiFi.RSSI(), "dBm", "ESP32", false});
            reading.measurements.push_back({"channel", WiFi.channel(), "", "ESP32", false});
        }

        // Internal chip temperature
        float chipTemp = temperatureRead();
        if (chipTemp > -40.0f && chipTemp < 125.0f) {
            reading.measurements.push_back({"system", chipTemp, "°C", "ESP32", false});
        }

        // Free heap memory (in bytes)
        reading.measurements.push_back({"free_heap", static_cast<float>(ESP.getFreeHeap()/1024.0), "kB", "ESP32", false});

        // Uptime in seconds
        reading.measurements.push_back({"uptime", (int32_t)(millis() / 1000), "s", "ESP32", false});
#endif

        return reading;
    }

    bool DeviceSensor::isConnected() {
        return true;  // Always available
    }

} // namespace Sensor
