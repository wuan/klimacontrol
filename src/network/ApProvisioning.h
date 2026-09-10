#ifndef KLIMACONTROL_NET_AP_PROVISIONING_H
#define KLIMACONTROL_NET_AP_PROVISIONING_H

#include <cstddef>
#include <cstdint>

#include "CaptivePortal.h"

namespace Config {
    class ConfigManager;
}

namespace Display {
    class DisplayManager;
}

class WebServerManager;

namespace Net {

    class MdnsAdvertiser;

    /**
     * Access-point mode for WiFi provisioning: brings up the SoftAP (WPA2 with
     * the password shown on the e-paper panel when one responds, open
     * otherwise), advertises it over mDNS, runs the captive-portal DNS and
     * switches the long-lived web server to its CONFIG route set.
     *
     * Two entry points, both of which end in a device restart:
     * - `runFirstBoot()` when no WiFi credentials are stored: waits
     *   indefinitely for the user to submit them.
     * - `runFallbackWindow()` after repeated STA failures: waits at most
     *   FALLBACK_TIMEOUT_MS, then bumps the failure counter so AP mode is
     *   offered again on a later boot while STA keeps being retried in
     *   between.
     */
    class ApProvisioning {
    public:
        /** 8 hex chars from `Support::computeApPassword` plus a NUL. */
        static constexpr size_t AP_PASSWORD_BUF_SIZE = 9;
        static constexpr uint32_t FALLBACK_TIMEOUT_MS = 5UL * 60 * 1000; // 5 minutes

        ApProvisioning(Config::ConfigManager &config, MdnsAdvertiser &mdns)
            : config(config), mdns(mdns) {}

        ApProvisioning(const ApProvisioning &) = delete;
        ApProvisioning &operator=(const ApProvisioning &) = delete;

        /** Non-owning; nullptr when no panel is wired (the AP is then open). */
        void setDisplay(Display::DisplayManager *display) { this->display = display; }
        /** Non-owning; main.cpp keeps the server alive for the firmware's lifetime. */
        void setWebServer(WebServerManager *webServer) { this->webServer = webServer; }

        /** Bring up the SoftAP, mDNS and the captive portal. */
        void startAP();

        [[noreturn]] void runFirstBoot();

        /** `failures` is the persisted STA failure count, for logging. */
        [[noreturn]] void runFallbackWindow(uint8_t failures);

    private:
        Config::ConfigManager &config;
        MdnsAdvertiser &mdns;
        CaptivePortal captivePortal;
        Display::DisplayManager *display = nullptr;
        WebServerManager *webServer = nullptr;

        void enterConfigMode();
        /** Poll the captive portal and feed the watchdog for one 100 ms slot. */
        void serviceSlot();
        [[noreturn]] void restart(uint32_t delayMs);
    };

} // namespace Net

#endif // KLIMACONTROL_NET_AP_PROVISIONING_H
