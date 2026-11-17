#include "WebServerManager.h"
#include "routes/RouteHelpers.h"

#include "Config.h"
#include "network/Network.h"
#include "display/DisplayManager.h"

#ifdef ARDUINO
#include <ArduinoJson.h>
#include <cmath>
#include "Log.h"
#endif

static constexpr const char* const TAG = "route";

void WebServerManager::setupDisplayRoutes() {
#ifdef ARDUINO
    // GET /api/display - Get e-paper display configuration
    server.on("/api/display", HTTP_GET, [this](AsyncWebServerRequest* request) {
        Config::DisplayConfig displayConfig = config.loadDisplayConfig();

        JsonDocument doc;
        doc["enabled"] = displayConfig.enabled;
        doc["rotation"] = displayConfig.rotation;
        doc["interval"] = displayConfig.interval;
        // NAN is the disabled sentinel and does not survive JSON, so it is
        // reported as null — the same encoding the POST handler accepts.
        if (std::isnan(displayConfig.warn_frost_c)) {
            doc["warn_frost_c"] = nullptr;
        } else {
            doc["warn_frost_c"] = displayConfig.warn_frost_c;
        }
        doc["warn_humidity_pct"] = displayConfig.warn_humidity_pct;
        doc["second_field"] = Config::displayFieldToken(displayConfig.second_field);
        doc["third_field"] = Config::displayFieldToken(displayConfig.third_field);

        sendJson(request, doc);
    });

    // POST /api/display - Update e-paper display configuration
    server.on(
        "/api/display", HTTP_POST, []([[maybe_unused]] AsyncWebServerRequest* request) {}, nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, [[maybe_unused]] size_t total) {
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

                // Warning thresholds. Explicit JSON null disables the frost
                // warning, while an absent field leaves the stored value
                // alone — isNull() alone cannot tell the two apart, so the
                // membership check comes first. A humidity limit of 0
                // disables the humidity warning. Out-of-range values are
                // clamped to disabled by validateDisplayConfig() inside
                // saveDisplayConfig(), never rejected — the same
                // clamp-don't-reject convention as rotation/interval.
                const JsonVariantConst frostField = doc["warn_frost_c"];
                if (frostField.is<float>()) {
                    displayConfig.warn_frost_c = frostField.as<float>();
                } else if (!frostField.isUnbound()) {
                    displayConfig.warn_frost_c = NAN; // explicit null
                }
                if (doc["warn_humidity_pct"].is<int>()) {
                    displayConfig.warn_humidity_pct = doc["warn_humidity_pct"].as<uint8_t>();
                }

                // Value fields use string tokens, never enum numbers. An absent
                // key preserves the stored selection; a present but unknown
                // token falls back to the documented default. `third_field:
                // null` means none (two-line layout). A `none` second token is
                // invalid for that slot and falls back to humidity during
                // validation.
                const JsonVariantConst secondField = doc["second_field"];
                if (!secondField.isUnbound()) {
                    Config::DisplayField parsed = Config::DEFAULT_SECOND_FIELD;
                    const char* token = secondField.as<const char*>();
                    if (token != nullptr && Config::displayFieldFromToken(token, parsed) &&
                        Config::isValidSecondField(parsed)) {
                        displayConfig.second_field = parsed;
                    } else {
                        displayConfig.second_field = Config::DEFAULT_SECOND_FIELD;
                    }
                }
                const JsonVariantConst thirdField = doc["third_field"];
                if (!thirdField.isUnbound()) {
                    Config::DisplayField parsed = Config::DEFAULT_THIRD_FIELD;
                    const char* token = thirdField.as<const char*>();
                    if (token != nullptr && Config::displayFieldFromToken(token, parsed) &&
                        Config::isValidThirdField(parsed)) {
                        displayConfig.third_field = parsed;
                    } else {
                        displayConfig.third_field = Config::DEFAULT_THIRD_FIELD;
                    }
                }

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
                    auto display = network.getDisplay();
                    if (display.has_value()) {
                        display->get().disableAndClear();
                    }
                }

                // saveDisplayConfig() validates (rotation 0..3,
                // interval 10..3600) before writing.
                config.saveDisplayConfig(displayConfig);

                ESP_LOGI(TAG,
                         "Display config updated: enabled=%d rotation=%u interval=%u frost=%s hum=%u second=%s "
                         "third=%s",
                         displayConfig.enabled, displayConfig.rotation, displayConfig.interval,
                         std::isnan(displayConfig.warn_frost_c) ? "off" : "on", displayConfig.warn_humidity_pct,
                         Config::displayFieldToken(displayConfig.second_field),
                         Config::displayFieldToken(displayConfig.third_field));

                // The display is brought up during setup() from the
                // persisted config, so the change takes effect on restart
                // — matching every other settings route.
                config.requestRestart(1000);

                request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
            }
        });
#endif
}
