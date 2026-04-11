#include "CaptivePortal.h"

#ifdef ARDUINO
#include <Arduino.h>
#include "Log.h"
#endif

static const char* TAG = "portal";

CaptivePortal::CaptivePortal() {
}

void CaptivePortal::begin() {
#ifdef ARDUINO
    // DNS server redirects all requests to the AP IP (192.168.4.1)
    IPAddress apIP = WiFi.softAPIP();

    // Start DNS server on port 53
    dnsServer.start(53, "*", apIP);

    running = true;

    ESP_LOGI(TAG, "Captive portal started, redirecting to: %s", apIP.toString().c_str());
#endif
}

void CaptivePortal::handleClient() {
#ifdef ARDUINO
    if (running) {
        dnsServer.processNextRequest();
    }
#endif
}

void CaptivePortal::end() {
#ifdef ARDUINO
    if (running) {
        dnsServer.stop();
        running = false;
        ESP_LOGI(TAG, "Captive portal stopped");
    }
#endif
}
