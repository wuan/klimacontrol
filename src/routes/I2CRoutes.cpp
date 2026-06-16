#include "WebServerManager.h"
#include "routes/RouteHelpers.h"

#include "I2CScanner.h"

#ifdef ARDUINO
#include <ArduinoJson.h>
#endif

void WebServerManager::setupI2CRoutes() {
#ifdef ARDUINO
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
#endif
}
