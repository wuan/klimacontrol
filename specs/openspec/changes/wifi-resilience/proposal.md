## Why

After hours of uptime the ESP32-S2 firmware was losing WiFi connectivity and failing to recover. Arduino's `WiFi.setAutoReconnect(true)` silently gives up on disconnect reasons such as `BEACON_TIMEOUT`, `ASSOC_EXPIRE`, and `AUTH_EXPIRE`, so the device stayed offline until a manual power cycle. The code also had no diagnostic logging for *why* a disconnect happened, leaving the failure mode unidentified across multiple report cycles.

## What Changes

- Register a `WiFi.onEvent()` handler that logs `STA_CONNECTED`, `GOT_IP`, `LOST_IP`, and `STA_DISCONNECTED` events. Disconnect reason codes are translated to readable strings (`BEACON_TIMEOUT`, `AUTH_EXPIRE`, `NO_AP_FOUND`, etc.) so the next failure is diagnosable from syslog alone.
- Wrap the initial connect-wait in `startSTA()` with an in-session retry loop: 3 attempts × 15 s with 3 s backoff between attempts (~54 s total). Transient AP issues at boot no longer immediately trigger the restart loop.
- Add an active-reconnect path in the network task's 1 s loop: when WiFi has been down for ≥ 30 s without recovering, force `WiFi.disconnect()` + `WiFi.reconnect()` with a 30 s minimum interval between attempts. After 6 failed forced reconnects (~3 min), restart the device to clear any deep stack state.
- Capture the disconnect timestamp from the event callback itself (rather than from the 1 s polling loop) so the "down-for" timer starts the instant the AP stops responding.

## Capabilities

### New Capabilities
- `network-wifi-resilience`: Detects WiFi disconnects, logs their reason, and recovers connectivity through layered retry strategies (in-session retry, passive auto-reconnect, active forced reconnect, eventual device restart).

### Modified Capabilities
<!-- None — no existing specs in the collection yet. -->

## Impact

- **Code**: `src/Network.cpp`, `src/Network.h`. No other modules touched.
- **APIs**: None. No new REST/MQTT endpoints, no config keys, no breaking changes.
- **Dependencies**: None. Uses existing `WiFi.h` (Arduino-ESP32) and `esp_task_wdt.h`.
- **Runtime behavior**: Boot path takes up to ~54 s before AP fallback when WiFi is unavailable (previously ~15 s). Mid-session disconnects are now actively recovered without user intervention. Syslog volume increases slightly: one log line per WiFi event.
- **Memory**: Negligible — five member variables added to `Network` (4× scalar timestamps/counters, 1× reason byte). No heap allocations.
