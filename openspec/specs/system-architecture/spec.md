# system-architecture Specification

## Purpose
TBD - created by archiving change baseline-capabilities. Update Purpose after archive.
## Requirements
### Requirement: Target hardware platform

The firmware SHALL run on the Adafruit QT Py ESP32-S2 board, using the ESP32-S2 single-core processor clocked at 80 MHz, the Stemma QT I2C connector for external sensors, and the on-board NeoPixel LED for status indication. PSRAM SHALL NOT be assumed available.

#### Scenario: PlatformIO target selection

- **WHEN** the firmware is built via `pio run`
- **THEN** the `adafruit_qtpy_esp32s2` environment SHALL be used and the produced binary SHALL be loadable onto the target board without PSRAM-dependent code paths

### Requirement: Memory budget

The firmware SHALL keep flash usage under 2 MB (the partition allocation) and SHALL maintain at least 64 KB of free heap to allow OTA updates. The system SHALL restart cleanly if free heap drops below 16 KB during runtime outside of an OTA flow. The firmware SHALL additionally publish `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` (in bytes) via the `/api/status` endpoint under the field `largest_free_block` so that heap *shape* regressions — fragmentation with healthy total free heap — are observable without serial-log access.

#### Scenario: Build size check

- **WHEN** the build completes
- **THEN** the reported flash usage SHALL be less than 1900 KB (the partition cap) and the build SHALL fail otherwise

#### Scenario: Low heap during runtime

- **WHEN** `ESP.getFreeHeap()` returns less than 16384 bytes and no OTA update is in progress
- **THEN** the firmware SHALL log a CRITICAL line and call `ESP.restart()` within 1 second

#### Scenario: Heap shape is exposed via the status API

- **WHEN** a client GETs `/api/status` while the firmware is running normally (free heap > 64 KB)
- **THEN** the response JSON contains a `largest_free_block` key whose value is the size in bytes of the largest contiguous free block in the 8-bit-capable heap, so an operator can detect fragmentation regressions from the web UI

### Requirement: FreeRTOS task structure

The firmware SHALL use FreeRTOS with two long-running tasks: a Network task running `Network::task()` and a Sensor Monitor task running `Task::SensorMonitor::task()`. Both tasks SHALL be registered with the ESP-IDF task watchdog using a 30-second timeout, and watchdog timeout SHALL trigger a panic. Each task body SHALL call `esp_task_wdt_reset()` at least once per iteration AND, in addition, SHALL feed the watchdog before and after any blocking external call that may exceed the per-iteration budget (see the *Network task blocking-call safety* requirement in `networking` for the Network task's specific obligations).

#### Scenario: Tasks register with watchdog

- **WHEN** each task body starts
- **THEN** `esp_task_wdt_add(NULL)` is called and `esp_task_wdt_reset()` is called at least once per iteration

#### Scenario: Network task stack size

- **WHEN** the Network task is created via `xTaskCreate`
- **THEN** its stack is at least 14 KB (per current configuration; the original 10 KB target was raised for stability)

#### Scenario: Sensor Monitor task stack size

- **WHEN** the Sensor Monitor task is created
- **THEN** its stack is at least 8 KB

#### Scenario: Blocking external call feeds the watchdog

- **WHEN** a task body makes a blocking external call (e.g. a UDP exchange in the Network task) that may take longer than the per-iteration watchdog budget
- **THEN** the task body feeds `esp_task_wdt_reset()` immediately before and immediately after the call, so a hung call does not starve the 30 s task watchdog

### Requirement: Task responsibilities

The Network task SHALL handle WiFi association, NTP synchronization, mDNS advertisement, the async webserver, MQTT publishing, the status LED, and — when the e-paper display is enabled — the periodic display refresh. The Sensor Monitor task SHALL read all configured sensors, update the cached measurement set, and drive the temperature controller.

The display SHALL NOT be given its own FreeRTOS task; a dedicated task stack would consume several kilobytes of internal SRAM, which is the resource the display's paged-rendering design exists to protect.

#### Scenario: Sensor reads run off the network task

- **WHEN** the firmware is running
- **THEN** all I2C bus interaction with sensors SHALL be initiated from the Sensor Monitor task, never from the Network task

#### Scenario: Sensor cadence

- **WHEN** the Sensor Monitor task is running with the default configuration
- **THEN** it SHALL read sensors at 1-second intervals

#### Scenario: Display refresh runs on the Network task

- **WHEN** the display is enabled and the refresh policy calls for a refresh
- **THEN** the refresh SHALL be performed from the Network task's one-second loop, alongside `StatusLed::update()`, and SHALL be skipped while `OTAUpdater::isUpdateInProgress()` is true

#### Scenario: Display refresh feeds the watchdog

- **WHEN** the Network task performs an e-paper refresh, which blocks on the panel's BUSY line for up to several seconds
- **THEN** `esp_task_wdt_reset()` SHALL be called immediately before and immediately after the blocking page loop, satisfying the blocking-external-call obligation in the *FreeRTOS task structure* requirement

#### Scenario: Blocking external call feeds the watchdog

- **WHEN** a task body makes a blocking external call (e.g. a UDP exchange in the Network task) that may take longer than the per-iteration watchdog budget
- **THEN** the task body feeds `esp_task_wdt_reset()` immediately before and immediately after the call, so a hung call does not starve the 30 s task watchdog

### Requirement: Ownership hierarchy

Resource ownership SHALL be expressed through `std::unique_ptr<T>`. Non-owning access SHALL use references (`T&`). Raw pointers SHALL NOT be used to express ownership of heap-allocated resources. The `SensorController` SHALL own all `Sensor::Sensor` instances. The `Network` instance SHALL own the `AsyncWebServer`, `StatusLed`, and `WebServerManager` instances.

#### Scenario: Adding a sensor

- **WHEN** a new sensor is registered
- **THEN** the caller transfers ownership via `sensorController.addSensor(std::make_unique<Sensor::SHT4x>(addr))` using `std::move` — no raw pointer is stored anywhere

#### Scenario: Webserver lifetime

- **WHEN** the device transitions between AP and STA modes
- **THEN** the previous `WebServerManager` is destroyed by resetting the `std::unique_ptr`, and the replacement is created via `std::make_unique`

### Requirement: Thread safety for shared sensor data

`SensorController` SHALL use a mutex to guard the shared `currentMeasurements` vector. All reads and writes of cached measurement state SHALL acquire that mutex.

#### Scenario: Concurrent read during write

- **WHEN** the Sensor Monitor task writes new measurements while a Network task handler reads `getMeasurements()`
- **THEN** both operations are serialized via the mutex and the reader SHALL observe a fully written snapshot (no torn reads)

### Requirement: Cross-task reads of `Support::Stats` use a snapshot accessor

`Support::Stats` exposes four `uint64_t` cycle-delay counters at `GET /api/about` (`cycle_count`, `avg_cycle_delay`, `min_cycle_delay`, `max_cycle_delay`) that are written once per loop iteration from the Sensor Monitor task and read from the AsyncTCP task. Cross-task readers SHALL observe those counters through a single `Support::Stats::snapshot()` accessor that returns an indivisible copy of all four fields under a spinlock, so a single handler call observes one coherent set of numbers rather than four independent ones. Same-task readers MAY continue to use the per-field getters. The spinlock SHALL be a `std::atomic_flag` initialised with `ATOMIC_FLAG_INIT`, matching the existing `deviceConfigLock` / `restartLock` discipline.

#### Scenario: AsyncTCP handler reads four consistent counters per request

- **WHEN** the `GET /api/about` handler running on the AsyncTCP task observes cycle-delay stats while the Sensor Monitor task is in the middle of a `stats.add(duration)` call
- **THEN** every one of `cycle_count`, `avg_cycle_delay`, `min_cycle_delay`, and `max_cycle_delay` describes the same logical snapshot, and the JSON response carries no torn combination of pre-update and post-update field values

#### Scenario: Lock-free native build still links

- **WHEN** `Support::Stats` is exercised from the `native` test environment without the ESP-IDF FreeRTOS port
- **THEN** the snapshot accessor and the lock compile and run identically to the firmware build, because the synchronization primitive is `std::atomic_flag` rather than a FreeRTOS-only API

#### Scenario: Min and max are consistent within one snapshot

- **WHEN** a reader takes a single snapshot while a writer has just inserted a value `v`
- **THEN** `snapshot.min <= v <= snapshot.max`, and `snapshot.min` and `snapshot.max` are both drawn from the same set of inserted values (no value outside the inserted range can appear, no value below the running minimum can appear in either field)

### Requirement: The device remains diagnosable

The firmware SHALL retain at least one working log channel on a device under test, so that a fault which produces no crash and no HTTP response can still be observed. Diagnosis SHALL NOT depend on inferring behaviour from HTTP response bodies alone, because that channel is unavailable for exactly the faults in which no response is produced.

Where the primary channel is USB CDC, its availability SHALL be verified rather than assumed: the board definition setting `ARDUINO_USB_CDC_ON_BOOT=1` is not by itself evidence that log output reaches a host.

#### Scenario: Boot log is observable

- **WHEN** a device is reset with a serial monitor attached
- **THEN** the boot log SHALL be readable, including the `Reset reason:` line

#### Scenario: A silent log channel is treated as a defect

- **WHEN** no log output can be obtained from a device over its primary channel
- **THEN** that SHALL be treated as a fault to be fixed, not worked around, because it blocks diagnosis of every other fault

### Requirement: Debug-level diagnostics are reachable

Diagnostics written at `ESP_LOGD` SHALL be reachable through a documented build configuration. `CORE_DEBUG_LEVEL` defaults to `0`, which compiles every `ESP_LOGD` call out of the binary entirely, so a debug-level log line is invisible in a default build and cannot be relied upon by a verification procedure.

Any task that asks an operator to read a debug-level log line SHALL state the build flag required to make it exist.

#### Scenario: Debug logging can be enabled

- **WHEN** a diagnostic build is produced with `-DCORE_DEBUG_LEVEL=4`
- **THEN** `ESP_LOGD` output SHALL be emitted on the active log channel

#### Scenario: Verification steps name their build requirement

- **WHEN** a verification step depends on reading an `ESP_LOGD` line
- **THEN** that step SHALL state the build flag needed, so it is not recorded as unverifiable after the fact

