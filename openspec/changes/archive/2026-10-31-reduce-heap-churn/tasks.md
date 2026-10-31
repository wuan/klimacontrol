# Tasks

## 1. OpenSpec change artifacts

- [x] 1.1 Create this change (proposal, design, spec deltas for `http-api` and `memory-management`, tasks). Validate with `openspec validate --all --strict` from the repo root.

## 2. Stream JSON responses (item 1)

- [x] 2.1 Add `sendJson()` to `src/routes/RouteHelpers.h` (AsyncResponseStream sized by `measureJson`, `setCode` for non-200).
- [x] 2.2 Replace every `String` + `serializeJson` + `send(...)` response site in `src/routes/*.cpp` and `WebServerManager::handleWiFiConfig` with `sendJson()` (17 sites across Control/Display/I2C/Mqtt/OTA/Sensor/Settings/Status/Syslog routes + WiFi config), preserving status codes (202/409/500).
- [x] 2.3 Verified: `pio run -e adafruit_qtpy_esp32s2` compiles (SUCCESS, RAM 84068 B, Flash 1419582 B).

## 3. MQTT discovery buffer off the task stack (item 2)

- [x] 3.1 Moved the discovery payload buffer in `MqttPublisher::publishDiscovery()` to a file-static `discoveryPayload[MQTT_BUFFER_SIZE]`; kept `MQTT_BUFFER_SIZE = 2048` and corrected its comment to the measured worst case (1787 B payload / 1828 B framed, so 2048 is the smallest round size above the pinned >=128 B headroom).
- [x] 3.2 Verified: `pio test -e native -f test_mqtt_payload` passes (worst-case pin unchanged); firmware builds. `nm` confirms `discoveryPayload` is now 2048 B in BSS (dram0.bss 64096 -> 66144) instead of on the Network task stack.

## 4. Device ID cache + IP formatting (item 3)

- [x] 4.1 `DeviceId::getDeviceId()`/`getMacAddress()` return `const char*` into a cached static buffer (ARDUINO and native branches); call sites updated in `MqttClient.cpp`, `MqttPublisher.cpp`, `MdnsAdvertiser.cpp`, `ApProvisioning.cpp`, `WebServerManager.cpp`.
- [x] 4.2 Added `src/support/IpFormat.h`; replaced `toString()` temporaries in `Network.cpp`, `WifiStation.cpp`, `CaptivePortal.cpp`, `ApProvisioning.cpp`, `WebServerManager.cpp` (AccessLogger) and the `ip_address`/`ap_ip` doc fields in `StatusRoutes.cpp`.
- [x] 4.3 Verified: full native suite passes (722 cases), firmware build SUCCESS.

## 5. Verification and archive

- [x] 5.1 `pio test -e native`: 722/722 passed. `pio run -e adafruit_qtpy_esp32s2`: SUCCESS. `clang-format -i` (19.1.7) applied to all touched C/C++ files.
- [x] 5.2 BSS delta confirmed via `xtensa-esp32s2-elf-size`/`nm`: +2048 B static discovery buffer, +7 B cached device ID; Network task stack spike removed.
- [x] 5.3 `openspec validate --all --strict` (pinned @fission-ai/openspec@1.14.0 via npx, as CI runs): 21 passed, 0 failed. Archived with `openspec archive 2026-10-31-reduce-heap-churn --yes`.
