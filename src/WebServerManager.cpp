#include "WebServerManager.h"

#include <cstdio>
#include <cstring>

#include "Config.h"
#include "Network.h"
#include "SensorController.h"
#include "DeviceId.h"
#include "OTAUpdater.h"
#include "Constants.h"
#include "OTAConfig.h"
#include "TimerScheduler.h"
#include "TouchController.h"
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
#include "generated/timers_gz.h"
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
static const char* JSON_RESPONSE_ERROR_INVALID_JSON = "{\"success\":false,\"error\":\"Invalid JSON\"}";
static const char* JSON_RESPONSE_ERROR_QUEUE_FULL = "{\"success\":false,\"error\":\"Queue full\"}";

// API Paths
static const char* API_PATH_WIFI = "/api/wifi";
static const char* API_PATH_STATUS = "/api/status";
static const char* API_PATH_LAYOUT = "/api/layout";
static const char* API_PATH_TIMERS = "/api/timers";
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
        StaticJsonDocument<Config::JSON_DOC_LARGE> doc;

        // Device info
        Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
        doc["device_id"] = deviceConfig.device_id;
        doc["device_name"] = deviceConfig.device_name;
        doc["firmware_version"] = FIRMWARE_VERSION;

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
            if (!isnan(relativeHumidity)) doc["relativeHumidity"] = relativeHumidity;
            float dewPoint = sensorController.getDewPoint();
            if (!isnan(dewPoint)) doc["dewPoint"] = dewPoint;
            int32_t vocIndex = sensorController.getVocIndex();
            if (vocIndex >= 0) doc["vocIndex"] = vocIndex;
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

    // GET /api/sensors - Get sensor information
    server.on("/api/sensors", HTTP_GET, [this](AsyncWebServerRequest *request) {
        StaticJsonDocument<Config::JSON_DOC_LARGE> doc;
        JsonArray sensors = doc.createNestedArray("sensors");

        for (size_t i = 0; i < sensorController.getSensorCount(); i++) {
            Sensor::Sensor *sensor = sensorController.getSensor(i);
            if (sensor) {
                JsonObject sensorObj = sensors.createNestedObject();
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
        JsonArray measurements = doc.createNestedArray("measurements");
        if (sensorController.isDataValid()) {
            for (const auto &m : sensorController.getMeasurements()) {
                JsonObject mObj = measurements.createNestedObject();
                mObj["type"] = m.type;
                if (auto* i = std::get_if<int32_t>(&m.value)) {
                    mObj["value"] = *i;
                } else {
                    mObj["value"] = std::get<float>(m.value);
                }
                mObj["unit"] = m.unit;
                mObj["sensor"] = m.sensor;
                mObj["calculated"] = m.calculated;
            }
        }

        String sensorResponse;
        serializeJson(doc, sensorResponse);
        request->send(200, CONTENT_TYPE_JSON, sensorResponse);
    });

    // POST /api/temperature/target - Set target temperature
    server.on("/api/temperature/target", HTTP_POST,
              []([[maybe_unused]] AsyncWebServerRequest *request) {
              },
              nullptr,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, [[maybe_unused]] size_t total) {
                  if (index == 0) {
                      StaticJsonDocument<Config::JSON_DOC_TINY> doc;
                      DeserializationError error = deserializeJson(doc, data, len);

                      if (error) {
                          request->send(400, CONTENT_TYPE_JSON, JSON_RESPONSE_ERROR_INVALID_JSON);
                          return;
                      }

                      if (!doc.containsKey(JSON_KEY_VALUE)) {
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
                      StaticJsonDocument<Config::JSON_DOC_SMALL> doc;

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

                      Serial.printf("WiFi credentials updated: SSID=%s\n", wifiConfig.ssid);

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
                      StaticJsonDocument<Config::JSON_DOC_SMALL> doc;
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

                      Serial.printf("Device name updated: %s\n", deviceConfig.device_name);

                      // Send success response (no restart needed)
                      request->send(200, CONTENT_TYPE_JSON, "{\"success\":true}");
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
                      StaticJsonDocument<Config::JSON_DOC_SMALL> doc;
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
        StaticJsonDocument<Config::JSON_DOC_LARGE> doc;

        // Device info
        Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
        doc["device_id"] = deviceConfig.device_id;

        // Sensor statistics
        JsonObject statsJson = doc.createNestedObject("stats");
        statsJson["sensor_count"] = sensorController.getSensorCount();
        statsJson["has_connected_sensors"] = sensorController.hasConnectedSensors();
        statsJson["time_since_last_reading"] = sensorController.getTimeSinceLastReading();

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

    // GET /api/timers - List all timers with remaining time
    server.on(API_PATH_TIMERS, HTTP_GET, [this](AsyncWebServerRequest *request) {
        TimerScheduler *scheduler = network.getTimerScheduler();
        if (!scheduler) {
            request->send(503, CONTENT_TYPE_JSON, R"({"success":false,"error":"Timer scheduler not available"})");
            return;
        }

        uint32_t currentEpoch = network.getCurrentEpoch();
        const Config::TimersConfig &timersConfig = scheduler->getTimersConfig();

        StaticJsonDocument<Config::JSON_DOC_LARGE> doc;
        doc["timezone_offset_hours"] = timersConfig.timezone_offset_hours;
        doc["current_epoch"] = currentEpoch;
        
        // Include local time as seconds since midnight for UI convenience
        if (currentEpoch != 0) {
            doc["local_seconds_since_midnight"] = scheduler->getSecondsSinceMidnight(currentEpoch);
        } else {
            doc["local_seconds_since_midnight"] = 0;
        }

        JsonArray timers = doc.createNestedArray("timers");
        for (uint8_t i = 0; i < Config::TimersConfig::MAX_TIMERS; i++) {
            const Config::TimerEntry &timer = timersConfig.timers[i];
            JsonObject timerObj = timers.createNestedObject();
            timerObj["index"] = i;
            timerObj["enabled"] = timer.enabled;

            if (timer.enabled) {
                timerObj["type"] = static_cast<int>(timer.type);
                timerObj["action"] = static_cast<int>(timer.action);
                timerObj["preset_index"] = timer.preset_index;
                timerObj["target_time"] = timer.target_time;
                timerObj["duration_seconds"] = timer.duration_seconds;
                timerObj["remaining_seconds"] = scheduler->getRemainingSeconds(i, currentEpoch);

                // Add type name for UI convenience
                switch (timer.type) {
                    case Config::TimerType::COUNTDOWN:
                        timerObj["type_name"] = "countdown";
                        break;
                    case Config::TimerType::ALARM_DAILY:
                        timerObj["type_name"] = "alarm_daily";
                        break;
                }
            }
        }

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // POST /api/timers/countdown - Set a countdown timer
    server.on("/api/timers/countdown", HTTP_POST,
              []([[maybe_unused]] AsyncWebServerRequest *request) {
              },
              nullptr,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
                     [[maybe_unused]] size_t total) {
                  if (index == 0) {
                      TimerScheduler *scheduler = network.getTimerScheduler();
                      if (!scheduler) {
                          request->send(503, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Timer scheduler not available"})");
                          return;
                      }

                      StaticJsonDocument<Config::JSON_DOC_SMALL> doc;
                      DeserializationError error = deserializeJson(doc, data, len);

                      if (error) {
                          request->send(400, CONTENT_TYPE_JSON, JSON_RESPONSE_ERROR_INVALID_JSON);
                          return;
                      }

                      // Required: duration in seconds
                      if (!doc.containsKey("duration")) {
                          request->send(400, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Duration required"})");
                          return;
                      }

                      uint32_t duration = doc["duration"];
                      if (duration == 0 || duration > 86400 * 7) { // Max 7 days
                          request->send(400, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Invalid duration"})");
                          return;
                      }

                      // Optional: index (defaults to first available slot)
                      int timerIndex = doc[JSON_KEY_INDEX] | -1;
                      if (timerIndex == -1) {
                          // Find first available slot
                          const Config::TimersConfig &timersConfig = scheduler->getTimersConfig();
                          for (uint8_t i = 0; i < Config::TimersConfig::MAX_TIMERS; i++) {
                              if (!timersConfig.timers[i].enabled) {
                                  timerIndex = i;
                                  break;
                              }
                          }
                          if (timerIndex == -1) {
                              request->send(400, CONTENT_TYPE_JSON,
                                            R"({"success":false,"error":"All timer slots are full"})");
                              return;
                          }
                      }

                      // Optional: action (defaults to TURN_OFF)
                      Config::TimerAction action = Config::TimerAction::DISABLE_CONTROL;

                      // Optional: preset_index (only used if action is LOAD_PRESET)
                      uint8_t presetIndex = doc["preset_index"] | 0;

                      uint32_t currentEpoch = network.getCurrentEpoch();
                      if (scheduler->setCountdown(timerIndex, duration, action, presetIndex, currentEpoch)) {
                          StaticJsonDocument<Config::JSON_DOC_TINY> responseDoc;
                          responseDoc["success"] = true;
                          responseDoc["index"] = timerIndex;
                          responseDoc["remaining_seconds"] = duration;

                          String response;
                          serializeJson(responseDoc, response);
                          request->send(200, CONTENT_TYPE_JSON, response);
                      } else {
                          request->send(500, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Failed to set timer"})");
                      }
                  }
              }
    );

    // POST /api/timers/alarm - Set a daily recurring alarm
    server.on("/api/timers/alarm", HTTP_POST,
              []([[maybe_unused]] AsyncWebServerRequest *request) {
              },
              nullptr,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
                     [[maybe_unused]] size_t total) {
                  if (index == 0) {
                      TimerScheduler *scheduler = network.getTimerScheduler();
                      if (!scheduler) {
                          request->send(503, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Timer scheduler not available"})");
                          return;
                      }

                      StaticJsonDocument<Config::JSON_DOC_SMALL> doc;
                      DeserializationError error = deserializeJson(doc, data, len);

                      if (error) {
                          request->send(400, CONTENT_TYPE_JSON, JSON_RESPONSE_ERROR_INVALID_JSON);
                          return;
                      }

                      // Required: hour and minute for the alarm time
                      if (!doc.containsKey("hour") || !doc.containsKey("minute")) {
                          request->send(400, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Hour and minute required"})");
                          return;
                      }

                      uint8_t hour = doc["hour"];
                      uint8_t minute = doc["minute"];
                      if (hour > 23 || minute > 59) {
                          request->send(400, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Invalid time"})");
                          return;
                      }

                      // Optional: index (defaults to first available slot)
                      int timerIndex = doc[JSON_KEY_INDEX] | -1;
                      if (timerIndex == -1) {
                          const Config::TimersConfig &timersConfig = scheduler->getTimersConfig();
                          for (uint8_t i = 0; i < Config::TimersConfig::MAX_TIMERS; i++) {
                              if (!timersConfig.timers[i].enabled) {
                                  timerIndex = i;
                                  break;
                              }
                          }
                          if (timerIndex == -1) {
                              request->send(400, CONTENT_TYPE_JSON,
                                            R"({"success":false,"error":"All timer slots are full"})");
                              return;
                          }
                      }

                      // Optional: action (defaults to TURN_OFF)
                      Config::TimerAction action = Config::TimerAction::DISABLE_CONTROL;
                      if (doc.containsKey("action")) {
                          int actionInt = doc["action"];
                          if (actionInt == 0) action = Config::TimerAction::UPDATE_SETPOINT;
                          else action = Config::TimerAction::DISABLE_CONTROL;
                      }

                      uint8_t presetIndex = doc["preset_index"] | 0;

                      uint32_t secondsSinceMidnight = hour * 3600 + minute * 60;
                      bool success = scheduler->setDailyAlarm(timerIndex, secondsSinceMidnight, action, presetIndex);

                      if (success) {
                          StaticJsonDocument<Config::JSON_DOC_TINY> responseDoc;
                          responseDoc["success"] = true;
                          responseDoc["index"] = timerIndex;

                          String response;
                          serializeJson(responseDoc, response);
                          request->send(200, CONTENT_TYPE_JSON, response);
                      } else {
                          request->send(500, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Failed to set alarm"})");
                      }
                  }
              }
    );

    // DELETE /api/timers - Cancel a timer by index
    server.on(API_PATH_TIMERS, HTTP_DELETE,
              []([[maybe_unused]] AsyncWebServerRequest *request) {
              },
              nullptr,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
                     [[maybe_unused]] size_t total) {
                  if (index == 0) {
                      TimerScheduler *scheduler = network.getTimerScheduler();
                      if (!scheduler) {
                          request->send(503, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Timer scheduler not available"})");
                          return;
                      }

                      StaticJsonDocument<Config::JSON_DOC_TINY> doc;
                      DeserializationError error = deserializeJson(doc, data, len);

                      if (error) {
                          request->send(400, CONTENT_TYPE_JSON, JSON_RESPONSE_ERROR_INVALID_JSON);
                          return;
                      }

                      if (!doc.containsKey(JSON_KEY_INDEX)) {
                          request->send(400, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Timer index required"})");
                          return;
                      }

                      int timerIndex = doc[JSON_KEY_INDEX];
                      if (timerIndex < 0 || timerIndex >= Config::TimersConfig::MAX_TIMERS) {
                          request->send(400, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Invalid timer index"})");
                          return;
                      }

                      if (scheduler->cancelTimer(timerIndex)) {
                          request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
                      } else {
                          request->send(500, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Failed to cancel timer"})");
                      }
                  }
              }
    );

    // POST /api/timers/timezone - Set timezone offset
    server.on("/api/timers/timezone", HTTP_POST,
              []([[maybe_unused]] AsyncWebServerRequest *request) {
              },
              nullptr,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
                     [[maybe_unused]] size_t total) {
                  if (index == 0) {
                      TimerScheduler *scheduler = network.getTimerScheduler();
                      if (!scheduler) {
                          request->send(503, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Timer scheduler not available"})");
                          return;
                      }

                      StaticJsonDocument<Config::JSON_DOC_TINY> doc;
                      DeserializationError error = deserializeJson(doc, data, len);

                      if (error) {
                          request->send(400, CONTENT_TYPE_JSON, JSON_RESPONSE_ERROR_INVALID_JSON);
                          return;
                      }

                      if (!doc.containsKey("offset")) {
                          request->send(400, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Timezone offset required"})");
                          return;
                      }

                      int8_t offset = doc["offset"];
                      if (offset < -12 || offset > 14) {
                          request->send(400, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Invalid timezone offset"})");
                          return;
                      }

                      scheduler->setTimezoneOffset(offset);
                      request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
                  }
              }
    );

    // GET /api/touch - Get touch configuration and current values
    server.on("/api/touch", HTTP_GET, [this](AsyncWebServerRequest *request) {
        TouchController *touch = network.getTouchController();
        if (!touch) {
            request->send(503, CONTENT_TYPE_JSON,
                          R"({"success":false,"error":"Touch controller not available"})");
            return;
        }

        StaticJsonDocument<Config::JSON_DOC_MEDIUM> doc;
        const Config::TouchConfig &touchConfig = touch->getTouchConfig();

        doc["enabled"] = touchConfig.enabled;
        doc["threshold"] = touchConfig.threshold;

        // Pin mappings
        JsonArray pins = doc.createNestedArray("pins");
        for (uint8_t i = 0; i < Config::TouchConfig::MAX_TOUCH_PINS; i++) {
            JsonObject pin = pins.createNestedObject();
            pin["index"] = i;
            pin["gpio"] = TouchController::getGpioPin(i);
            pin["action"] = (i == 0) ? "Switch Show" : (i == 1) ? "Switch Variant" : "Switch Layout";
        }

        // Current touch values for debugging/calibration
        uint32_t touchValues[Config::TouchConfig::MAX_TOUCH_PINS];
        touch->getTouchValues(touchValues);
        JsonArray values = doc.createNestedArray("values");
        for (uint8_t i = 0; i < Config::TouchConfig::MAX_TOUCH_PINS; i++) {
            values.add(touchValues[i]);
        }

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // POST /api/touch - Update touch configuration
    server.on("/api/touch", HTTP_POST,
              []([[maybe_unused]] AsyncWebServerRequest *request) {
              },
              nullptr,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
                     [[maybe_unused]] size_t total) {
                  if (index == 0) {
                      TouchController *touch = network.getTouchController();
                      if (!touch) {
                          request->send(503, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Touch controller not available"})");
                          return;
                      }

                      StaticJsonDocument<Config::JSON_DOC_SMALL> doc;
                      DeserializationError error = deserializeJson(doc, data, len);

                      if (error) {
                          request->send(400, CONTENT_TYPE_JSON, JSON_RESPONSE_ERROR_INVALID_JSON);
                          return;
                      }

                      Config::TouchConfig touchConfig = touch->getTouchConfig();

                      // Update enabled state if provided
                      if (doc.containsKey("enabled")) {
                          touchConfig.enabled = doc["enabled"];
                      }

                      // Update threshold if provided
                      if (doc.containsKey("threshold")) {
                          touchConfig.threshold = doc["threshold"];
                      }

                      touch->setTouchConfig(touchConfig);
                      request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
                  }
              }
    );

    // GET /api/ota/check - Check for firmware updates
    server.on(API_PATH_OTA_CHECK, HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<Config::JSON_DOC_LARGE> doc;

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
                      StaticJsonDocument<Config::JSON_DOC_MEDIUM> doc;
                      deserializeJson(doc, data, len);

                      String downloadUrl = doc["download_url"] | "";
                      size_t size = doc["size"] | 0;

                      if (downloadUrl.isEmpty() || size == 0) {
                          return;
                      }

                      Serial.printf("[WebServer] Starting OTA: %s (%zu bytes)\n", downloadUrl.c_str(), size);

                      // Perform OTA update (this will block)
                      bool success = OTAUpdater::performUpdate(downloadUrl, size, [](int percent, size_t bytes) {
                          Serial.printf("[OTA] Progress: %d%% (%zu bytes)\n", percent, bytes);
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
        StaticJsonDocument<Config::JSON_DOC_MEDIUM> doc;

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
        uint32_t freeHeap, minFreeHeap, psramFree;
        OTAUpdater::getMemoryInfo(freeHeap, minFreeHeap);
        doc["free_heap"] = freeHeap;
        doc["min_free_heap"] = minFreeHeap;
        doc["psram_free"] = psramFree;
        doc["ota_safe"] = OTAUpdater::hasEnoughMemory();

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // POST /api/ota/confirm - Confirm successful boot after OTA
    server.on("/api/ota/confirm", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool success = OTAUpdater::confirmBoot();

        StaticJsonDocument<Config::JSON_DOC_SMALL> doc;
        doc[JSON_KEY_SUCCESS] = success;
        doc["message"] = success ? "Boot confirmed, rollback disabled" : "Failed to confirm boot";

        String response;
        serializeJson(doc, response);
        request->send(success ? 200 : 500, CONTENT_TYPE_JSON, response);
    });

    // GET /api/i2c/scan - Scan I2C bus for devices (with possible sensor types)
    server.on("/api/i2c/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        auto devices = I2CScanner::scan();

        StaticJsonDocument<Config::JSON_DOC_MEDIUM> doc;
        JsonArray arr = doc.createNestedArray("devices");

        for (const auto& dev : devices) {
            JsonObject obj = arr.createNestedObject();
            obj["address"] = dev.address;

            char hexBuf[8];
            snprintf(hexBuf, sizeof(hexBuf), "0x%02X", dev.address);
            obj["address_hex"] = String(hexBuf);

            auto types = I2CScanner::sensorsForAddress(dev.address);
            JsonArray typesArr = obj.createNestedArray("types");
            for (const char* t : types) {
                typesArr.add(t);
            }
        }

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // GET /api/sensors/config - Get sensor configuration as devices array
    server.on("/api/sensors/config", HTTP_GET, [this](AsyncWebServerRequest *request) {
        Config::SensorConfig sensorConfig = config.loadSensorConfig();

        StaticJsonDocument<Config::JSON_DOC_MEDIUM> doc;
        JsonArray arr = doc.createNestedArray("devices");

        // Parse assignment string "44=SHT4x,77=BME680" into JSON array
        char buf[128];
        strncpy(buf, sensorConfig.assignments, sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';

        char* token = strtok(buf, ",");
        while (token) {
            char* eq = strchr(token, '=');
            if (eq) {
                *eq = '\0';
                uint8_t addr = (uint8_t)strtoul(token, nullptr, 10);
                const char* name = eq + 1;

                JsonObject obj = arr.createNestedObject();
                obj["address"] = addr;
                obj["type"] = name;
            }
            token = strtok(nullptr, ",");
        }

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // POST /api/sensors/config - Save sensor configuration (triggers restart)
    // Accepts: {"devices": [{"address": 68, "type": "SHT4x"}, ...]}
    server.on("/api/sensors/config", HTTP_POST,
              []([[maybe_unused]] AsyncWebServerRequest *request) {
              },
              nullptr,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, [[maybe_unused]] size_t total) {
                  if (index == 0) {
                      StaticJsonDocument<Config::JSON_DOC_MEDIUM> doc;
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

    // GET /api/mqtt - Get MQTT configuration
    server.on("/api/mqtt", HTTP_GET, [this](AsyncWebServerRequest *request) {
        Config::MqttConfig mqttConfig = config.loadMqttConfig();

        StaticJsonDocument<Config::JSON_DOC_SMALL> doc;
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
                      StaticJsonDocument<Config::JSON_DOC_SMALL> doc;
                      DeserializationError error = deserializeJson(doc, data, len);

                      if (error) {
                          request->send(400, CONTENT_TYPE_JSON, JSON_RESPONSE_ERROR_INVALID_JSON);
                          return;
                      }

                      Config::MqttConfig mqttConfig = config.loadMqttConfig();

                      if (doc.containsKey("enabled")) mqttConfig.enabled = doc["enabled"];
                      if (doc.containsKey("host")) strlcpy(mqttConfig.host, doc["host"] | "", sizeof(mqttConfig.host));
                      if (doc.containsKey("port")) mqttConfig.port = doc["port"];
                      if (doc.containsKey("username")) strlcpy(mqttConfig.username, doc["username"] | "", sizeof(mqttConfig.username));
                      if (doc.containsKey("password")) strlcpy(mqttConfig.password, doc["password"] | "", sizeof(mqttConfig.password));
                      if (doc.containsKey("prefix")) strlcpy(mqttConfig.prefix, doc["prefix"] | "", sizeof(mqttConfig.prefix));
                      if (doc.containsKey("interval")) mqttConfig.interval = doc["interval"];

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

    // GET /timers - Timers page
    server.on("/timers", HTTP_GET, [](AsyncWebServerRequest *request) {
        sendGzippedResponse(request, CONTENT_TYPE_HTML, TIMERS_GZ, TIMERS_GZ_LEN);
    });
#endif
}

WebServerManager::WebServerManager(Config::ConfigManager &config, Network &network, SensorController &sensor_controller)
    : config(config), network(network), sensorController(sensor_controller)
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
        StaticJsonDocument<Config::JSON_DOC_SMALL> doc;
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
        StaticJsonDocument<Config::JSON_DOC_TINY> responseDoc;
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
                                               SensorController &sensorController)
    : WebServerManager(config, network, sensorController) {
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
                                                         SensorController &sensorController)
    : WebServerManager(config, network, sensorController) {
}

void OperationalWebServerManager::setupRoutes() {
#ifdef ARDUINO
    // Setup API routes (LED control, status, etc.)
    setupAPIRoutes();

    // Add 404 handler
    server.onNotFound([](AsyncWebServerRequest *request) {
        Serial.printf("[HTTP] 404 Not Found: %s\n", request->url().c_str());
        request->send(404, "text/plain", "Not found");
    });
#endif
}
