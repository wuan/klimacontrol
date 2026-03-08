#include "WebServerManager.h"

#include <cstdio>
#include <cstring>

#include "Config.h"
#include "Network.h"
#include "SensorController.h"
#include "task/SensorMonitor.h"
#include "DeviceId.h"
#include "OTAUpdater.h"
#include "Constants.h"
#include "OTAConfig.h"
#include "MqttClient.h"
#include "I2CScanner.h"

#ifdef ARDUINO
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
// Include compressed web content
#include "generated/config_gz.h"
#include "generated/control_gz.h"
#include "generated/about_gz.h"
#include "generated/settings_gz.h"
#include "generated/common_gz.h"
#include "generated/favicon_gz.h"

// Content type constants
static const char* CONTENT_TYPE_HTML = "text/html";
static const char* CONTENT_TYPE_CSS = "text/css";
static const char* CONTENT_TYPE_SVG = "image/svg+xml";
static const char* CONTENT_TYPE_JSON = "application/json";

// JSON Key constants
static const char* JSON_KEY_SUCCESS = "success";
static const char* JSON_KEY_ERROR = "error";
static const char* JSON_KEY_VALUE = "value";
static const char* JSON_KEY_NAME = "name";
static const char* JSON_KEY_INDEX = "index";

// Common JSON Responses
static const char* JSON_RESPONSE_SUCCESS = "{\"success\":true}";
static const char* JSON_RESPONSE_ERROR_INVALID_JSON = R"({"success":false,"error":"Invalid JSON"})";
static const char* JSON_RESPONSE_ERROR_QUEUE_FULL = R"({"success":false,"error":"Queue full"})";

// API Paths
static const char* API_PATH_WIFI = "/api/wifi";
static const char* API_PATH_STATUS = "/api/status";
static const char* API_PATH_RESTART = "/api/restart";
static const char* API_PATH_RESET = "/api/reset";
static const char* API_PATH_OTA_CHECK = "/api/ota/check";
static const char* API_PATH_OTA_UPDATE = "/api/ota/update";
#endif

// Helper functions to send gzipped responses
#ifdef ARDUINO
static void sendGzippedResponse(AsyncWebServerRequest *request, const char *contentType, const uint8_t *data, size_t len) {
    AsyncWebServerResponse *response = request->beginResponse(200, contentType, data, len);
    response->addHeader("Content-Encoding", "gzip");
    response->addHeader("Cache-Control", "max-age=86400");
    request->send(response);
}
#endif

// Web source files are in data/ directory
// Run: python3 scripts/compress_web.py to regenerate compressed headers

void AccessLogger::run(AsyncWebServerRequest *request, ArMiddlewareNext next) {
    Print *_out = &Serial;
    char logBuf[128];
    const char* ip = request->client()->remoteIP().toString().c_str();
    const char* url = request->url().c_str();
    const char* method = request->methodToString();

    uint32_t elapsed = millis();
    next();
    elapsed = millis() - elapsed;

    AsyncWebServerResponse *response = request->getResponse();
    if (response) {
        snprintf(logBuf, sizeof(logBuf), "[HTTP] %s %s %s (%u ms) %u", 
                 ip, url, method, elapsed, response->code());
    } else {
        snprintf(logBuf, sizeof(logBuf), "[HTTP] %s %s %s (%u ms) (no response)", 
                 ip, url, method, elapsed);
    }
    _out->println(logBuf);
}

void WebServerManager::setupCommonRoutes() {
#ifdef ARDUINO
    // Serve common CSS (gzip compressed)
    server.on("/common.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        sendGzippedResponse(request, CONTENT_TYPE_CSS, COMMON_GZ, COMMON_GZ_LEN);
    });

    // Serve favicon (gzip compressed)
    server.on("/favicon.svg", HTTP_GET, [](AsyncWebServerRequest *request) {
        sendGzippedResponse(request, CONTENT_TYPE_SVG, FAVICON_GZ, FAVICON_GZ_LEN);
    });
#endif
}

void WebServerManager::setupConfigRoutes() {
#ifdef ARDUINO
    Serial.println("Setting up config routes...");

    setupCommonRoutes();

    // Serve WiFi config page (gzip compressed)
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        sendGzippedResponse(request, CONTENT_TYPE_HTML, CONFIG_GZ, CONFIG_GZ_LEN);
    });

    // Handle WiFi configuration POST
    server.on(API_PATH_WIFI, HTTP_POST,
              []([[maybe_unused]] AsyncWebServerRequest *request) {
                  // This callback is called after body processing
              },
              nullptr,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                  this->handleWiFiConfig(request, data, len, index, total);
              }
    );
#endif
}

void WebServerManager::setupAPIRoutes() {
#ifdef ARDUINO
    Serial.println("Setting up API routes...");

    setupCommonRoutes();

    // Serve main control page (gzip compressed)
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        sendGzippedResponse(request, CONTENT_TYPE_HTML, CONTROL_GZ, CONTROL_GZ_LEN);
    });

    // GET /api/status - Get device status
    server.on(API_PATH_STATUS, HTTP_GET, [this](AsyncWebServerRequest *request) {
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

    // GET /api/sensors/config - Get sensor configuration as devices array
    // NOTE: Must be registered before /api/sensors to avoid prefix matching
    server.on("/api/sensors/config", HTTP_GET, [this](AsyncWebServerRequest *request) {
        Config::SensorConfig sensorConfig = config.loadSensorConfig();

        JsonDocument doc;
        JsonArray arr = doc["devices"].to<JsonArray>();

        // Parse assignment string "44=SHT4x,77=BME680" into JSON array
        char buf[128];
        strncpy(buf, sensorConfig.assignments, sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';

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
                sensorObj["connected"] = sensor->isConnected();
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

    // POST /api/temperature/target - Set target temperature
    server.on("/api/temperature/target", HTTP_POST,
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

                      if (!doc[JSON_KEY_VALUE].is<float>()) {
                          request->send(400, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Value required"})");
                          return;
                      }

                      float targetTemp = doc[JSON_KEY_VALUE];
                      sensorController.setTargetTemperature(targetTemp);

                      // Save to config
                      Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
                      deviceConfig.target_temperature = targetTemp;
                      config.saveDeviceConfig(deviceConfig);

                      request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
                  }
              }
    );

    // POST /api/control/enable - Enable temperature control
    server.on("/api/control/enable", HTTP_POST, [this](AsyncWebServerRequest *request) {
        sensorController.setControlEnabled(true);
        
        // Save to config
        Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
        deviceConfig.temperature_control_enabled = true;
        config.saveDeviceConfig(deviceConfig);
        
        request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
    });

    // POST /api/control/disable - Disable temperature control
    server.on("/api/control/disable", HTTP_POST, [this](AsyncWebServerRequest *request) {
        sensorController.setControlEnabled(false);
        
        // Save to config
        Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
        deviceConfig.temperature_control_enabled = false;
        config.saveDeviceConfig(deviceConfig);
        
        request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
    });

    // POST /api/restart - Restart the device
    server.on(API_PATH_RESTART, HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, CONTENT_TYPE_JSON, R"({"success":true,"message":"Restarting..."})");
        delay(500); // Give time for response to send
        ESP.restart();
    });

    // POST /api/settings/wifi - Update WiFi credentials
    server.on("/api/settings/wifi", HTTP_POST,
              []([[maybe_unused]] AsyncWebServerRequest *request) {
              },
              nullptr,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, [[maybe_unused]] size_t total) {
                  if (index == 0) {
                      JsonDocument doc;

                      if (deserializeJson(doc, data, len)) {
                          request->send(400, CONTENT_TYPE_JSON, JSON_RESPONSE_ERROR_INVALID_JSON);
                          return;
                      }

                      const char *ssid = doc["ssid"];
                      const char *password = doc["password"];

                      if (ssid == nullptr || strlen(ssid) == 0) {
                          request->send(400, CONTENT_TYPE_JSON, R"({"success":false,"error":"SSID required"})");
                          return;
                      }

                      // Create and save WiFi config
                      Config::WiFiConfig wifiConfig;
                      strncpy(wifiConfig.ssid, ssid, sizeof(wifiConfig.ssid) - 1);
                      wifiConfig.ssid[sizeof(wifiConfig.ssid) - 1] = '\0';

                      if (password != nullptr) {
                          strncpy(wifiConfig.password, password, sizeof(wifiConfig.password) - 1);
                          wifiConfig.password[sizeof(wifiConfig.password) - 1] = '\0';
                      } else {
                          wifiConfig.password[0] = '\0';
                      }

                      wifiConfig.configured = true;
                      config.saveWiFiConfig(wifiConfig);

                      Serial.printf("WiFi credentials updated: SSID=%s\r\n", wifiConfig.ssid);

                      // Send success response and restart
                      request->send(200, CONTENT_TYPE_JSON,
                                    R"({"success":true,"message":"WiFi updated, restarting..."})");
                      delay(1000); // Give time for response to send
                      ESP.restart();
                  }
              }
    );

    // POST /api/settings/device-name - Update device name
    server.on("/api/settings/device-name", HTTP_POST,
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

                      const char *name = doc[JSON_KEY_NAME];

                      if (name == nullptr || strlen(name) == 0) {
                          request->send(400, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Device name required"})");
                          return;
                      }

                      // Load current config, update name, and save
                      Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
                      strncpy(deviceConfig.device_name, name, sizeof(deviceConfig.device_name) - 1);
                      deviceConfig.device_name[sizeof(deviceConfig.device_name) - 1] = '\0';
                      config.saveDeviceConfig(deviceConfig);

                      Serial.printf("Device name updated: %s\r\n", deviceConfig.device_name);

                      // Send success response (no restart needed)
                      request->send(200, CONTENT_TYPE_JSON, "{\"success\":true}");
                  }
              }
    );

    // POST /api/settings/elevation - Update station elevation
    server.on("/api/settings/elevation", HTTP_POST,
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

                      if (!doc["elevation"].is<float>()) {
                          request->send(400, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Elevation required"})");
                          return;
                      }

                      auto elevation = doc["elevation"].as<float>();

                      Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
                      deviceConfig.elevation = elevation;
                      config.saveDeviceConfig(deviceConfig);

                      Serial.printf("Elevation updated: %.0f m\r\n", elevation);

                      request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
                  }
              }
    );

    // POST /api/settings/device - Update device hardware settings (num_pixels, led_pin)
    server.on("/api/settings/device", HTTP_POST,
              []([[maybe_unused]] AsyncWebServerRequest *request) {
              },
              nullptr,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
                     [[maybe_unused]] size_t total) {
                  if (index == 0) {
                      JsonDocument doc;
                      DeserializationError error = deserializeJson(doc, data, len);

                      if (error) {
                          request->send(400, CONTENT_TYPE_JSON, JSON_RESPONSE_ERROR_INVALID_JSON);
                          return;
                      }

                      // Load current config
                      Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
                      bool changed = false;

                      if (!changed) {
                          request->send(400, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"No valid parameters provided"})");
                          return;
                      }

                      // Save config
                      config.saveDeviceConfig(deviceConfig);

                      // Send success response and request deferred restart
                      request->send(200, CONTENT_TYPE_JSON,
                                    R"({"success":true,"message":"Device settings updated, restarting..."})");
                      config.requestRestart(1000);
                  }
              }
    );

    // POST /api/settings/reboot - Reboot device
    server.on("/api/settings/reboot", HTTP_POST, [this](AsyncWebServerRequest *request) {
        Serial.println("Reboot requested");
        request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
        config.requestRestart(1000);
    });

    // POST /api/settings/factory-reset - Factory reset device
    server.on("/api/settings/factory-reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
        Serial.println("Factory reset requested");

        // Send success response first
        request->send(200, CONTENT_TYPE_JSON,
                      R"({"success":true,"message":"Factory reset complete, restarting..."})");

        // Clear all configuration
        config.reset();

        Serial.println("All settings cleared");

        // Request deferred restart
        config.requestRestart(1000);
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

    // GET /api/ota/check - Check for firmware updates
    server.on(API_PATH_OTA_CHECK, HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;

        FirmwareInfo info;
        bool releaseFound = OTAUpdater::checkForUpdate(OTA_GITHUB_OWNER, OTA_GITHUB_REPO, info);

        // Only report update available if release found AND version is different
        bool updateAvailable = releaseFound && (info.version != FIRMWARE_VERSION);

        if (releaseFound) {
            doc["update_available"] = updateAvailable;
            doc["current_version"] = FIRMWARE_VERSION;
            doc["latest_version"] = info.version;
            doc["release_name"] = info.name;
            doc["size_bytes"] = info.size;
            doc["download_url"] = info.downloadUrl;
            doc["release_notes"] = info.releaseNotes;
        } else {
            doc["update_available"] = false;
            doc["current_version"] = FIRMWARE_VERSION;
            doc["message"] = "No update available or failed to check";
        }

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // POST /api/ota/update - Perform OTA update
    server.on(API_PATH_OTA_UPDATE, HTTP_POST,
              [](AsyncWebServerRequest *request) {
                  // Send immediate response before starting update
                  request->send(200, CONTENT_TYPE_JSON,
                                R"({"status":"starting","message":"OTA update started"})");
              },
              nullptr,
              [this]([[maybe_unused]] AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                  if (index == 0) {
                      Serial.println("[WebServer] OTA update requested");
                  }

                  if (index + len == total) {
                      JsonDocument doc;
                      deserializeJson(doc, data, len);

                      String downloadUrl = doc["download_url"] | "";
                      size_t size = doc["size"] | 0;

                      if (downloadUrl.isEmpty() || size == 0) {
                          return;
                      }

                      Serial.printf("[WebServer] Starting OTA: %s (%zu bytes)\r\n", downloadUrl.c_str(), size);

                      // Perform OTA update (this will block)
                      bool success = OTAUpdater::performUpdate(downloadUrl, size, [](int percent, size_t bytes) {
                          Serial.printf("[OTA] Progress: %d%% (%zu bytes)\r\n", percent, bytes);
                      });

                      if (success) {
                          Serial.println("[WebServer] OTA update successful, scheduling restart...");
                          this->config.requestRestart(1000);
                      } else {
                          Serial.println("[WebServer] OTA update failed!");
                      }
                  }
              }
    );

    // GET /api/ota/status - Get OTA status
    server.on("/api/ota/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;

        doc["firmware_version"] = FIRMWARE_VERSION;
        doc["build_date"] = FIRMWARE_BUILD_DATE;
        doc["build_time"] = FIRMWARE_BUILD_TIME;

        // Partition info
        String partitionLabel;
        uint32_t partitionAddress;
        if (OTAUpdater::getRunningPartitionInfo(partitionLabel, partitionAddress)) {
            doc["partition"] = partitionLabel;
            doc["partition_address"] = partitionAddress;
        }

        // Check if running unconfirmed update
        doc["unconfirmed_update"] = OTAUpdater::hasUnconfirmedUpdate();

        // Memory info
        uint32_t freeHeap, minFreeHeap;
        OTAUpdater::getMemoryInfo(freeHeap, minFreeHeap);
        doc["free_heap"] = freeHeap;
        doc["min_free_heap"] = minFreeHeap;
        doc["ota_safe"] = OTAUpdater::hasEnoughMemory();

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // POST /api/ota/confirm - Confirm successful boot after OTA
    server.on("/api/ota/confirm", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool success = OTAUpdater::confirmBoot();

        JsonDocument doc;
        doc[JSON_KEY_SUCCESS] = success;
        doc["message"] = success ? "Boot confirmed, rollback disabled" : "Failed to confirm boot";

        String response;
        serializeJson(doc, response);
        request->send(success ? 200 : 500, CONTENT_TYPE_JSON, response);
    });

    // GET /api/i2c/scan - Scan I2C bus for devices (addresses only, types come from registry)
    server.on("/api/i2c/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        auto devices = I2CScanner::scan();

        JsonDocument doc;
        JsonArray arr = doc["devices"].to<JsonArray>();

        for (const auto& dev : devices) {
            arr.add(dev.address);
        }

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // GET /api/mqtt - Get MQTT configuration
    server.on("/api/mqtt", HTTP_GET, [this](AsyncWebServerRequest *request) {
        Config::MqttConfig mqttConfig = config.loadMqttConfig();

        JsonDocument doc;
        doc["enabled"] = mqttConfig.enabled;
        doc["host"] = mqttConfig.host;
        doc["port"] = mqttConfig.port;
        doc["username"] = mqttConfig.username;
        doc["prefix"] = mqttConfig.prefix;
        doc["interval"] = mqttConfig.interval;

        MqttClient* mqtt = network.getMqttClient();
        doc["connected"] = mqtt ? mqtt->isConnected() : false;

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // POST /api/mqtt - Update MQTT configuration
    server.on("/api/mqtt", HTTP_POST,
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

                      Config::MqttConfig mqttConfig = config.loadMqttConfig();

                      if (doc["enabled"].is<bool>()) mqttConfig.enabled = doc["enabled"];
                      if (doc["host"].is<const char*>()) strlcpy(mqttConfig.host, doc["host"] | "", sizeof(mqttConfig.host));
                      if (doc["port"].is<int>()) mqttConfig.port = doc["port"];
                      if (doc["username"].is<const char*>()) strlcpy(mqttConfig.username, doc["username"] | "", sizeof(mqttConfig.username));
                      if (doc["password"].is<const char*>()) strlcpy(mqttConfig.password, doc["password"] | "", sizeof(mqttConfig.password));
                      if (doc["prefix"].is<const char*>()) strlcpy(mqttConfig.prefix, doc["prefix"] | "", sizeof(mqttConfig.prefix));
                      if (doc["interval"].is<int>()) mqttConfig.interval = doc["interval"];

                      config.saveMqttConfig(mqttConfig);
                      network.updateMqttConfig(mqttConfig);

                      request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
                  }
              }
    );

    // GET /about - About page
    server.on("/about", HTTP_GET, [](AsyncWebServerRequest *request) {
        sendGzippedResponse(request, CONTENT_TYPE_HTML, ABOUT_GZ, ABOUT_GZ_LEN);
    });

    // GET /settings - Settings page
    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
        sendGzippedResponse(request, CONTENT_TYPE_HTML, SETTINGS_GZ, SETTINGS_GZ_LEN);
    });
#endif
}

WebServerManager::WebServerManager(Config::ConfigManager &config, Network &network, SensorController &sensor_controller, Task::SensorMonitor &sensor_monitor)
    : config(config), network(network), sensorController(sensor_controller), sensorMonitor(sensor_monitor)
#ifdef ARDUINO
      , server(80)
#endif
{
}

#ifdef ARDUINO
void WebServerManager::handleWiFiConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
                                        [[maybe_unused]] size_t total) {
    // Only process the first chunk (index == 0)
    if (index == 0) {
        // Parse JSON body
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
            request->send(400, CONTENT_TYPE_JSON, JSON_RESPONSE_ERROR_INVALID_JSON);
            return;
        }

        // Extract SSID and password
        const char *ssid = doc["ssid"];
        const char *password = doc["password"];

        if (ssid == nullptr || strlen(ssid) == 0) {
            request->send(400, CONTENT_TYPE_JSON, R"({"success":false,"error":"SSID required"})");
            return;
        }

        // Create WiFi config
        Config::WiFiConfig wifiConfig;
        strncpy(wifiConfig.ssid, ssid, sizeof(wifiConfig.ssid) - 1);
        wifiConfig.ssid[sizeof(wifiConfig.ssid) - 1] = '\0';

        if (password != nullptr) {
            strncpy(wifiConfig.password, password, sizeof(wifiConfig.password) - 1);
            wifiConfig.password[sizeof(wifiConfig.password) - 1] = '\0';
        } else {
            wifiConfig.password[0] = '\0';
        }

        wifiConfig.configured = true;

        // Save configuration
        config.saveWiFiConfig(wifiConfig);

        Serial.print("WiFi configured: SSID=");
        Serial.println(wifiConfig.ssid);

        // Generate mDNS hostname for response
        String deviceId = DeviceId::getDeviceId();
        String hostname = Constants::HOSTNAME_PREFIX + deviceId;
        hostname.toLowerCase();

        // Send success response with hostname
        JsonDocument responseDoc;
        responseDoc["success"] = true;
        responseDoc["hostname"] = hostname + ".local";

        String response;
        serializeJson(responseDoc, response);
        request->send(200, CONTENT_TYPE_JSON, response);

        // Note: The Network task will detect config.isConfigured() and restart the device
    }
}
#endif

void WebServerManager::begin() {
#ifdef ARDUINO
    Serial.println("Starting webserver...");

    // Add access logging middleware for all requests
    // server.addMiddleware(&logging);

    // Setup routes (implemented by subclass)
    setupRoutes();

    // Start server
    server.begin();

    Serial.println("Webserver started on port 80");
#endif
}

void WebServerManager::end() {
#ifdef ARDUINO
    server.end();
    Serial.println("Webserver stopped");
#endif
}

// ConfigWebServerManager implementation
ConfigWebServerManager::ConfigWebServerManager(Config::ConfigManager &config, Network &network,
                                               SensorController &sensorController, Task::SensorMonitor &sensorMonitor)
    : WebServerManager(config, network, sensorController, sensorMonitor) {
}

void ConfigWebServerManager::setupRoutes() {
#ifdef ARDUINO
    // Setup config routes only (WiFi setup, OTA)
    setupConfigRoutes();

    // Captive portal: redirect all unknown requests to root
    // This makes the captive portal work on phones/tablets
    server.onNotFound([](AsyncWebServerRequest *request) {
        // Redirect to the root page for captive portal detection
        request->redirect("/");
    });
#endif
}

// OperationalWebServerManager implementation
OperationalWebServerManager::OperationalWebServerManager(Config::ConfigManager &config, Network &network,
                                                         SensorController &sensorController, Task::SensorMonitor &sensorMonitor)
    : WebServerManager(config, network, sensorController, sensorMonitor) {
}

void OperationalWebServerManager::setupRoutes() {
#ifdef ARDUINO
    // Setup API routes (LED control, status, etc.)
    setupAPIRoutes();

    // Add 404 handler
    server.onNotFound([](AsyncWebServerRequest *request) {
        Serial.printf("[HTTP] 404 Not Found: %s\r\n", request->url().c_str());
        request->send(404, "text/plain", "Not found");
    });
#endif
}
