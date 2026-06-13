#include "WebServerManager.h"
#include "routes/RouteHelpers.h"

#include "Config.h"
#include "Network.h"
#include "MqttClient.h"

#ifdef ARDUINO
#include <ArduinoJson.h>
#endif

void WebServerManager::setupMqttRoutes() {
#ifdef ARDUINO
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

        if (mqtt) {
            JsonObject stats = doc["stats"].to<JsonObject>();
            stats["published"] = mqtt->getPublishedCount();
            stats["failed"] = mqtt->getFailedCount();
            stats["cycles"] = mqtt->getPublishCycles();
            stats["failedCycles"] = mqtt->getFailedCycles();
        }

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
                      if (!verifyCsrfHeader(request)) {
                          return;
                      }

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
#endif
}
