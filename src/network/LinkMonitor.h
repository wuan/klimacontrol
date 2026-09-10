#ifndef KLIMACONTROL_NET_LINK_MONITOR_H
#define KLIMACONTROL_NET_LINK_MONITOR_H

#include <atomic>
#include <cstdint>

namespace Net {

    /**
     * WiFi link supervision, as pure bookkeeping: fed with connection
     * transitions and once-per-second polls, it decides when the Network task
     * has to step in because Arduino's auto-reconnect has silently given up.
     * It never touches the WiFi driver itself, so it builds and is tested on
     * the native environment; `Net::WifiStation` owns one and acts on its
     * verdicts.
     *
     * Two independent safety nets are combined here:
     *
     * - Active reconnect. `WiFi.setAutoReconnect(true)` gives up silently on
     *   some disconnect reasons (BEACON_TIMEOUT, ASSOC_EXPIRE, AUTH_EXPIRE
     *   after long sessions). Once the link has been down for
     *   ACTIVE_RECONNECT_AFTER_MS, a forced reconnect is requested at most
     *   every ACTIVE_RECONNECT_MIN_INTERVAL_MS; after
     *   MAX_ACTIVE_RECONNECT_FAILURES attempts a restart is requested, because
     *   by then it is usually a deep stack state only a clean boot recovers.
     *
     * - Flapping-immune restart backstop. The active-reconnect path resets its
     *   attempt counter on *any* brief reconnect, so a link that flickers
     *   connected-then-dropped never trips it. A connection only counts as
     *   "stable" after it has held continuously for STABLE_CONNECT_MS; if no
     *   stable connection happens for FORCE_RESTART_NO_STABLE_MS, a restart is
     *   requested.
     *
     * Event stamps (`onStaConnected` / `onStaDisconnected`) arrive from the
     * WiFi event task, everything else runs on the Network task; the shared
     * fields are atomics.
     */
    class LinkMonitor {
    public:
        static constexpr uint32_t ACTIVE_RECONNECT_AFTER_MS = 30000;
        static constexpr uint32_t ACTIVE_RECONNECT_MIN_INTERVAL_MS = 30000;
        static constexpr uint8_t MAX_ACTIVE_RECONNECT_FAILURES = 6;
        static constexpr uint32_t STABLE_CONNECT_MS = 60000;        // 1 min up = "stable"
        static constexpr uint32_t FORCE_RESTART_NO_STABLE_MS = 600000; // 10 min without stability

        enum class Restart : uint8_t {
            None,
            ReconnectExhausted, // MAX_ACTIVE_RECONNECT_FAILURES forced reconnects did not help
            NoStableLink        // no STABLE_CONNECT_MS streak within FORCE_RESTART_NO_STABLE_MS
        };

        struct Verdict {
            bool reconnected = false;    // down -> up transition since the last poll
            bool dropped = false;        // up -> down transition since the last poll
            bool forceReconnect = false; // caller should disconnect + reconnect now
            uint8_t reconnectAttempt = 0; // 1-based attempt number when forceReconnect is set
            uint32_t downForMs = 0;      // how long the link has been down (0 when up or unknown)
            Restart restart = Restart::None;
            uint32_t unstableForMs = 0;  // time since the last stable connection (for logging)
        };

        // --- WiFi event task ---
        void onStaConnected(uint32_t nowMs) { lastConnectMs.store(nowMs); }
        void onStaDisconnected(uint32_t nowMs, uint8_t reason) {
            lastDisconnectReason.store(reason);
            lastDisconnectMs.store(nowMs);
        }

        // --- Network task ---

        /**
         * The boot-time association succeeded. Seeds the "connected since"
         * baseline if no CONNECTED event was delivered, so a later silent
         * drop still has something to reason from.
         */
        void markConnected(uint32_t nowMs);

        /**
         * Start the once-per-second supervision with the link up at `nowMs`.
         * Both stability baselines start at "now".
         */
        void beginSupervision(uint32_t nowMs);

        /**
         * One supervision poll. `isConnected` is the driver's current view.
         */
        Verdict poll(bool isConnected, uint32_t nowMs);

        uint8_t disconnectReason() const { return lastDisconnectReason.load(); }
        uint8_t activeReconnectFailures() const { return reconnectFailures; }

    private:
        std::atomic<uint8_t> lastDisconnectReason{0};
        std::atomic<uint32_t> lastConnectMs{0};
        std::atomic<uint32_t> lastDisconnectMs{0};

        uint32_t lastActiveReconnectMs = 0;
        uint8_t reconnectFailures = 0;
        bool wasConnected = true;
        uint32_t connectedSinceMs = 0;    // start of the current connected streak (0 = down)
        uint32_t lastStableConnectMs = 0; // last time the link was confirmed stable
    };

} // namespace Net

#endif // KLIMACONTROL_NET_LINK_MONITOR_H
