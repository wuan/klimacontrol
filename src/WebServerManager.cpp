#include "WebServerManager.h"
#include "routes/RouteHelpers.h"

#include "Config.h"
#include "Network.h"
#include "SensorController.h"
#include "task/SensorMonitor.h"
#include "DeviceId.h"
#include "Constants.h"

#ifdef ARDUINO
#include <ArduinoJson.h>
#include "Log.h"
#include "generated/config_gz.h"
#include "generated/common_gz.h"
#include "generated/favicon_gz.h"
#endif

static const char* TAG = "http";

// Web source files are in data/ directory
// Run: python3 scripts/compress_web.py to regenerate compressed headers

void AccessLogger::run(AsyncWebServerRequest *request, ArMiddlewareNext next) {
    uint32_t elapsed = millis();
    next();
    elapsed = millis() - elapsed;

    AsyncWebServerResponse *response = request->getResponse();
    if (response) {
        ESP_LOGI(TAG, "%s %s %s (%u ms) %u",
                 request->client()->remoteIP().toString().c_str(),
                 request->url().c_str(), request->methodToString(),
                 elapsed, response->code());
    } else {
        ESP_LOGI(TAG, "%s %s %s (%u ms) (no response)",
                 request->client()->remoteIP().toString().c_str(),
                 request->url().c_str(), request->methodToString(), elapsed);
    }
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
    ESP_LOGI(TAG, "Setting up config routes...");

    setupCommonRoutes();

    // Serve WiFi config page (gzip compressed)
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        sendGzippedResponse(request, CONTENT_TYPE_HTML, CONFIG_GZ, CONFIG_GZ_LEN);
    });

    // Handle WiFi configuration POST
    server.on("/api/wifi", HTTP_POST,
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
    ESP_LOGI(TAG, "Setting up API routes...");

    setupCommonRoutes();
    setupPageRoutes();
    setupStatusRoutes();
    setupSensorRoutes();
    setupControlRoutes();
    setupSettingsRoutes();
    setupOTARoutes();
    setupMqttRoutes();
    setupSyslogRoutes();
    setupI2CRoutes();
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
        if (!verifyCsrfHeader(request)) {
            return;
        }

        // Parse JSON body on the stack — no heap allocation for the document.
        // 512 bytes is the spec ceiling (see http-api → "Request handler
        // allocation discipline") and is generous for an SSID + password pair.
        StaticJsonDocument<512> doc;
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
        strlcpy(wifiConfig.ssid, ssid, sizeof(wifiConfig.ssid));

        if (password != nullptr) {
            strlcpy(wifiConfig.password, password, sizeof(wifiConfig.password));
        } else {
            wifiConfig.password[0] = '\0';
        }

        wifiConfig.configured = true;

        // Save configuration
        config.saveWiFiConfig(wifiConfig);

        ESP_LOGI(TAG, "WiFi configured: SSID=%s", wifiConfig.ssid);

        // Generate mDNS hostname for response
        String deviceId = DeviceId::getDeviceId();
        String hostname = Constants::HOSTNAME_PREFIX + deviceId;
        hostname.toLowerCase();

        // Build the success response on the stack — no heap allocation for the
        // document. The serialized String is what we hand to AsyncWebServer.
        StaticJsonDocument<128> responseDoc;
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
    // No-op: the server is started by `setMode()` which is the canonical
    // entry point in the new lifecycle (construct once, setMode to start,
    // setMode again to switch, end to stop). Calling `begin()` after a
    // `setMode()` would re-register the same routes on top of the existing
    // route table, so we deliberately do nothing here.
}

void WebServerManager::end() {
#ifdef ARDUINO
    server.end();
    ESP_LOGI(TAG, "Webserver stopped");
#endif
}

void WebServerManager::setMode(WebServerMode mode) {
#ifdef ARDUINO
    if (mode == currentMode && currentMode != WebServerMode::NONE) {
        // Idempotent: same mode is already active, don't disturb the route table.
        return;
    }

    // Stop the server while we rebuild the route table so an incoming
    // request cannot see a partial set of routes mid-swap.
    server.end();

    // Drop all previously-registered handlers and the not-found hook.
    clearRoutes();

    if (mode == WebServerMode::CONFIG) {
        ESP_LOGI(TAG, "Switching webserver to CONFIG mode");
        setupConfigRoutes();
        // Captive portal: redirect all unknown requests to root so phones/tablets
        // detect the configuration portal.
        server.onNotFound([](AsyncWebServerRequest *request) {
            request->redirect("/");
        });
    } else if (mode == WebServerMode::OPERATIONAL) {
        ESP_LOGI(TAG, "Switching webserver to OPERATIONAL mode");
        setupAPIRoutes();
        server.onNotFound([](AsyncWebServerRequest *request) {
            ESP_LOGD(TAG, "404 Not Found: %s", request->url().c_str());
            request->send(404, "text/plain", "Not found");
        });
    }
    // WebServerMode::NONE: leave the server stopped with no routes.

    currentMode = mode;
    server.begin();
    ESP_LOGI(TAG, "Webserver started on port 80 (mode=%u)", static_cast<unsigned>(mode));
#endif
}

void WebServerManager::clearRoutes() {
#ifdef ARDUINO
    // AsyncWebServer's reset() drops the entire handler list and the
    // not-found hook, returning the object to the same state as a
    // freshly-constructed server. This is the only supported way to
    // clear handlers — there is no per-handler remove API.
    server.reset();
#endif
}
