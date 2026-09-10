#ifndef KLIMACONTROL_NET_INTERNET_HEALTH_H
#define KLIMACONTROL_NET_INTERNET_HEALTH_H

#include <atomic>
#include <cstdint>

namespace Net {

    /**
     * Counts internet-level failures reported by MQTT, NTP and OTA and decides
     * when the Network task should force a WiFi reconnect. A failure here means
     * "WiFi says connected, but nothing beyond the router answers"; the
     * remedy is to re-associate, which usually clears a stale DHCP lease or a
     * stuck lwIP state.
     *
     * reportFailure()/reportSuccess() may be called from other tasks (the OTA
     * worker), so the counter is atomic. The window bookkeeping is only
     * touched from the Network task.
     */
    class InternetHealth {
    public:
        static constexpr uint32_t FAILURE_THRESHOLD = 5;
        static constexpr uint32_t FAILURE_WINDOW_MS = 60000;

        void reportFailure();
        void reportSuccess();

        /**
         * Forget everything recorded so far and restart the action window at
         * `nowMs`. Used after an OTA download ends: failures recorded while
         * the link was saturated describe the download, not the internet.
         */
        void reset(uint32_t nowMs);

        /**
         * True when the failure count has reached the threshold and at least
         * FAILURE_WINDOW_MS have passed since the last forced reconnect. A
         * true result stamps the window, so repeated calls within the window
         * return false even if failures keep accumulating.
         */
        bool shouldForceReconnect(uint32_t nowMs);

        uint32_t failures() const { return failureCount.load(); }

    private:
        std::atomic<uint32_t> failureCount{0};
        uint32_t lastActionMs = 0;
    };

} // namespace Net

#endif // KLIMACONTROL_NET_INTERNET_HEALTH_H
