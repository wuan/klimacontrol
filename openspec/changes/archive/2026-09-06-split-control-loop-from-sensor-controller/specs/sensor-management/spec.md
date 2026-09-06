## ADDED Requirements

### Requirement: Single-lock process value accessor

`SensorController` SHALL expose `getProcessValue()`, returning a `ProcessValue` struct of `{float temperature, bool valid, uint32_t timestamp}` captured under a single acquisition of the data mutex. `temperature` SHALL be the value `getTemperature()` would return (first `Temperature` measurement in sensor order, or `NAN`); `valid` SHALL be the value `isDataValid()` would return; `timestamp` SHALL be the value `getLastReadingTimestamp()` would return. On mutex timeout the accessor SHALL return the default `{NAN, false, 0}`.

The accessor exists so the Sensor Monitor task can hand the control loop a consistent pair without the loop taking the lock several times per tick, and it SHALL NOT copy the measurement vector or allocate.

#### Scenario: Consistent pair

- **WHEN** the Sensor Monitor task calls `getProcessValue()` while the cache holds a valid temperature of `21.3` °C
- **THEN** the result SHALL be `{21.3, true, <timestamp of that reading>}` observed under one lock

#### Scenario: Invalid cache

- **WHEN** every cache slot is invalid
- **THEN** `getProcessValue()` SHALL return `valid = false` and `temperature = NAN`

#### Scenario: No allocation

- **WHEN** `getProcessValue()` is called once per second by the Sensor Monitor task
- **THEN** no heap allocation SHALL occur as a result of the call

## MODIFIED Requirements

### Requirement: SensorController aggregation

`SensorController` SHALL own all sensor instances, expose `addSensor(std::unique_ptr<Sensor::Sensor>)`, `readSensors()`, `readSensors(uint32_t nowMs)`, `getMeasurements()`, `getTemperature()`, `getRelativeHumidity()`, `getDewPoint()`, `getProcessValue()`, `hasConnectedSensors()`, and `isDataValid()`. It SHALL expose no temperature-control API: setpoint, enable state, PID, autotune and actuator-state concerns belong to `Control::TemperatureController` (see `temperature-control`), which SensorController SHALL NOT reference. When multiple sensors of the same measurement type are present, `getTemperature()` / `getRelativeHumidity()` / `getDewPoint()` SHALL return the value from the first sensor that reported the type, in the order returned by the sorted sensor list. Averaging across sensors is **not** used because a faulty reading (e.g. a wiring fault reporting −40 °C) diluted by a healthy reading would still produce a contaminated value; the defence against bad readings is per-driver range validation at the point of acquisition (see `sensor-management` → "Per-driver range validation"), not averaging.

`isDataValid()` SHALL return true iff at least one per-sensor cache slot is valid after the most recent `readSensors()` call (see "Per-sensor last-good cache"). It SHALL NOT depend on whether any sensor was read on that particular call. `getLastReadingTimestamp()` and `getTimeSinceLastReading()` SHALL refer to the most recent `readSensors()` call in which at least one sensor returned a valid reading.

#### Scenario: Two temperature sensors, first is healthy

- **WHEN** two `SHT4x` sensors are present and the first sensor in the sorted order reports a valid temperature `T1`
- **THEN** `getTemperature()` SHALL return `T1`

#### Scenario: No connected sensors

- **WHEN** no sensors are present or all sensors are in `InitFailed` state
- **THEN** `hasConnectedSensors()` SHALL return false and `isDataValid()` SHALL return false

#### Scenario: Data stays valid on a tick with no reads

- **WHEN** a default-interval sensor produced a valid reading at `t0` and `readSensors(t0 + 1000)` reads no sensor
- **THEN** `isDataValid()` SHALL return true after the second call

#### Scenario: Data becomes invalid when all slots expire

- **WHEN** every sensor's slot has been invalidated by status change or age
- **THEN** `isDataValid()` SHALL return false and `getValidMeasurements()` SHALL return an empty vector

#### Scenario: No control surface

- **WHEN** a caller needs the setpoint, the control output, or the heating permission
- **THEN** it SHALL obtain them from `Control::TemperatureController`, and `SensorController` SHALL provide no such accessor
