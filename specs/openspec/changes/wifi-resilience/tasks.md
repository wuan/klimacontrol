## 1. Diagnostics

- [x] 1.1 Add `wifiDisconnectReasonStr()` helper in an anonymous namespace in `src/Network.cpp` covering the reason codes listed in the spec, with `OTHER` fallback that preserves the integer value at the caller.
- [x] 1.2 Add `void onWiFiEvent(WiFiEvent_t, WiFiEventInfo_t)` member in `src/Network.h` (guarded by `#ifdef ARDUINO`) and implement in `src/Network.cpp` to log `STA_CONNECTED`, `GOT_IP`, `LOST_IP`, and `STA_DISCONNECTED` events.
- [x] 1.3 In `startSTA()`, register the handler via `WiFi.onEvent([this](...) { this->onWiFiEvent(...); })` before the first `WiFi.begin()`.

## 2. Boot-time retry

- [x] 2.1 Wrap the existing 15 s connect-wait in `startSTA()` with a 3-iteration outer loop. Use named constants (`MAX_CONNECT_TRIES=3`, `MAX_WAIT_SLOTS=30`, `BACKOFF_MS=3000`).
- [x] 2.2 Between attempts, call `WiFi.disconnect(false)` and `vTaskDelay(BACKOFF_MS / portTICK_PERIOD_MS)`.
- [x] 2.3 Call `esp_task_wdt_reset()` inside the wait loop so the watchdog does not fire during the retry window.
- [x] 2.4 Log per-attempt status with both attempt number and last known disconnect reason (`tryNum`, `MAX_CONNECT_TRIES`, `lastWifiDisconnectReason`).

## 3. Active reconnect

- [x] 3.1 Add member variables to `Network`: `volatile uint8_t lastWifiDisconnectReason`, `volatile unsigned long lastWifiConnectMs`, `volatile unsigned long lastWifiDisconnectMs`, `unsigned long lastActiveReconnectMs`, `uint8_t activeReconnectFailures`.
- [x] 3.2 In `onWiFiEvent()`, stamp `lastWifiDisconnectMs = millis()` on `ARDUINO_EVENT_WIFI_STA_DISCONNECTED` and `lastWifiConnectMs = millis()` on `ARDUINO_EVENT_WIFI_STA_CONNECTED`.
- [x] 3.3 In the network task's 1 s loop, after the existing connect/disconnect transition logging, add the active-reconnect block: when `!isConnected`, compute `downForMs` from `lastWifiDisconnectMs`, and when `downForMs >= 30000` and at least 30 s since the previous forced reconnect, call `WiFi.disconnect(false)` + `vTaskDelay(100ms)` + `WiFi.reconnect()`.
- [x] 3.4 Increment `activeReconnectFailures` on each forced attempt and reset to 0 on observed reconnection (the existing `!wasConnected && isConnected` branch).
- [x] 3.5 When `activeReconnectFailures >= 6`, log `Active reconnect exhausted - restarting` and call `ESP.restart()`.

## 4. Verify

- [x] 4.1 Build `pio run -e adafruit_qtpy_esp32s2`; confirm SUCCESS, RAM and flash budgets unchanged within rounding.
- [ ] 4.2 Field validation: after deployment, capture a syslog from a multi-day session and confirm the next disconnect is logged with a reason code, and that active-reconnect lines appear if recovery is needed.
