#include "network/WifiStation.h"

#include "Config.h"
#include "Log.h"

#ifdef ARDUINO
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#endif

static constexpr const char *const TAG = "net";

namespace Net {

#ifdef ARDUINO
    const char *wifiDisconnectReasonStr(uint8_t reason) {
        switch (reason) {
            case WIFI_REASON_UNSPECIFIED: return "UNSPECIFIED";
            case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
            case WIFI_REASON_AUTH_LEAVE: return "AUTH_LEAVE";
            case WIFI_REASON_ASSOC_EXPIRE: return "ASSOC_EXPIRE";
            case WIFI_REASON_ASSOC_TOOMANY: return "ASSOC_TOOMANY";
            case WIFI_REASON_NOT_AUTHED: return "NOT_AUTHED";
            case WIFI_REASON_NOT_ASSOCED: return "NOT_ASSOCED";
            case WIFI_REASON_ASSOC_LEAVE: return "ASSOC_LEAVE";
            case WIFI_REASON_ASSOC_NOT_AUTHED: return "ASSOC_NOT_AUTHED";
            case WIFI_REASON_BEACON_TIMEOUT: return "BEACON_TIMEOUT";
            case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";
            case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL";
            case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL";
            case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT";
            case WIFI_REASON_CONNECTION_FAIL: return "CONNECTION_FAIL";
            default: return "OTHER";
        }
    }

    void WifiStation::onWiFiEvent(WiFiEvent_t& event, WiFiEventInfo_t& info) {
        switch (event) {
            case ARDUINO_EVENT_WIFI_STA_CONNECTED:
                link.onStaConnected(millis());
                ESP_LOGI(TAG, "WiFi event: STA_CONNECTED ch=%u", info.wifi_sta_connected.channel);
                break;
            case ARDUINO_EVENT_WIFI_STA_GOT_IP:
                ESP_LOGI(TAG, "WiFi event: GOT_IP %s rssi=%d",
                         IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str(),
                         WiFi.RSSI());
                break;
            case ARDUINO_EVENT_WIFI_STA_LOST_IP:
                ESP_LOGW(TAG, "WiFi event: LOST_IP");
                break;
            case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
                const uint8_t reason = info.wifi_sta_disconnected.reason;
                link.onStaDisconnected(millis(), reason);
                ESP_LOGW(TAG, "WiFi event: DISCONNECTED reason=%u (%s)",
                         reason, wifiDisconnectReasonStr(reason));
                break;
            }
            default:
                break;
        }
    }

    void WifiStation::applyEnergyConfig() {
        const Config::EnergyConfig energyConfig = config.loadEnergyConfig();
        WiFi.setTxPower(static_cast<wifi_power_t>(energyConfig.wifi_power));

        // 0=WIFI_PS_NONE, 1=WIFI_PS_MIN_MODEM, 2=WIFI_PS_MAX_MODEM
        wifi_ps_type_t sleepMode = WIFI_PS_NONE;
        const char *sleepModeStr = "NONE";
        if (energyConfig.wifi_sleep_mode == 1) {
            sleepMode = WIFI_PS_MIN_MODEM;
            sleepModeStr = "MIN_MODEM";
        } else if (energyConfig.wifi_sleep_mode == 2) {
            sleepMode = WIFI_PS_MAX_MODEM;
            sleepModeStr = "MAX_MODEM";
        }
        WiFi.setSleep(sleepMode);

        ESP_LOGI(TAG, "WiFi config: TX Power=%d, Sleep Mode=%s", WiFi.getTxPower(), sleepModeStr);
    }

    void WifiStation::logConnectionDetails() {
        ESP_LOGI(TAG, "WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
        ESP_LOGD(TAG, "WiFi diagnostics: SSID=%s BSSID=%s Ch=%d RSSI=%d dBm MAC=%s",
                 WiFi.SSID().c_str(), WiFi.BSSIDstr().c_str(), WiFi.channel(),
                 WiFi.RSSI(), WiFi.macAddress().c_str());
        ESP_LOGD(TAG, "WiFi network: GW=%s DNS=%s TxPwr=%d Sleep=%d AutoReconn=%d",
                 WiFi.gatewayIP().toString().c_str(), WiFi.dnsIP().toString().c_str(),
                 WiFi.getTxPower(), WiFi.getSleep(), WiFi.getAutoReconnect());
    }
#endif

    bool WifiStation::connect(const char *ssid, const char *password) {
#ifdef ARDUINO
        // Clear any previous WiFi state without sending a disconnect frame.
        // disconnect(true) sends a deauth to the AP, which can cause AP-side
        // rate limiting or blocklisting when attempts fail repeatedly.
        WiFi.disconnect(false);
        vTaskDelay(50 / portTICK_PERIOD_MS);

        // WiFi.mode(WIFI_STA) is where esp_wifi_init() runs: it powers up the
        // radio and allocates the WiFi/TCP-IP task stacks, which must come from
        // one contiguous block of *internal* SRAM (never PSRAM) — the same
        // constraint that broke the OTA tasks, see OTAUpdater.h. It is also the
        // current surge that trips the brownout detector on a marginal supply.
        // Both failure modes reset the chip with no output surviving on USB CDC,
        // so log the heap state immediately before the call: this line being the
        // last one in the log pinpoints esp_wifi_init(), and the numbers say
        // whether memory was the cause (low largest-block) or not (healthy heap
        // => suspect brownout, and the next boot's "Reset reason:" confirms it).
        ESP_LOGI(TAG, "Pre-WiFi heap: internal free=%u largest=%u, total free=%u",
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 heap_caps_get_free_size(MALLOC_CAP_DEFAULT));

        WiFiClass::mode(WIFI_STA);
        WiFi.setAutoReconnect(true);

        // Guarded so a re-entry can't stack duplicate handlers (Arduino appends,
        // never replaces).
        if (!eventHandlerRegistered) {
            WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
                this->onWiFiEvent(event, info);
            });
            eventHandlerRegistered = true;
        }

        applyEnergyConfig();

        // Each attempt waits MAX_WAIT_SLOTS * 500 ms (~15 s) for association;
        // between attempts we back off briefly so the AP isn't hammered.
        for (int tryNum = 1; tryNum <= MAX_CONNECT_TRIES; tryNum++) {
            ESP_LOGI(TAG, "Connecting to WiFi %s (attempt %d/%d) ...", ssid, tryNum, MAX_CONNECT_TRIES);
            WiFi.begin(ssid, password);

            int slots = 0;
            while (WiFiClass::status() != WL_CONNECTED && slots < MAX_WAIT_SLOTS) {
                vTaskDelay(500 / portTICK_PERIOD_MS);
                esp_task_wdt_reset();
                slots++;
                if (slots % 5 == 0) {
                    ESP_LOGI(TAG, "Still connecting... (%d/%d, status=%d)",
                             slots, MAX_WAIT_SLOTS, WiFi.status());
                }
            }

            if (WiFiClass::status() == WL_CONNECTED) break;

            const uint8_t reason = link.disconnectReason();
            ESP_LOGW(TAG, "Connect attempt %d failed (last reason=%u %s), backing off %d ms",
                     tryNum, reason, wifiDisconnectReasonStr(reason), BACKOFF_MS);
            WiFi.disconnect(false);
            vTaskDelay(BACKOFF_MS / portTICK_PERIOD_MS);
            esp_task_wdt_reset();
        }

        if (WiFiClass::status() != WL_CONNECTED) {
            ESP_LOGE(TAG, "WiFi connection failed");
            return false;
        }

        link.markConnected(millis());
        logConnectionDetails();
        return true;
#else
        (void) ssid;
        (void) password;
        return false;
#endif
    }

    bool WifiStation::isConnected() {
#ifdef ARDUINO
        return WiFiClass::status() == WL_CONNECTED;
#else
        return false;
#endif
    }

    void WifiStation::beginSupervision(uint32_t nowMs) {
        link.beginSupervision(nowMs);
    }

    WifiStation::Event WifiStation::supervise(uint32_t nowMs) {
#ifdef ARDUINO
        const LinkMonitor::Verdict v = link.poll(isConnected(), nowMs);
        const uint8_t reason = link.disconnectReason();

        if (v.dropped) {
            ESP_LOGW(TAG, "WiFi disconnected (last reason=%u %s) - waiting for auto-reconnect",
                     reason, wifiDisconnectReasonStr(reason));
        }

        if (v.forceReconnect) {
            ESP_LOGW(TAG, "WiFi down %lus - forcing reconnect (attempt %u/%u, last reason=%u %s)",
                     static_cast<unsigned long>(v.downForMs / 1000), v.reconnectAttempt,
                     LinkMonitor::MAX_ACTIVE_RECONNECT_FAILURES,
                     reason, wifiDisconnectReasonStr(reason));
            forceReconnect();
        }

        switch (v.restart) {
            case LinkMonitor::Restart::ReconnectExhausted:
                ESP_LOGE(TAG, "Active reconnect exhausted - restarting");
                return Event::RestartRequired;
            case LinkMonitor::Restart::NoStableLink:
                ESP_LOGE(TAG, "No stable WiFi for %lus (flapping or stuck) - restarting",
                         static_cast<unsigned long>(v.unstableForMs / 1000));
                return Event::RestartRequired;
            case LinkMonitor::Restart::None:
                break;
        }

        if (v.reconnected) {
            ESP_LOGI(TAG, "WiFi reconnected (IP: %s)", WiFi.localIP().toString().c_str());
            return Event::Reconnected;
        }
        return Event::None;
#else
        (void) nowMs;
        return Event::None;
#endif
    }

    void WifiStation::forceReconnect() {
#ifdef ARDUINO
        WiFi.disconnect(false);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        WiFi.reconnect();
#endif
    }

} // namespace Net
