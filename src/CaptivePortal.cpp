#include "CaptivePortal.h"

#ifdef ARDUINO
#include <Arduino.h>
#include "Log.h"
#include "support/IpFormat.h"
#endif

static constexpr const char* TAG = "portal";

void CaptivePortal::begin() {
#ifdef ARDUINO
    // DNS server redirects all requests to the AP IP (192.168.4.1)
    IPAddress apIP = WiFi.softAPIP();

    // Start DNS server on port 53
    dnsServer.start(53, "*", apIP);

    running = true;

    char apIpStr[16];
    formatIpv4(apIP, apIpStr, sizeof(apIpStr));
    ESP_LOGI(TAG, "Captive portal started, redirecting to: %s", apIpStr);
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
