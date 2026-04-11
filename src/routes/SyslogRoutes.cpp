#include "WebServerManager.h"
#include "routes/RouteHelpers.h"

#include "Config.h"
#include "SyslogOutput.h"

#ifdef ARDUINO
#include <ArduinoJson.h>
#include "Log.h"
#endif

static const char* TAG = "route";

void WebServerManager::setupSyslogRoutes() {
#ifdef ARDUINO
    // GET /api/syslog - Get syslog configuration
    server.on("/api/syslog", HTTP_GET, [this](AsyncWebServerRequest *request) {
        Config::SyslogConfig syslogConfig = config.loadSyslogConfig();

        JsonDocument doc;
        doc["enabled"] = syslogConfig.enabled;
        doc["host"] = syslogConfig.host;
        doc["port"] = syslogConfig.port;
        doc["active"] = SyslogOutput::isActive();

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // POST /api/syslog - Update syslog configuration
    server.on("/api/syslog", HTTP_POST,
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

                      Config::SyslogConfig syslogConfig = config.loadSyslogConfig();

                      if (doc["enabled"].is<bool>()) syslogConfig.enabled = doc["enabled"];
                      if (doc["host"].is<const char*>()) strlcpy(syslogConfig.host, doc["host"] | "", sizeof(syslogConfig.host));
                      if (doc["port"].is<int>()) syslogConfig.port = doc["port"];

                      config.saveSyslogConfig(syslogConfig);
                      SyslogOutput::setConfig(syslogConfig);

                      ESP_LOGI(TAG, "Syslog config updated: enabled=%d host=%s port=%u",
                               syslogConfig.enabled, syslogConfig.host, syslogConfig.port);

                      request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
                  }
              }
    );
#endif
}
