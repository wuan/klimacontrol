#include "network/LinkMonitor.h"

namespace Net {

    void LinkMonitor::markConnected(uint32_t nowMs) {
        if (lastConnectMs.load() == 0) lastConnectMs.store(nowMs);
    }

    void LinkMonitor::beginSupervision(uint32_t nowMs) {
        wasConnected = true;
        connectedSinceMs = nowMs;
        lastStableConnectMs = nowMs;
        lastDisconnectMs.store(0);
    }

    LinkMonitor::Verdict LinkMonitor::poll(bool isConnected, uint32_t nowMs) {
        Verdict v;

        if (!wasConnected && isConnected) {
            v.reconnected = true;
            reconnectFailures = 0;
            // Time the next drop from when it happens, not from the previous
            // disconnect period.
            lastDisconnectMs.store(0);
            lastConnectMs.store(nowMs);
        } else if (wasConnected && !isConnected) {
            v.dropped = true;
            // Defensive: stamp the disconnect time from the polling path if the
            // WiFi event handler did not. Some failure modes observed in the
            // field never deliver ARDUINO_EVENT_WIFI_STA_DISCONNECTED, leaving
            // the stamp at 0 — the active-reconnect path below would then never
            // fire and the device gets stuck at WL_IDLE_STATUS.
            if (lastDisconnectMs.load() == 0) lastDisconnectMs.store(nowMs);
        }
        wasConnected = isConnected;

        if (!isConnected) {
            const uint32_t downSince = lastDisconnectMs.load();
            v.downForMs = downSince != 0 ? nowMs - downSince : 0;
            const bool due = nowMs - lastActiveReconnectMs >= ACTIVE_RECONNECT_MIN_INTERVAL_MS;
            if (v.downForMs >= ACTIVE_RECONNECT_AFTER_MS && due) {
                reconnectFailures++;
                lastActiveReconnectMs = nowMs;
                v.forceReconnect = true;
                v.reconnectAttempt = reconnectFailures;
                if (reconnectFailures >= MAX_ACTIVE_RECONNECT_FAILURES) {
                    v.restart = Restart::ReconnectExhausted;
                }
            }
        }

        if (isConnected) {
            if (connectedSinceMs == 0) connectedSinceMs = nowMs; // streak started
            if (nowMs - connectedSinceMs >= STABLE_CONNECT_MS) lastStableConnectMs = nowMs;
        } else {
            connectedSinceMs = 0; // streak broken
        }
        v.unstableForMs = nowMs - lastStableConnectMs;
        if (v.restart == Restart::None && v.unstableForMs >= FORCE_RESTART_NO_STABLE_MS) {
            v.restart = Restart::NoStableLink;
        }

        return v;
    }

} // namespace Net
