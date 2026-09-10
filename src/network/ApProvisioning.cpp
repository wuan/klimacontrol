#include "network/ApProvisioning.h"

#include "Config.h"
#include "Constants.h"
#include "Log.h"
#include "WebServerManager.h"
#include "network/MdnsAdvertiser.h"
#include "support/ApPassword.h"

#ifdef ARDUINO
#include <WiFi.h>
#include <esp_task_wdt.h>
#include "DeviceId.h"
#include "display/DisplayManager.h"
#endif

static constexpr const char *const TAG = "net";

namespace Net {

    void ApProvisioning::startAP() {
#ifdef ARDUINO
        const String deviceId = DeviceId::getDeviceId();
        const String apSsid = Constants::AP_SSID_PREFIX + deviceId;

        // Decide the AP security mode by probing for an e-paper panel.
        //   - Panel responds (manager already enabled, OR the probe passes and
        //     the subsequent panel.begin() succeeds): use WPA2-PSK and render
        //     the password on the panel via showApInfo().
        //   - Panel does not respond: fall back to an open AP. The probe (the
        //     BUSY-transition check in EPaperDisplay::probe) is what catches
        //     the no-panel case — panel.begin() alone cannot, because
        //     GxEPD2::display.init() silently succeeds when no panel is wired
        //     up. A false result on the probe is the safer failure mode: the
        //     user can configure WiFi from a phone over the open AP, where a
        //     false positive would lock the user out with no way to recover on
        //     a device with no serial cable, no case label, and no panel. After
        //     the user submits credentials they can enable the display via the
        //     web UI for the normal status display; the AP password derivation
        //     is independent of that choice.
        //
        // See change `fix-display-probe-busy-transitions` for the rationale.
        bool useWpa2 = false;
        char password[AP_PASSWORD_BUF_SIZE] = "";

        if (display.has_value()) {
            Display::DisplayManager &displayManager = display->get();
            Config::DisplayConfig apConfig{};
            if (displayManager.tryBeginForApInfo(apConfig)) {
                Support::computeApPassword(deviceId.c_str(), password, sizeof(password));
                useWpa2 = true;
                ESP_LOGI(TAG, "Display responded at AP-mode entry — using WPA2-PSK");
            } else {
                ESP_LOGW(TAG, "No display responded at AP-mode entry — AP will be open");
            }
        } else {
            ESP_LOGW(TAG, "No DisplayManager wired — AP will be open");
        }

        if (useWpa2) {
            ESP_LOGI(TAG, "Starting Access Point: SSID='%s' (WPA2-PSK)", apSsid.c_str());
            ESP_LOGI(TAG, "AP password: %s", password);

            // Bring the AP up first so we have a real IP to show. The AP IP is
            // 192.168.4.1 on ESP32 SoftAP, but we read it from the runtime to
            // stay honest if that ever changes.
            WiFi.softAP(apSsid.c_str(), password);

            // The show must come AFTER WiFi.softAP() so the IP we hand the
            // panel is the one the user actually connects to.
            const IPAddress apIp = WiFi.softAPIP();
            char ipStr[16];
            snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u", apIp[0], apIp[1], apIp[2], apIp[3]);
            display->get().showApInfo(apSsid.c_str(), password, ipStr);
            display->get().endApInfo();
        } else {
            ESP_LOGI(TAG, "Starting Access Point: SSID='%s' (open — no display detected)", apSsid.c_str());
            WiFi.softAP(apSsid.c_str());
        }

        ESP_LOGI(TAG, "AP IP address: %s", WiFi.softAPIP().toString().c_str());

        mdns.advertise();

        // Redirect all DNS to this device so phones open the config page.
        captivePortal.begin();
#endif
    }

    void ApProvisioning::enterConfigMode() {
        startAP();

        // Switch the long-lived web server to CONFIG mode (WiFi setup + captive
        // portal routes). The same instance is reused; nothing is re-allocated.
        if (webServer.has_value()) {
            webServer->get().setMode(WebServerMode::CONFIG);
        } else {
            ESP_LOGE(TAG, "webServer not wired up — bug in main.cpp ordering");
        }
    }

    void ApProvisioning::serviceSlot() {
#ifdef ARDUINO
        // The Network task is subscribed to the task watchdog, so every wait
        // loop must feed it — otherwise the 30 s panic WDT reboots the device
        // while the user is still entering credentials in the captive portal.
        esp_task_wdt_reset();
        captivePortal.handleClient();
        vTaskDelay(100 / portTICK_PERIOD_MS);
#endif
    }

    void ApProvisioning::restart(uint32_t delayMs) {
#ifdef ARDUINO
        vTaskDelay(delayMs / portTICK_PERIOD_MS);
        ESP.restart();
#else
        (void) delayMs;
#endif
        for (;;) {}
    }

    void ApProvisioning::runFirstBoot() {
        enterConfigMode();

        while (!config.isConfigured()) {
            serviceSlot();
        }
        ESP_LOGI(TAG, "Configuration received");

        // The user has provided new credentials; start their failure count fresh.
        config.resetConnectionFailures();

#ifdef ARDUINO
        // Clear the AP info (SSID + password + IP) off the panel so it does not
        // persist across the restart into STA mode — important when the normal
        // status display is disabled and nothing else would overwrite it.
        // clear() rather than disableAndClear() preserves the user's
        // DisplayConfig preference for the next boot.
        if (display.has_value() && display->get().isEnabled()) {
            ESP_LOGI(TAG, "Clearing e-paper display before restart");
            display->get().clear();
        }

        vTaskDelay(5000 / portTICK_PERIOD_MS);
#endif

        ESP_LOGI(TAG, "Stopping captive portal");
        captivePortal.end();

        ESP_LOGI(TAG, "Scheduling restart");
        config.requestRestart(1000);

        // Stay here until the main loop restarts us, feeding the watchdog so we
        // don't trip a panic reset before the scheduled restart fires.
        for (;;) {
#ifdef ARDUINO
            esp_task_wdt_reset();
            vTaskDelay(1000 / portTICK_PERIOD_MS);
#endif
        }
    }

    void ApProvisioning::runFallbackWindow(uint8_t failures) {
        ESP_LOGW(TAG, "Multiple connection failures (%u) - opening AP for %lu s for reconfiguration",
                 failures, static_cast<unsigned long>(FALLBACK_TIMEOUT_MS / 1000));

        enterConfigMode();

#ifdef ARDUINO
        const uint32_t apStart = millis();
        while (!config.isRestartPending() && millis() - apStart < FALLBACK_TIMEOUT_MS) {
            serviceSlot();
        }
#endif

        captivePortal.end();
        // Drop the AP routes from the long-lived server so the device can be
        // restarted cleanly. We do NOT destroy the server — the singleton stays
        // alive for the next boot, see spec `memory-management` → "Long-lived
        // singletons are constructed once". `end()` stops the listening socket
        // so a request that arrives during the restart window is rejected.
        if (webServer.has_value()) {
            webServer->get().end();
        }

        if (config.isRestartPending()) {
            ESP_LOGI(TAG, "New configuration received - resetting failure count and restarting...");
            config.resetConnectionFailures();

#ifdef ARDUINO
            // Clear the AP info off the panel before restart (mirrors
            // runFirstBoot). The cold-boot setupDisplay() path will not
            // overwrite it unless DisplayConfig.enabled is true.
            if (display.has_value()) {
                ESP_LOGI(TAG, "Clearing e-paper display before restart");
                display->get().clear();
            }
#endif
            restart(1000);
        }

        // Timed out: bump the counter (persisted to NVS by the config manager)
        // so the device enters AP mode again on a later boot, giving the user
        // another opportunity to reconfigure.
        const uint8_t newFailures = config.incrementConnectionFailures();
        ESP_LOGW(TAG, "AP fallback timed out (total failures: %u) - restarting to retry AP mode...",
                 newFailures);
        restart(1000);
    }

} // namespace Net
