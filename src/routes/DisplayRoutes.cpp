#include "WebServerManager.h"
#include "routes/RouteHelpers.h"

#include "Config.h"
#include "Network.h"
#include "display/DisplayManager.h"

#ifdef ARDUINO
#include <ArduinoJson.h>
#include "Log.h"
#endif

static const char* TAG = "route";

void WebServerManager::setupDisplayRoutes() {
#ifdef ARDUINO
    // GET /api/display - Get e-paper display configuration
    server.on("/api/display", HTTP_GET, [this](AsyncWebServerRequest *request) {
        Config::DisplayConfig displayConfig = config.loadDisplayConfig();

        JsonDocument doc;
        doc["enabled"] = displayConfig.enabled;
        doc["rotation"] = displayConfig.rotation;
        doc["interval"] = displayConfig.interval;

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // POST /api/display - Update e-paper display configuration
    server.on("/api/display", HTTP_POST,
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

                      Config::DisplayConfig displayConfig = config.loadDisplayConfig();
                      const bool wasEnabled = displayConfig.enabled;

                      if (doc["enabled"].is<bool>()) displayConfig.enabled = doc["enabled"];
                      if (doc["rotation"].is<int>()) displayConfig.rotation = doc["rotation"];
                      if (doc["interval"].is<int>()) displayConfig.interval = doc["interval"];

                      // Turning the display off blanks the panel here and now,
                      // before the restart. e-paper retains its image without
                      // power, so a disabled display has to be actively cleared
                      // or it keeps showing the last reading indefinitely.
                      //
                      // This blocks the AsyncTCP callback for a full refresh
                      // (~2.6 s), plus up to another if the Network task is
                      // mid-repaint — DisplayManager serialises the two. That
                      // is acceptable for a deliberate settings change that is
                      // about to reboot the device anyway.
                      if (wasEnabled && !displayConfig.enabled) {
                          Display::DisplayManager *display = network.getDisplay();
                          if (display != nullptr) {
                              display->disableAndClear();
                          }
                      }

                      // saveDisplayConfig() validates (rotation 0..3,
                      // interval 10..3600) before writing.
                      config.saveDisplayConfig(displayConfig);

                      ESP_LOGI(TAG, "Display config updated: enabled=%d rotation=%u interval=%u",
                               displayConfig.enabled, displayConfig.rotation,
                               displayConfig.interval);

                      // The display is brought up during setup() from the
                      // persisted config, so the change takes effect on restart
                      // — matching every other settings route.
                      config.requestRestart(1000);

                      request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
                  }
              }
    );
#endif
}
