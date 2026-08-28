## MODIFIED Requirements

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
