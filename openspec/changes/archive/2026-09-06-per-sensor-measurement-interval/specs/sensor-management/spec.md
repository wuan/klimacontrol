## ADDED Requirements

### Requirement: Sensors declare their required read interval

`Sensor::Sensor` SHALL expose `virtual uint32_t requiredIntervalMs() const` returning `0` by default. A return value of `0` SHALL mean the sensor is read on the system measurement interval. A non-zero value `N` SHALL mean the sensor MUST be read every `N` ms regardless of the system measurement interval. The `SGP40` driver SHALL return `1000`, because the Sensirion VOC index algorithm it feeds assumes 1 Hz sampling. All other shipped drivers SHALL return `0`.

#### Scenario: Default driver has no requirement

- **WHEN** `requiredIntervalMs()` is called on an `SHT4x`, `BME680`, `SCD4x`, `BMP3xx`, `DPS310`, `PM25`, `TSL2591`, `VEML7700`, `BH1750` or `DeviceSensor` instance
- **THEN** it SHALL return `0`

#### Scenario: SGP40 requires 1 Hz

- **WHEN** `requiredIntervalMs()` is called on an `SGP40` instance
- **THEN** it SHALL return `1000`

### Requirement: System measurement interval is a compile-time constant

`SensorController` SHALL define `MEASUREMENT_INTERVAL_MS` as a public compile-time constant equal to `15000`. It SHALL NOT be loaded from or persisted to configuration.

#### Scenario: Constant value

- **WHEN** code references `SensorController::MEASUREMENT_INTERVAL_MS`
- **THEN** its value SHALL be `15000`

### Requirement: Sensors are read only when due

`SensorController::readSensors(uint32_t nowMs)` SHALL read a sensor on a given call only if it is `Online` and due. A sensor with `requiredIntervalMs() == 0` SHALL be due when the shared default phase is due, which is the first call ever and every call for which `nowMs - lastDefaultCycleMs >= MEASUREMENT_INTERVAL_MS`. A sensor with `requiredIntervalMs() == N > 0` SHALL be due when it has never been read or `nowMs - lastReadMs >= N`, where `lastReadMs` is that sensor's own last attempted read. All default-interval sensors SHALL be read on the same call, so that their readings are time-coherent. `readSensors()` without arguments SHALL behave as `readSensors(millis())`. The SensorMonitor task tick SHALL remain 1000 ms.

#### Scenario: First cycle reads everything

- **WHEN** `readSensors(t0)` is called for the first time with an `Online` default-interval sensor and an `Online` 1000 ms sensor
- **THEN** both sensors' `read()` SHALL be called exactly once

#### Scenario: Default sensor skipped inside the interval

- **WHEN** `readSensors(t0)` has read a default-interval sensor and `readSensors(t0 + 1000)` is called
- **THEN** that sensor's `read()` SHALL NOT be called on the second invocation

#### Scenario: Required-interval sensor read every tick

- **WHEN** a sensor returning `requiredIntervalMs() == 1000` is `Online` and `readSensors()` is called at `t0`, `t0 + 1000`, `t0 + 2000`
- **THEN** its `read()` SHALL be called on each of the three invocations

#### Scenario: Default sensors share a phase

- **WHEN** two default-interval sensors are `Online`, `readSensors(t0)` was called, and `readSensors(t0 + 15000)` is called
- **THEN** both sensors' `read()` SHALL be called on the second invocation, and neither SHALL have been called on any invocation strictly between `t0` and `t0 + 15000`

#### Scenario: Late tick shifts the phase without a double read

- **WHEN** `readSensors(t0)` has read the default-interval sensors and the next calls are at `t0 + 16000` and `t0 + 30000`
- **THEN** the default-interval sensors SHALL be read at `t0 + 16000` and SHALL NOT be read at `t0 + 30000` (the next default read is due at `t0 + 31000` or later)

### Requirement: Per-sensor last-good cache

`SensorController` SHALL keep, for each sensor, an internal slot holding the measurements from that sensor's most recent valid `read()` (including its `Time` measurement), the time that reading was taken, and a validity flag. After each `readSensors()` call, the measurement snapshot returned by `getMeasurements()`, `getValidMeasurements()` and `getSnapshot()` SHALL be the concatenation of every valid slot's measurements in sorted sensor order. A slot SHALL be invalidated when its sensor's status is no longer `Online`, or when `nowMs - lastValidMs > 3 × effectiveInterval`, where `effectiveInterval` is the sensor's `requiredIntervalMs()` if non-zero and `MEASUREMENT_INTERVAL_MS` otherwise. The cache SHALL be purely internal: no per-measurement age or timestamp SHALL be added to the HTTP API or the MQTT payload.

#### Scenario: Measurements persist across a skipped tick

- **WHEN** a default-interval temperature sensor was read at `t0` and `readSensors(t0 + 1000)` runs without reading it
- **THEN** `getMeasurements()` after the second call SHALL still contain that sensor's temperature measurement with the value from `t0`

#### Scenario: Failed read keeps last-good while still Online

- **WHEN** a sensor returned a valid reading at `t0`, returns `valid = false` at `t0 + 15000`, and remains `Online`
- **THEN** `getMeasurements()` SHALL still contain the `t0` values for that sensor

#### Scenario: Slot cleared when sensor leaves Online

- **WHEN** a sensor's status transitions to `ReadFailing` or `InitFailed`
- **THEN** its slot SHALL be invalidated on that `readSensors()` call and none of its measurements SHALL appear in `getMeasurements()`

#### Scenario: Slot expires after three missed intervals

- **WHEN** a default-interval sensor was last valid at `t0`, remains `Online`, and `readSensors(t0 + 45001)` runs without a valid reading from it
- **THEN** its slot SHALL be invalidated and its measurements SHALL NOT appear in `getMeasurements()`

#### Scenario: Snapshot order follows sensor order

- **WHEN** two sensors both provide `Temperature` and both slots are valid
- **THEN** `getTemperature()` SHALL return the value from the sensor earlier in the sorted order, exactly as when both were read on the same tick

### Requirement: Dependent sensors receive cached inputs

The `prior` vector passed to `Sensor::read(config, prior)` SHALL be built from the union of currently valid slots, updated with any reading completed earlier in the same `readSensors()` call. A sensor that depends on measurement types provided by another sensor SHALL therefore see those inputs even on calls where the providing sensor was not due.

#### Scenario: SGP40 sees humidity between humidity reads

- **WHEN** an `SHT4x` (default interval) and an `SGP40` (1000 ms) are `Online`, `readSensors(t0)` read both, and `readSensors(t0 + 1000)` reads only the `SGP40`
- **THEN** the `prior` passed to `SGP40::read()` at `t0 + 1000` SHALL contain the `Temperature` and `RelativeHumidity` measurements from the `SHT4x` read at `t0`

#### Scenario: Same-tick provider is seen fresh

- **WHEN** both a provider and a dependent sensor are due on the same call and the provider precedes the dependent in sorted order
- **THEN** the `prior` passed to the dependent SHALL contain the provider's value from this call, not from a previous one

### Requirement: SCD4x driver holds no private reading cache

The `SCD4x` driver SHALL NOT retain the previous CO2 value across `read()` calls. `read()` SHALL return `valid = true` with a `CO2` measurement only when the sensor reports data ready and the measurement is read successfully with `co2 > 0`; otherwise it SHALL return `valid = false`. Persistence of the last value between reads is provided by the controller's per-sensor cache.

#### Scenario: Data not ready

- **WHEN** `SCD4x::read()` is called and `getDataReadyStatus()` reports no new data
- **THEN** the returned `SensorReading` SHALL have `valid = false` and no measurements

### Requirement: I2C bus recovery counts attempted sensors only

The controller SHALL attempt I2C bus recovery after `I2C_RECOVERY_FAILURE_STREAK` (3) consecutive `readSensors()` calls in which at least one I2C sensor was `Online` and due (attempted) and none of the attempted I2C sensors returned a valid reading. A call in which no I2C sensor was attempted SHALL neither increment nor reset the streak. Any valid I2C reading SHALL reset the streak to zero.

#### Scenario: Quiet ticks do not count as failures

- **WHEN** the only I2C sensors are default-interval and `readSensors()` is called on 14 consecutive ticks on which none of them is due
- **THEN** the failure streak SHALL be unchanged and no bus recovery SHALL be attempted

#### Scenario: Three attempted-and-failed cycles trigger recovery

- **WHEN** on three consecutive `readSensors()` calls at least one I2C sensor was attempted and every attempted I2C sensor returned `valid = false`
- **THEN** the controller SHALL attempt I2C bus recovery on the third call and reset the streak

#### Scenario: One valid reading resets the streak

- **WHEN** the streak is 2 and the next call has an attempted I2C sensor return a valid reading
- **THEN** the streak SHALL be reset to 0

## MODIFIED Requirements

### Requirement: SensorController aggregation

`SensorController` SHALL own all sensor instances, expose `addSensor(std::unique_ptr<Sensor::Sensor>)`, `readSensors()`, `readSensors(uint32_t nowMs)`, `getMeasurements()`, `getTemperature()`, `getRelativeHumidity()`, `getDewPoint()`, `hasConnectedSensors()`, and `isDataValid()`. When multiple sensors of the same measurement type are present, `getTemperature()` / `getRelativeHumidity()` / `getDewPoint()` SHALL return the value from the first sensor that reported the type, in the order returned by the sorted sensor list. Averaging across sensors is **not** used because a faulty reading (e.g. a wiring fault reporting −40 °C) diluted by a healthy reading would still produce a contaminated value; the defence against bad readings is per-driver range validation at the point of acquisition (see `sensor-management` → "Per-driver range validation"), not averaging.

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

