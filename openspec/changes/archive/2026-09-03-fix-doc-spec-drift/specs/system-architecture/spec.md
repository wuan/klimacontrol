## MODIFIED Requirements

### Requirement: Target hardware platform

The firmware SHALL run on the Adafruit QT Py ESP32-S2 board, using the ESP32-S2 single-core processor clocked at 80 MHz, the Stemma QT I2C connector for external sensors, and the on-board NeoPixel LED for status indication. The board exposes ~2 MB of PSRAM (`psram_size ≈ 2094735` on the production batch, recorded on-device at `src/Network.cpp:555`). The real-time constraints of the firmware (task stacks, FreeRTOS / lwIP / WiFi / mbedTLS working set, DMA buffers) remain *internal-SRAM-only* even with PSRAM available; see the *Memory budget* requirement for the corresponding constraint.

#### Scenario: PlatformIO target selection

- **WHEN** the firmware is built via `pio run`
- **THEN** the `adafruit_qtpy_esp32s2` environment SHALL be used and the produced binary SHALL load and run on the target board

### Requirement: Memory budget

The firmware SHALL keep flash usage under 2 MB (the partition allocation) and SHALL maintain at least 64 KB of free heap to allow OTA updates. The system SHALL restart cleanly if free heap drops below 16 KB during runtime outside of an OTA flow. The firmware SHALL additionally publish `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` (in bytes) via the `/api/status` endpoint under the field `largest_free_block` so that heap *shape* regressions — fragmentation with healthy total free heap — are observable without serial-log access.

OTA memory accounting SHALL use `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` rather than `ESP.getFreeHeap()` / `esp_get_free_heap_size()`. The two calls report different things on this board: the latter sums PSRAM and internal SRAM (`CONFIG_SPIRAM_USE_MALLOC=y`, `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=0`), and would pass an OTA memory gate unconditionally when PSRAM is healthy. The allocations that fail under pressure (task stacks, lwIP / WiFi structures, DMA buffers, the mbedTLS working set) are internal-only, which is what the gate is actually protecting against.

#### Scenario: Build size check

- **WHEN** the build completes
- **THEN** the reported flash usage SHALL be less than 1900 KB (the partition cap) and the build SHALL fail otherwise

#### Scenario: Low heap during runtime

- **WHEN** `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` returns less than 16384 bytes and no OTA update is in progress
- **THEN** the firmware SHALL log a CRITICAL line and call `ESP.restart()` within 1 second

#### Scenario: Heap shape is exposed via the status API

- **WHEN** a client GETs `/api/status` while the firmware is running normally (free heap > 64 KB)
- **THEN** the response JSON contains a `largest_free_block` key whose value is the size in bytes of the largest contiguous free block in the 8-bit-capable heap, so an operator can detect fragmentation regressions from the web UI

### Requirement: FreeRTOS task structure

The firmware SHALL use FreeRTOS with two long-running tasks: a Network task running `Network::task()` and a Sensor Monitor task running `Task::SensorMonitor::task()`. Both tasks SHALL be registered with the ESP-IDF task watchdog using a 30-second timeout, and watchdog timeout SHALL trigger a panic. Each task body SHALL call `esp_task_wdt_reset()` at least once per iteration AND, in addition, SHALL feed the watchdog before and after any blocking external call that may exceed the per-iteration budget (see the *Network task blocking-call safety* requirement in `networking` for the Network task's specific obligations).

The Network task stack SHALL be at least 6 KB (in-tree: 8192 B at `src/Network.cpp:993`, justified against a measured peak of 3544 B in the comment at `src/Network.cpp:976-989`). The Sensor Monitor task stack SHALL be at least 4 KB (in-tree: 6144 B at `src/task/SensorMonitor.cpp:33`, justified against a measured peak of 2056 B in the comment at `src/task/SensorMonitor.cpp:22-29`). Both tasks SHALL emit a periodic "stack HWM" diagnostic line so a future review can verify the threshold against measured use rather than guesswork.

#### Scenario: Tasks register with watchdog

- **WHEN** each task body starts
- **THEN** `esp_task_wdt_add(NULL)` is called and `esp_task_wdt_reset()` is called at least once per iteration

#### Scenario: Network task stack size

- **WHEN** the Network task is created via `xTaskCreate`
- **THEN** its stack is at least 6 KB; the in-tree value is 8192 B at `src/Network.cpp:993`, chosen against a measured peak of 3544 B (~2.3x headroom) per the comment at `src/Network.cpp:976-989`

#### Scenario: Sensor Monitor task stack size

- **WHEN** the Sensor Monitor task is created
- **THEN** its stack is at least 4 KB; the in-tree value is 6144 B at `src/task/SensorMonitor.cpp:33`, chosen against a measured peak of 2056 B (~3x headroom) per the comment at `src/task/SensorMonitor.cpp:22-29`

#### Scenario: Blocking external call feeds the watchdog

- **WHEN** a task body makes a blocking external call (e.g. a UDP exchange in the Network task) that may take longer than the per-iteration watchdog budget
- **THEN** the task body feeds `esp_task_wdt_reset()` immediately before and immediately after the call, so a hung call does not starve the 30 s task watchdog

### Requirement: Ownership hierarchy

Resource ownership SHALL be expressed through `std::unique_ptr<T>`. Non-owning access SHALL use references (`T&`). Raw pointers SHALL NOT be used to express ownership of heap-allocated resources. The `SensorController` SHALL own all `Sensor::Sensor` instances. The `Network` instance SHALL own the `AsyncWebServer` and `StatusLed` instances; it SHALL hold a non-owning `WebServerManager*` to the file-scope instance constructed once in `main.cpp` (`src/main.cpp:137`, wired via `Network::setWebServer(...)` at `src/main.cpp:201`). AP ↔ STA mode transitions SHALL switch the same `WebServerManager` instance between route sets via `setMode(WebServerMode::CONFIG | OPERATIONAL)` rather than destroy and reconstruct it.

#### Scenario: Adding a sensor

- **WHEN** a new sensor is registered
- **THEN** the caller transfers ownership via `sensorController.addSensor(std::make_unique<Sensor::SHT4x>(addr))` using `std::move` — no raw pointer is stored anywhere

#### Scenario: Webserver lifetime

- **WHEN** the device transitions between AP and STA modes
- **THEN** the same `WebServerManager` instance is reused; the route set changes via a `setMode(WebServerMode::...)` call (see `src/Network.cpp:347-352, 427-432, 504-512`). No `WebServerManager` is destroyed or re-allocated as part of the transition.
