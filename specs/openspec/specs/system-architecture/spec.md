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

The firmware SHALL keep flash usage under 2 MB (the partition allocation) and SHALL maintain at least 64 KB of free heap to allow OTA updates. The system SHALL restart cleanly if free heap drops below 16 KB during runtime outside of an OTA flow.

#### Scenario: Build size check

- **WHEN** the build completes
- **THEN** the reported flash usage SHALL be less than 1900 KB (the partition cap) and the build SHALL fail otherwise

#### Scenario: Low heap during runtime

- **WHEN** `ESP.getFreeHeap()` returns less than 16384 bytes and no OTA update is in progress
- **THEN** the firmware SHALL log a CRITICAL line and call `ESP.restart()` within 1 second

### Requirement: FreeRTOS task structure

The firmware SHALL use FreeRTOS with two long-running tasks: a Network task running `Network::task()` and a Sensor Monitor task running `Task::SensorMonitor::task()`. Both tasks SHALL be registered with the ESP-IDF task watchdog using a 30-second timeout, and watchdog timeout SHALL trigger a panic.

#### Scenario: Tasks register with watchdog

- **WHEN** each task body starts
- **THEN** `esp_task_wdt_add(NULL)` is called and `esp_task_wdt_reset()` is called at least once per iteration

#### Scenario: Network task stack size

- **WHEN** the Network task is created via `xTaskCreate`
- **THEN** its stack is at least 14 KB (per current configuration; the original 10 KB target was raised for stability)

#### Scenario: Sensor Monitor task stack size

- **WHEN** the Sensor Monitor task is created
- **THEN** its stack is at least 8 KB

### Requirement: Task responsibilities

The Network task SHALL handle WiFi association, NTP synchronization, mDNS advertisement, the async webserver, MQTT publishing, and the status LED. The Sensor Monitor task SHALL read all configured sensors, update the cached measurement set, and drive the temperature controller.

#### Scenario: Sensor reads run off the network task

- **WHEN** the firmware is running
- **THEN** all I2C bus interaction with sensors SHALL be initiated from the Sensor Monitor task, never from the Network task

#### Scenario: Sensor cadence

- **WHEN** the Sensor Monitor task is running with the default configuration
- **THEN** it SHALL read sensors at 1-second intervals

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

