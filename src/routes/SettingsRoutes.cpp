#include "WebServerManager.h"
#include "routes/RouteHelpers.h"

#include <cstring>

#include "Config.h"
#include "SensorController.h"

#ifdef ARDUINO
#include <ArduinoJson.h>
#include "Log.h"
#endif

static const char* TAG = "route";

void WebServerManager::setupSettingsRoutes() {
#ifdef ARDUINO
    // POST /api/settings/wifi - Update WiFi credentials
    server.on("/api/settings/wifi", HTTP_POST,
              []([[maybe_unused]] AsyncWebServerRequest *request) {
              },
              nullptr,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, [[maybe_unused]] size_t total) {
                  if (index == 0) {
                      if (!verifyCsrfHeader(request)) {
                          return;
                      }

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
                      strlcpy(wifiConfig.ssid, ssid, sizeof(wifiConfig.ssid));

                      if (password != nullptr) {
                          strlcpy(wifiConfig.password, password, sizeof(wifiConfig.password));
                      } else {
                          wifiConfig.password[0] = '\0';
                      }

                      wifiConfig.configured = true;
                      config.saveWiFiConfig(wifiConfig);

                      ESP_LOGI(TAG, "WiFi credentials updated: SSID=%s", wifiConfig.ssid);

                      // Send success response and request deferred restart
                      request->send(200, CONTENT_TYPE_JSON,
                                    R"({"success":true,"message":"WiFi updated, restarting..."})");
                      config.requestRestart(1000);
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
                      if (!verifyCsrfHeader(request)) {
                          return;
                      }

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

                      // Update device name using partial update
                      config.updateDeviceName(name);

                      ESP_LOGI(TAG, "Device name updated: %s", name);

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
                      if (!verifyCsrfHeader(request)) {
                          return;
                      }

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

                      config.updateElevation(elevation);

                      ESP_LOGI(TAG, "Elevation updated: %.0f m", elevation);

                      request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
                  }
              }
    );

    // POST /api/settings/reboot - Reboot device
    server.on("/api/settings/reboot", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!verifyCsrfHeader(request)) {
            return;
        }
        ESP_LOGI(TAG, "Reboot requested");
        request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
        config.requestRestart(1000);
    });

    // POST /api/settings/factory-reset - Factory reset device
    //
    // Destructive: wipes all stored configuration. Two independent checks ensure
    // a single forged or accidental request cannot trigger it:
    //   1. verifyCsrfHeader() blocks cross-origin forgery (see RouteHelpers.h).
    //   2. An explicit "confirm=factory-reset" body parameter is the server-side
    //      confirmation step - a bare POST is rejected.
    server.on("/api/settings/factory-reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!verifyCsrfHeader(request)) {
            ESP_LOGW(TAG, "Factory reset rejected: missing/invalid CSRF header");
            return;
        }

        const AsyncWebParameter *confirm = request->getParam("confirm", true);
        if (confirm == nullptr || confirm->value() != "factory-reset") {
            ESP_LOGW(TAG, "Factory reset rejected: missing confirmation");
            request->send(400, CONTENT_TYPE_JSON,
                          R"({"success":false,"error":"Missing confirmation"})");
            return;
        }

        ESP_LOGW(TAG, "Factory reset requested");

        // Send success response first
        request->send(200, CONTENT_TYPE_JSON,
                      R"({"success":true,"message":"Factory reset complete, restarting..."})");

        // Clear all configuration
        config.reset();

        ESP_LOGW(TAG, "All settings cleared");

        // Request deferred restart
        config.requestRestart(1000);
    });

    // POST /api/restart - Restart the device
    server.on("/api/restart", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!verifyCsrfHeader(request)) {
            return;
        }
        request->send(200, CONTENT_TYPE_JSON, R"({"success":true,"message":"Restarting..."})");
        config.requestRestart(1000);
    });

    // GET /api/settings/energy - Get energy configuration
    server.on("/api/settings/energy", HTTP_GET, [this](AsyncWebServerRequest *request) {
        Config::EnergyConfig energyConfig = config.loadEnergyConfig();

        ESP_LOGD(TAG, "Loaded energy config: wifi_power=%u, wifi_sleep_mode=%u",
                 energyConfig.wifi_power, energyConfig.wifi_sleep_mode);

        JsonDocument doc;
        doc["wifi_power"] = energyConfig.wifi_power;
        doc["wifi_sleep_mode"] = energyConfig.wifi_sleep_mode;

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // POST /api/settings/energy - Update energy configuration (triggers restart)
    server.on("/api/settings/energy", HTTP_POST,
              []([[maybe_unused]] AsyncWebServerRequest *request) {
              },
              nullptr,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, [[maybe_unused]] size_t total) {
                  if (index == 0) {
                      if (!verifyCsrfHeader(request)) {
                          return;
                      }

                      JsonDocument doc;
                      DeserializationError error = deserializeJson(doc, data, len);

                      if (error) {
                          request->send(400, CONTENT_TYPE_JSON, JSON_RESPONSE_ERROR_INVALID_JSON);
                          return;
                      }

                      Config::EnergyConfig energyConfig = config.loadEnergyConfig();

                      if (doc["wifi_power"].is<int>()) {
                          uint8_t wp = doc["wifi_power"];
                          if (wp == 8 || wp == 34 || wp == 52 || wp == 68 || wp == 80) {
                              energyConfig.wifi_power = wp;
                          } else {
                              request->send(400, CONTENT_TYPE_JSON,
                                            R"({"success":false,"error":"Invalid wifi_power value"})");
                              return;
                          }
                      }

                      if (doc["wifi_sleep_mode"].is<int>()) {
                          uint8_t sm = doc["wifi_sleep_mode"];
                          if (sm <= 2) {
                              energyConfig.wifi_sleep_mode = sm;
                              ESP_LOGI(TAG, "WiFi sleep mode set to: %u", sm);
                          } else {
                              ESP_LOGW(TAG, "Invalid wifi_sleep_mode value: %u", sm);
                              request->send(400, CONTENT_TYPE_JSON,
                                            R"({"success":false,"error":"Invalid wifi_sleep_mode value"})");
                              return;
                          }
                      }

                      ESP_LOGI(TAG, "Saving energy config: power=%u, sleep_mode=%u",
                               energyConfig.wifi_power, energyConfig.wifi_sleep_mode);
                      config.saveEnergyConfig(energyConfig);
                      config.requestRestart(1000);

                      request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
                  }
              }
    );
#endif
}
