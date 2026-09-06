## ADDED Requirements

### Requirement: Control-loop state has a single writer

`Control::TemperatureController` SHALL own all heating control-loop state: the PID accumulators, the relay autotuner, the over-temperature shutoff latch, the last computed output, the PID decimation timestamp, and the actuator agreement fields. That state SHALL be mutated only from the Sensor Monitor task via `update()`, with two documented exceptions that are each single-writer from another task: `publishActuatorState()`, written only by the Network task, and the request flags (`requestAutotuneStart()`, `requestAutotuneCancel()`, `requestGains()`, `acceptAutotuneResult()`), written only by the AsyncTCP web task as `std::atomic<bool>` requests that `update()` consumes with `exchange()`.

Other tasks SHALL read control state either as single scalar loads of single-writer members (safe on the single-core ESP32-S2 without a lock), or through `Config::ConfigManager::getDeviceConfigSnapshot()` for configuration fields. No mutex SHALL guard control state; the class holds no reference to `SensorController` and takes no sensor lock.

The class SHALL be a file-scope global in `main.cpp` constructed after `Config::ConfigManager` and before any task or manager that takes a reference to it; its `begin()` SHALL be called from `setup()` before the Sensor Monitor task is created, so that adopting NVS tuning does not race the task.

#### Scenario: Web task changes gains

- **WHEN** `POST /api/control/tuning` is handled on the AsyncTCP task
- **THEN** the handler SHALL persist through `ConfigManager` and set the gains-change request flag, and the PID gains SHALL change only when the Sensor Monitor task's next `update()` consumes the flag

#### Scenario: Network task reports actuator state

- **WHEN** the Network task completes an actuator tick
- **THEN** it SHALL call `publishActuatorState()` and SHALL write no other control-loop member

#### Scenario: Display reads control state lock-free

- **WHEN** the display refresh on the Network task reads `getReportedState()` and `getControlOutput()`
- **THEN** it SHALL take no lock, and a value at most one Sensor Monitor tick stale is acceptable

## MODIFIED Requirements

### Requirement: Task responsibilities

The Network task SHALL handle WiFi association, NTP synchronization, mDNS advertisement, the async webserver, MQTT publishing, the status LED, and — when the e-paper display is enabled — the periodic display refresh. The Sensor Monitor task SHALL read all configured sensors via `SensorController`, update the cached measurement set, take one `SensorController::getProcessValue()`, and drive `Control::TemperatureController::update()` with that value on every tick.

The display SHALL NOT be given its own FreeRTOS task; a dedicated task stack would consume several kilobytes of internal SRAM, which is the resource the display's paged-rendering design exists to protect.

#### Scenario: Sensor reads run off the network task

- **WHEN** the firmware is running
- **THEN** all I2C bus interaction with sensors SHALL be initiated from the Sensor Monitor task, never from the Network task

#### Scenario: Sensor cadence

- **WHEN** the Sensor Monitor task is running with the default configuration
- **THEN** it SHALL read sensors at 1-second intervals

#### Scenario: Control loop is fed by the sensor task

- **WHEN** a Sensor Monitor tick completes its sensor reads
- **THEN** the same iteration SHALL call `Control::TemperatureController::update(temperature, valid, nowMs)` with values from a single `getProcessValue()` call, and no other task SHALL call `update()`

#### Scenario: Display refresh runs on the Network task

- **WHEN** the display is enabled and the refresh policy calls for a refresh
- **THEN** the refresh SHALL be performed from the Network task's one-second loop, alongside `StatusLed::update()`, and SHALL be skipped while `OTAUpdater::isUpdateInProgress()` is true

#### Scenario: Display refresh feeds the watchdog

- **WHEN** the Network task performs an e-paper refresh, which blocks on the panel's BUSY line for up to several seconds
- **THEN** `esp_task_wdt_reset()` SHALL be called immediately before and immediately after the blocking page loop, satisfying the blocking-external-call obligation in the *FreeRTOS task structure* requirement

#### Scenario: Blocking external call feeds the watchdog

- **WHEN** a task body makes a blocking external call (e.g. a UDP exchange in the Network task) that may take longer than the per-iteration watchdog budget
- **THEN** the task body feeds `esp_task_wdt_reset()` immediately before and immediately after the call, so a hung call does not starve the 30 s task watchdog
