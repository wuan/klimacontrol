#include "WebServerManager.h"
#include "routes/RouteHelpers.h"

#ifdef ARDUINO
#include "generated/control_gz.h"
#include "generated/about_gz.h"
#include "generated/settings_gz.h"
#endif

void WebServerManager::setupPageRoutes() {
#ifdef ARDUINO
    // Serve main control page (gzip compressed)
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendGzippedResponse(request, CONTENT_TYPE_HTML, CONTROL_GZ, CONTROL_GZ_LEN);
    });

    // GET /about - About page
    server.on("/about", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendGzippedResponse(request, CONTENT_TYPE_HTML, ABOUT_GZ, ABOUT_GZ_LEN);
    });

    // GET /settings - Settings page
    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendGzippedResponse(request, CONTENT_TYPE_HTML, SETTINGS_GZ, SETTINGS_GZ_LEN);
    });
#endif
}
