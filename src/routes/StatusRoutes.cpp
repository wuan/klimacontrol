#include "WebServerManager.h"
#include "routes/RouteHelpers.h"

#include "Config.h"
#include "Network.h"
#include "SensorController.h"
#include "task/SensorMonitor.h"
#include "Constants.h"

#ifdef ARDUINO
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#endif

void WebServerManager::setupStatusRoutes() {
#ifdef ARDUINO
    // GET /api/status - Get device status
    server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        JsonDocument doc;

        // Device info
        Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
        doc["device_id"] = deviceConfig.device_id;
        doc["device_name"] = deviceConfig.device_name;
        doc["firmware_version"] = FIRMWARE_VERSION;
        doc["elevation"] = deviceConfig.elevation;

        // OTA partition info
        const esp_partition_t *running_partition = esp_ota_get_running_partition();
        if (running_partition != nullptr) {
            doc["ota_partition"] = running_partition->label;
        }

        // Sensor info
        doc["sensor_connected"] = sensorController.hasConnectedSensors();
        doc["sensor_valid"] = sensorController.isDataValid();

        if (sensorController.isDataValid()) {
            // Backward-compatible top-level fields
            float temperature = sensorController.getTemperature();
            if (!isnan(temperature)) doc["temperature"] = temperature;
            float relativeHumidity = sensorController.getRelativeHumidity();
            if (!isnan(relativeHumidity)) doc["relative_humidity"] = relativeHumidity;
            float dewPoint = sensorController.getDewPoint();
            if (!isnan(dewPoint)) doc["dew_point"] = dewPoint;
            doc["sensor_timestamp"] = sensorController.getLastReadingTimestamp();
        }


        // Temperature control info
        doc["target_temperature"] = sensorController.getTargetTemperature();
        doc["control_enabled"] = sensorController.isControlEnabled();

        // Network info
        doc["wifi_connected"] = WiFiClass::status() == WL_CONNECTED;
        if (WiFiClass::status() == WL_CONNECTED) {
            doc["ip_address"] = WiFi.localIP().toString();
            doc["wifi_ssid"] = WiFi.SSID();
        }

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // GET /api/about - Device information
    server.on("/api/about", HTTP_GET, [this](AsyncWebServerRequest *request) {
        JsonDocument doc;

        // Device info
        Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
        doc["device_id"] = deviceConfig.device_id;

        // Sensor statistics
        auto statsJson = doc["stats"].to<JsonObject>();
        statsJson["sensor_count"] = sensorController.getSensorCount();
        statsJson["has_connected_sensors"] = sensorController.hasConnectedSensors();
        statsJson["time_since_last_reading"] = sensorController.getTimeSinceLastReading();

        // Cycle delay statistics
        const auto& cycleStats = sensorMonitor.getStats();
        statsJson["cycle_count"] = cycleStats.get_count();
        statsJson["avg_cycle_delay"] = cycleStats.get_average();
        statsJson["min_cycle_delay"] = cycleStats.get_min();
        statsJson["max_cycle_delay"] = cycleStats.get_max();

        // Chip info
        doc["chip_model"] = ESP.getChipModel();
        doc["chip_revision"] = ESP.getChipRevision();
        doc["chip_cores"] = ESP.getChipCores();
        doc["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
#ifdef ARDUINO
        doc["cpu_temp"] = temperatureRead();
#endif

        // Memory info
        doc["free_heap"] = ESP.getFreeHeap();
        doc["heap_size"] = ESP.getHeapSize();
        doc["min_free_heap"] = ESP.getMinFreeHeap();
        doc["psram_size"] = ESP.getPsramSize();

        // Flash info
        doc["flash_size"] = ESP.getFlashChipSize();
        doc["flash_speed"] = ESP.getFlashChipSpeed();

        // Runtime info
        doc["uptime_ms"] = millis();

        // Network info
        if (WiFiClass::status() == WL_CONNECTED) {
            doc["wifi_ssid"] = WiFi.SSID();
            doc["wifi_rssi"] = WiFi.RSSI();
            doc["wifi_tx_power"] = WiFi.getTxPower();
            doc["ip_address"] = WiFi.localIP().toString();
            doc["mac_address"] = WiFi.macAddress();
        } else if (WiFiClass::getMode() == WIFI_AP) {
            doc["ap_ssid"] = WiFi.softAPgetHostname();
            doc["ap_ip"] = WiFi.softAPIP().toString();
            doc["ap_clients"] = WiFi.softAPgetStationNum();
        }

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // GET /api/measurements - Get detailed overview of most recent measurements as a table
    server.on("/api/measurements", HTTP_GET, [this](AsyncWebServerRequest *request) {
        JsonDocument doc;

        doc["valid"] = sensorController.isDataValid();
        doc["timestamp"] = sensorController.getLastReadingTimestamp();

        JsonArray rows = doc["measurements"].to<JsonArray>();
        if (sensorController.isDataValid()) {
            for (const auto &m : sensorController.getMeasurements()) {
                auto row = rows.add<JsonObject>();
                row["type"] = Sensor::measurementTypeLabel(m.type);
                row["unit"] = Sensor::measurementTypeUnit(m.type);
                row["sensor"] = m.sensor;
                row["calculated"] = m.calculated;
                if (const auto *i = std::get_if<int32_t>(&m.value)) {
                    row["value"] = *i;
                } else {
                    row["value"] = std::get<float>(m.value);
                }
            }
        }

        String measurementResponse;
        serializeJson(doc, measurementResponse);
        request->send(200, CONTENT_TYPE_JSON, measurementResponse);
    });
#endif
}
