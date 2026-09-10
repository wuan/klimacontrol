#ifndef KLIMACONTROL_NET_WIFI_STATION_H
#define KLIMACONTROL_NET_WIFI_STATION_H

#include <cstdint>

#ifdef ARDUINO
#include <WiFi.h>
#endif

#include "network/LinkMonitor.h"

namespace Config {
    class ConfigManager;
}

namespace Net {

    /**
     * Human-readable name for an esp_wifi_types disconnect reason code.
     * Arduino-only: the WIFI_REASON_* constants come from the ESP-IDF.
     */
    const char *wifiDisconnectReasonStr(uint8_t reason);

    /**
     * Station-mode WiFi: brings the radio up and associates at boot, then
     * supervises the link once per second and steps in when the driver's
     * auto-reconnect has given up. The decision logic lives in
     * `Net::LinkMonitor`; this class owns the WiFi calls and the logging.
     */
    class WifiStation {
    public:
        explicit WifiStation(Config::ConfigManager &config) : config(config) {}

        WifiStation(const WifiStation &) = delete;
        WifiStation &operator=(const WifiStation &) = delete;

        /**
         * Initialise the radio and associate with `ssid`. Blocks for up to
         * MAX_CONNECT_TRIES attempts of ~15 s each plus back-off, feeding the
         * task watchdog while it waits. Returns true when connected.
         */
        bool connect(const char *ssid, const char *password);

        bool isConnected() const;

        enum class Event : uint8_t {
            None,
            Reconnected,    // link came back after a drop; re-advertise services
            RestartRequired // supervision gave up; caller should restart the device
        };

        /**
         * Start supervising with the link up at `nowMs`. Call once, right
         * before the first `supervise()`.
         */
        void beginSupervision(uint32_t nowMs);

        /**
         * One supervision tick; call every second. Performs forced reconnects
         * itself and reports transitions the caller has to react to.
         */
        Event supervise(uint32_t nowMs);

        /**
         * Tear the association down without a deauth frame and re-associate.
         * Also the remedy `InternetHealth` asks for when the internet is gone
         * but WiFi still reports connected.
         */
        void forceReconnect();

        const LinkMonitor &monitor() const { return link; }

    private:
        static constexpr int MAX_CONNECT_TRIES = 3;
        static constexpr int MAX_WAIT_SLOTS = 30; // x 500 ms per attempt
        static constexpr int BACKOFF_MS = 3000;

        Config::ConfigManager &config;
        LinkMonitor link;
        bool eventHandlerRegistered = false; // WiFi.onEvent appends; register only once

#ifdef ARDUINO
        void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
        void applyEnergyConfig();
        void logConnectionDetails() const;
#endif
    };

} // namespace Net

#endif // KLIMACONTROL_NET_WIFI_STATION_H
