#include "WebServerManager.h"
#include "routes/RouteHelpers.h"

#include "Config.h"
#include "SensorController.h"

#include <cmath>

#ifdef ARDUINO
#include <ArduinoJson.h>
#endif

void WebServerManager::setupControlRoutes() {
#ifdef ARDUINO
    // POST /api/temperature/target - Set target temperature
    server.on("/api/temperature/target", HTTP_POST,
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

                      if (!doc[JSON_KEY_VALUE].is<float>()) {
                          request->send(400, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Value required"})");
                          return;
                      }

                      float targetTemp = doc[JSON_KEY_VALUE];

                      // Reject rather than clamp. Answering {"success":true}
                      // after quietly storing a different setpoint than the one
                      // requested tells the caller its request was honoured when
                      // it was not; a UI built on that reports a value the
                      // device is not holding until the next poll corrects it.
                      // isfinite() also screens NaN, which the clamp downstream
                      // would otherwise map to 30.0 — std::min(30.0f, NAN)
                      // returns 30.0f.
                      if (!std::isfinite(targetTemp) ||
                          targetTemp < Config::TARGET_TEMPERATURE_MIN_C ||
                          targetTemp > Config::TARGET_TEMPERATURE_MAX_C) {
                          request->send(
                              400, CONTENT_TYPE_JSON,
                              R"({"success":false,"error":"Value out of range 10.0-30.0"})");
                          return;
                      }

                      sensorController.setTargetTemperature(targetTemp);

                      request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
                  }
              }
    );

    // POST /api/control/enable - Enable temperature control
    server.on("/api/control/enable", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!verifyCsrfHeader(request)) {
            return;
        }
        sensorController.setControlEnabled(true);

        request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
    });

    // POST /api/control/disable - Disable temperature control
    server.on("/api/control/disable", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!verifyCsrfHeader(request)) {
            return;
        }
        sensorController.setControlEnabled(false);

        request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
    });
#endif
}
