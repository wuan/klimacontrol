## 1. Audit and Singleton Lifetime

- [x] 1.1 Audit current `make_unique<...WebServerManager>` call sites
  in `src/Network.cpp` (lines 305, 375, 442) and confirm all three
  are reachable on a single boot. Document the largest
  `WebServerManager` instance size (read the subclasses' member
  lists) for the change notes.
- [x] 1.2 Audit `MqttClient::begin()` to confirm it is idempotent —
  i.e. calling `begin()` on an already-constructed `MqttClient`
  performs a reconnect handshake without re-allocating the
  underlying client. If it is *not* idempotent, refactor it
  before folding it into the singleton rule.
- [x] 1.3 Audit `AsyncWebServer` for any per-request `new` in the
  request hot path (handler chains, response objects). File an
  upstream issue if found; record the result in the design notes.
- [x] 1.4 Verify the 512-byte JSON ceiling in
  `openspec/specs/http-api/spec.md` is not exceeded by the
  largest current route response (`/api/sensors` at full
  saturation). If it is, raise the ceiling as a follow-up and
  flag the route here.

## 2. Collapse `WebServerManager` Subclasses

- [x] 2.1 In `src/WebServerManager.h`, add a public
  `setMode(WebServerMode mode)` method and a private
  `clearRoutes()` helper. Define `WebServerMode { CONFIG,
  OPERATIONAL }` in the same header.
- [x] 2.2 In `src/WebServerManager.cpp`, move the body of
  `ConfigWebServerManager::setupRoutes()` into a private
  `setupConfigRoutes()` and the body of
  `OperationalWebServerManager::setupRoutes()` into
  `setupAPIRoutes()` (or rename to `setupOperationalRoutes()`).
  `setMode()` shall call `clearRoutes()` first, then the
  appropriate `setup*Routes()`, then `server.begin()`.
- [x] 2.3 Remove `ConfigWebServerManager` and
  `OperationalWebServerManager` from both header and source. Keep
  only the base `WebServerManager`. Adjust the `unique_ptr`
  type in `Network.h` accordingly.
- [x] 2.4 Build for ESP32: `pio run -e adafruit_qtpy_esp32s2`.
  Fix any callers that still reference the removed subclasses.

## 3. Make `WebServerManager` and `MqttClient` Long-Lived

- [x] 3.1 In `src/main.cpp`, after `Network::begin()` is called,
  construct the `WebServerManager` once and pass it into
  `Network`. Update the `Network` constructor signature to accept
  the pre-built server (or have `Network::begin()` build it once).
- [x] 3.2 In `src/Network.cpp`, replace the three
  `webServer = std::make_unique<...>(...)` reassignments with
  `webServer->setMode(WebServerMode::CONFIG)` /
  `setMode(WebServerMode::OPERATIONAL)` calls. Confirm
  `webServer.reset()` (line 386) is replaced with
  `webServer->setMode(WebServerMode::NONE)` or removed entirely.
- [x] 3.3 In `src/Network.cpp`, replace
  `mqttClient = std::make_unique<MqttClient>()` (line 277) with
  a single construction in `Network::begin()` and a
  `mqttClient->begin(mqttConfig)` re-init call on each STA
  connect.
- [x] 3.4 Build for ESP32: `pio run -e adafruit_qtpy_esp32s2`.
  Confirm the build still passes and the firmware boots to
  STA mode on a real device.

## 4. Reserve Sensor Vector Capacity

- [x] 4.1 In `src/SensorController.h`, add a public
  `void reserveSensorSlots(size_t n);` method.
- [x] 4.2 In `src/SensorController.cpp`, implement
  `reserveSensorSlots` to call `sensors.reserve(n)` and
  `currentMeasurements.reserve(n * MAX_MEASUREMENTS_PER_SENSOR)`
  (define `MAX_MEASUREMENTS_PER_SENSOR` as a `constexpr` near
  the top of the file, e.g. `8`).
- [x] 4.3 In `src/main.cpp`, define
  `static constexpr size_t MAX_KNOWN_SENSORS = 10;` and call
  `sensorController.reserveSensorSlots(MAX_KNOWN_SENSORS);`
  immediately after the `sensorController.begin()` call and
  before the I2C scan loop.
- [x] 4.4 Build for ESP32 and native:
  `pio run -e adafruit_qtpy_esp32s2 && pio test -e native`.

## 5. Replace Per-Request `JsonDocument` Allocations

- [x] 5.1 In `src/WebServerManager.cpp`, replace
  `std::make_unique<JsonDocument>()` in
  `handleWiFiConfig` with stack-allocated `StaticJsonDocument`
  for both the body parse and the response build.
- [x] 5.2 In each handler in `src/routes/StatusRoutes.cpp`,
  `src/routes/SensorRoutes.cpp`, `src/routes/ControlRoutes.cpp`,
  `src/routes/SettingsRoutes.cpp`, `src/routes/OTARoutes.cpp`,
  `src/routes/MqttRoutes.cpp`, `src/routes/SyslogRoutes.cpp`,
  and `src/routes/I2CRoutes.cpp`, replace any
  `std::make_unique<JsonDocument>()` with a stack-allocated
  `StaticJsonDocument<512>` local to the handler. The
  `String` response is then built with `serializeJson` and
  passed to `request->send`.
- [x] 5.3 Grep `src/routes/` and `src/WebServerManager.cpp` for
  `make_unique<JsonDocument>`, `new JsonDocument`, and any
  other heap-allocation patterns. The result SHALL be empty
  for the request hot path.
- [x] 5.4 Build for ESP32: `pio run -e adafruit_qtpy_esp32s2`.

## 6. Expose `largest_free_block` in `/api/status`

- [x] 6.1 In `src/routes/StatusRoutes.cpp`, add the
  `largest_free_block` field to the JSON document built by
  the `/api/status` handler, sourced from
  `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`. Guard
  the call with `#ifdef ARDUINO` so native tests compile.
- [x] 6.2 Update the `data/index.html` (or whichever template
  embeds the status panel) to display the new field next to
  `free_heap`. Re-run `scripts/compress_web.py` to regenerate
  the `src/generated/*_gz.h` headers.
- [x] 6.3 Build for ESP32: `pio run -e adafruit_qtpy_esp32s2`.
- [ ] 6.4 Flash the firmware, navigate to the device's web UI,
  confirm `largest_free_block` is visible and changes
  responsively (e.g. drop after an OTA download chunk, recover
  after the OTA finishes).

## 7. Native Tests

- [x] 7.1 Create `test/test_memory/` with a `test_singleton_lifetimes.cpp`
  native test that asserts:
  - `SensorController::reserveSensorSlots(10)` followed by
    adding 10 sensors does not reallocate `sensors` (compare
    `sensors.capacity()` before and after each `addSensor`).
  - The number of `if (i2cBus.probe(addr))` branches in
    `src/main.cpp` equals `10` (a static `assert`-style check
    parsed from the source file or a hand-maintained
    `MAX_KNOWN_SENSORS` constant asserted in the test).
- [x] 7.2 Create `test/test_memory/test_status_payload.cpp` that
  exercises the status handler stub and asserts the produced
  JSON contains a `largest_free_block` key. The test compiles
  under `ARDUINO` (linking a stub for `heap_caps_*`) and is
  also runnable in the `native` env with the key reported as
  zero.
- [x] 7.3 Run the full native test suite:
  `pio test -e native`. All existing tests plus the two new
  ones SHALL pass.

## 8. Spec Updates and Validation

- [x] 8.1 Run `openspec validate --change reduce-heap-fragmentation
  --strict` from the repo root. Fix any reported errors.
- [x] 8.2 Run `openspec list --specs` and confirm
  `memory-management` is listed alongside the existing
  capabilities.
- [x] 8.3 Update the project README or `AGENTS.md` (whichever
  references the spec list) only if a CI / discovery script
  consumes it; do not edit documentation proactively.

## 9. Soak Test and Rollout

- [ ] 9.1 Flash the new firmware to a single test device and
  monitor `largest_free_block` via `/api/status` over a
  24-hour soak. The metric should be flat (within ±5% of the
  value at boot, modulo normal allocations) over the soak
  period.
  *(Deferred — requires real device; not runnable in this environment.)*
- [ ] 9.2 Force-trigger a WiFi reconnect cycle (reboot the
  AP, wait for the device to fail 3 times, observe AP-mode
  bring-up, then restore the AP and confirm STA reconnect).
  Confirm the heap does not lose a measurable amount of
  `largest_free_block` across the cycle.
  *(Deferred — requires real device.)*
- [ ] 9.3 Confirm the new firmware is rollback-safe: flash the
  previous release via OTA, confirm the device boots and
  `/api/status` does not contain `largest_free_block`
  (since the previous release does not have it).
  *(Deferred — requires real device.)*
