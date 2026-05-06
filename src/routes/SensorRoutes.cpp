#include "WebServerManager.h"
#include "routes/RouteHelpers.h"

#include <cstdio>
#include <cstring>

#include "Config.h"
#include "SensorController.h"
#include "I2CScanner.h"

#ifdef ARDUINO
#include <ArduinoJson.h>
#endif

void WebServerManager::setupSensorRoutes() {
#ifdef ARDUINO
    // GET /api/sensors/config - Get sensor configuration as devices array
    // NOTE: Must be registered before /api/sensors to avoid prefix matching
    server.on("/api/sensors/config", HTTP_GET, [this](AsyncWebServerRequest *request) {
        Config::SensorConfig sensorConfig = config.loadSensorConfig();

        JsonDocument doc;
        JsonArray arr = doc["devices"].to<JsonArray>();

        // Parse assignment string "44=SHT4x,77=BME680" into JSON array
        char buf[128];
        strlcpy(buf, sensorConfig.assignments, sizeof(buf));

        char* token = strtok(buf, ",");
        while (token) {
            char* eq = strchr(token, '=');
            if (eq) {
                *eq = '\0';
                auto addr = (uint8_t)strtoul(token, nullptr, 10);
                const char* name = eq + 1;

                auto obj = arr.add<JsonObject>();
                obj["address"] = addr;
                obj["type"] = name;
            }
            token = strtok(nullptr, ",");
        }

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // GET /api/sensors/registry - Get known sensor types and their I2C addresses
    // NOTE: Must be registered before /api/sensors to avoid prefix matching
    server.on("/api/sensors/registry", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;

        size_t registryCount;
        const SensorInfo* registry = I2CScanner::getRegistry(registryCount);
        for (size_t i = 0; i < registryCount; i++) {
            JsonArray addrs = doc[registry[i].name].to<JsonArray>();
            for (uint8_t j = 0; j < registry[i].addressCount; j++) {
                addrs.add(registry[i].addresses[j]);
            }
        }
        String response;
        serializeJson(doc, response);

        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // POST /api/sensors/config - Save sensor configuration (triggers restart)
    // Accepts: {"devices": [{"address": 68, "type": "SHT4x"}, ...]}
    // NOTE: Must be registered before /api/sensors to avoid prefix matching
    server.on("/api/sensors/config", HTTP_POST,
              []([[maybe_unused]] AsyncWebServerRequest *request) {
              },
              nullptr,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, [[maybe_unused]] size_t total) {
                  if (index == 0) {
                      JsonDocument doc;
                      DeserializationError error = deserializeJson(doc, data, len);

                      if (error) {
                          request->send(400, CONTENT_TYPE_JSON, JSON_RESPONSE_ERROR_INVALID_JSON);
                          return;
                      }

                      Config::SensorConfig sensorConfig;
                      char* p = sensorConfig.assignments;
                      size_t remaining = sizeof(sensorConfig.assignments);

                      JsonArray devices = doc["devices"];
                      for (size_t i = 0; i < devices.size(); i++) {
                          uint8_t addr = devices[i]["address"];
                          const char* type = devices[i]["type"];
                          if (!type) continue;

                          int written = snprintf(p, remaining, "%s%u=%s",
                                                 (p != sensorConfig.assignments) ? "," : "",
                                                 addr, type);
                          if (written > 0 && (size_t)written < remaining) {
                              p += written;
                              remaining -= written;
                          }
                      }

                      config.saveSensorConfig(sensorConfig);
                      config.requestRestart(1000);

                      request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
                  }
              }
    );

    // GET /api/sensors - Get sensor information
    server.on("/api/sensors", HTTP_GET, [this](AsyncWebServerRequest *request) {
        JsonDocument doc;
        JsonArray sensors = doc["sensors"].to<JsonArray>();

        for (size_t i = 0; i < sensorController.getSensorCount(); i++) {
            Sensor::Sensor *sensor = sensorController.getSensor(i);
            if (sensor) {
                auto sensorObj = sensors.add<JsonObject>();
                sensorObj["type"] = sensor->getType();
                auto status = sensor->getStatus();
                sensorObj["status"] = Sensor::sensorStatusLabel(status);
                sensorObj["connected"] = (status == Sensor::SensorStatus::Online);
                sensorObj["consecutive_failures"] = sensor->getConsecutiveReadFailures();
            }
        }

        // Add current sensor data
        float currentTemp = sensorController.getTemperature();
        float currentHum = sensorController.getRelativeHumidity();
        doc["current_temperature"] = sensorController.isDataValid() ? currentTemp : static_cast<float>(NAN);
        doc["current_humidity"] = sensorController.isDataValid() ? currentHum : static_cast<float>(NAN);
        doc["data_valid"] = sensorController.isDataValid();
        doc["data_timestamp"] = sensorController.getLastReadingTimestamp();

        // Add measurements array
        JsonArray measurements = doc["measurements"].to<JsonArray>();
        if (sensorController.isDataValid()) {
            for (const auto &m : sensorController.getMeasurements()) {
                auto mObj = measurements.add<JsonObject>();
                mObj["type"] = Sensor::measurementTypeLabel(m.type);
                if (auto* i = std::get_if<int32_t>(&m.value)) {
                    mObj["value"] = *i;
                } else {
                    mObj["value"] = std::get<float>(m.value);
                }
                mObj["unit"] = Sensor::measurementTypeUnit(m.type);
                mObj["sensor"] = m.sensor;
                mObj["calculated"] = m.calculated;
            }
        }

        String sensorResponse;
        serializeJson(doc, sensorResponse);
        request->send(200, CONTENT_TYPE_JSON, sensorResponse);
    });
#endif
}
