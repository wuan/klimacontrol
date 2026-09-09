# sensor-management Specification

## Purpose
TBD - created by archiving change baseline-capabilities. Update Purpose after archive.
## Requirements
### Requirement: Supported sensor types

The firmware SHALL support the following I2C sensors, each with a dedicated driver class under the `Sensor::` namespace: `SHT4x`, `BME680`, `SGP40`, `BMP3xx`, `SCD4x`, `TSL2591`, `PM25`, `VEML7700`, `DPS310`, `BH1750`. Each driver SHALL inherit from `Sensor::Sensor` and implement `begin()`, `read()`, `isConnected()`, `getType()`, and `getStatus()`.

#### Scenario: Driver registration

- **WHEN** the configuration string references a known sensor type token
- **THEN** the corresponding driver class is instantiated and added to the controller via `addSensor(std::move(sensor))`

#### Scenario: Unknown sensor type

- **WHEN** the configuration string references a sensor type token that is not in the supported set
- **THEN** the firmware logs a warning identifying the unknown token and continues without that sensor (no crash)

### Requirement: Sensor configuration via assignment string

Sensor instances SHALL be described by a compact assignment string of the form `<hex-addr>=<type>[,<hex-addr>=<type>]*` (e.g., `"44=SHT4x,77=BME680"`). The string SHALL be persisted in NVS and parsed at startup. Each entry SHALL accept a 7-bit I2C address in the range `0x08` through `0x77`.

#### Scenario: Parsing two sensors

- **WHEN** the configuration string is `"44=SHT4x,77=BME680"`
- **THEN** an `SHT4x` is instantiated at address `0x44` and a `BME680` at `0x77`, and both are owned by `SensorController`

#### Scenario: Address out of range

- **WHEN** an entry specifies an address below `0x08` or above `0x77`
- **THEN** that entry SHALL be rejected with a warning log and the remaining valid entries SHALL still be applied

### Requirement: Sensor lifecycle status

Each sensor SHALL track a `SensorStatus` with the values `Uninitialized`, `Online`, `InitFailed`, `ReadFailing`. A sensor SHALL start `Uninitialized`. A successful `begin()` SHALL transition it to `Online`; a failed `begin()` SHALL transition it to `InitFailed`. After 10 consecutive read failures while `Online`, the sensor SHALL transition to `ReadFailing`. A subsequent successful read SHALL return it to `Online`.

#### Scenario: First successful initialization

- **WHEN** `tryBegin()` succeeds on a sensor that was `Uninitialized`
- **THEN** the status SHALL transition to `Online`

#### Scenario: Repeated read failures

- **WHEN** a sensor returns a failure result on 10 consecutive `read()` calls while in `Online` state
- **THEN** its status SHALL transition to `ReadFailing` on the 10th failure

#### Scenario: Recovery after transient failure

- **WHEN** a sensor in `ReadFailing` state next returns a successful read
- **THEN** its status SHALL transition back to `Online` and the failure counter SHALL reset

### Requirement: Measurement model

Sensor readings SHALL be expressed as `Sensor::Measurement` records with fields `type` (`Sensor::MeasurementType` enum), `value` (`std::variant<float, int32_t>`), `unit` (`const char*`), `sensor` (`const char*`), and `calculated` (bool). The set of measurement types SHALL include at minimum: `Temperature`, `RelativeHumidity`, `DewPoint`, `Pressure`, `SeaLevelPressure`, `GasResistance`, `CO2`, `Illuminance`, `Particles`, PM concentration variants, `VocIndex`.

#### Scenario: Integer-valued measurement

- **WHEN** a CO2 sensor returns a value
- **THEN** the measurement SHALL hold `std::int32_t` in its `value` variant, with `unit = "ppm"`

#### Scenario: Float-valued measurement

- **WHEN** an SHT4x sensor returns a value
- **THEN** the measurement SHALL hold `float` in its `value` variant, with `unit = "°C"` for temperature and `unit = "%"` for relative humidity

### Requirement: Per-driver range validation

Each sensor driver SHALL validate the plausibility of values returned by its hardware before publishing them as a measurement. A value outside the documented operating range of the physical quantity — for example a temperature below the sensor's lower bound, or any `NaN`/`Inf` float — SHALL be discarded at the driver boundary and SHALL NOT appear in `getMeasurements()`. The exact plausibility bounds are driver-specific (e.g. `SHT4x` temperature plausibly lies in `[−40 °C, +125 °C]`); the rule that they exist and are enforced is what is normative here.

#### Scenario: Temperature sensor returns out-of-range value

- **WHEN** an `SHT4x` driver decodes a frame whose temperature field is `−273.15 °C` (below any physical plausibility bound)
- **THEN** the driver SHALL discard the reading and `getMeasurements()` SHALL NOT contain a temperature measurement for this cycle

#### Scenario: Sensor returns NaN

- **WHEN** a driver decodes a frame whose numeric field is `NaN` or `Inf`
- **THEN** the driver SHALL discard the reading and `getMeasurements()` SHALL NOT contain that measurement type for this cycle

### Requirement: Calculated measurements

`SensorController` SHALL emit calculated measurements derived from sensed values. Dew point SHALL be calculated from temperature and relative humidity using the Magnus formula with constants `a = 17.625`, `b = 243.04`. Sea-level pressure SHALL be calculated from absolute pressure and configured elevation using the hypsometric formula.

#### Scenario: Dew point calculation

- **WHEN** both temperature and relative humidity are available
- **THEN** `getDewPoint()` SHALL return the Magnus-formula result and the corresponding measurement SHALL be marked `calculated = true`

#### Scenario: Missing inputs

- **WHEN** temperature is available but relative humidity is not
- **THEN** `getDewPoint()` SHALL return `NAN` and no dew-point measurement SHALL be added to the snapshot

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

### Requirement: Mutex initialization precondition

`SensorController` SHALL treat a failed `xSemaphoreCreateMutex()` as a fatal init error. On the failure path the controller SHALL:

- log an `ESP_LOGE` line identifying the failure mode (heap exhausted at boot);
- drive the status LED to the `ERROR` state (solid red);
- hold for a short grace period (5 s) so the LED indicator is visible to a human;
- call `ESP.restart()` to give the device a chance to recover from transient boot-time heap pressure.

The previous "log and continue" behavior — where data accessors silently return defaults because `dataMutex` is `nullptr` — is removed.

The `StatusLed*` passed to the constructor MAY be `nullptr` (e.g. in native unit tests); in that case the LED step is skipped and the controller still logs and restarts under `ARDUINO`. The native build (no `ARDUINO` defined) SHALL NOT call `ESP.restart()`; the failure SHALL be observable as a non-null return from the constructor's failure-detection helper for tests to assert on.

#### Scenario: Mutex allocation fails at boot

- **WHEN** the heap is exhausted at boot and `xSemaphoreCreateMutex()` returns `nullptr` from the `SensorController` constructor
- **THEN** an `ESP_LOGE` line is logged, the status LED transitions to the `ERROR` state, the controller holds for 5 s, and `ESP.restart()` is called

#### Scenario: Mutex allocation succeeds at boot

- **WHEN** the heap is healthy and `xSemaphoreCreateMutex()` returns a valid handle from the `SensorController` constructor
- **THEN** no log line is emitted on the failure path, the LED is not driven to `ERROR`, and the controller proceeds with normal initialization

### Requirement: Largest free block is a measurement type

The firmware SHALL include `LargestFreeBlock` in the
`Sensor::MeasurementType` enum, and the value SHALL be sourced
from `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` (the size
of the largest contiguous free block in the 8-bit-capable heap,
in bytes). `measurementTypeLabel(LargestFreeBlock)` SHALL return
`"largest_free_block"` and `measurementTypeUnit(LargestFreeBlock)`
SHALL return `"kB"`. The `DeviceSensor` SHALL list
`LargestFreeBlock` in `providesMeasurements()` and SHALL push a
measurement of that type in `read()` on the same cadence as the
other device-internal metrics.

#### Scenario: Label and unit for the new type

- **WHEN** a caller invokes `measurementTypeLabel(MeasurementType::LargestFreeBlock)`
- **THEN** the result is the string `"largest_free_block"`

#### Scenario: Unit for the new type

- **WHEN** a caller invokes `measurementTypeUnit(MeasurementType::LargestFreeBlock)`
- **THEN** the result is the string `"kB"` (matching the existing
  `FreeHeap` convention; the value stored in the measurement is the
  raw byte count divided by 1024)

#### Scenario: DeviceSensor declares the new type

- **WHEN** a caller invokes `DeviceSensor::providesMeasurements()`
- **THEN** the returned `TypeSpan` includes `MeasurementType::LargestFreeBlock`
  in its list

#### Scenario: DeviceSensor pushes the measurement on each read

- **WHEN** the firmware is running on the device and
  `DeviceSensor::read()` is called as part of a normal sensor cycle
- **THEN** the returned `SensorReading` contains a measurement of
  type `LargestFreeBlock` whose value (in kB) is the largest free
  8-bit-capable heap block at the moment of the call

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

`SensorController::readSensors(uint32_t nowMs)` SHALL read a sensor on a given call only if it is `Online` and due. A sensor with `requiredIntervalMs() == 0` SHALL be due when the shared default phase is due, which is the first call ever and every call for which `nowMs - lastDefaultCycleMs >= MEASUREMENT_INTERVAL_MS`. A sensor with `requiredIntervalMs() == N > 0` SHALL be due when it has never been read or `nowMs - lastReadMs >= N`, where `lastReadMs` is that sensor's own last attempted read. All default-interval sensors SHALL be read on the same call, so that their readings are time-coherent. `readSensors()` without arguments SHALL behave as `readSensors(millis())`. The Sensor Monitor task tick SHALL be chosen as specified in "Sensor Monitor tick follows the fastest configured sensor"; it SHALL NOT be a settable fixed interval.

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

### Requirement: Sensor Monitor tick follows the fastest configured sensor

`SensorController` SHALL provide `uint32_t minReadIntervalMs() const`, returning the minimum over every configured sensor of its effective read interval (`requiredIntervalMs()` when non-zero, otherwise `MEASUREMENT_INTERVAL_MS`), or `MEASUREMENT_INTERVAL_MS` when no sensor is configured. Sensor status SHALL NOT affect the result: a sensor that is `InitFailed` or `ReadFailing` SHALL contribute its interval, because it is retried inside `readSensors()` and must find the tick already running at its rate when it comes online.

The Sensor Monitor task SHALL compute its tick once, when the task starts and after every sensor has been registered, as `minReadIntervalMs()` clamped to the closed range `[MIN_TICK_MS, MEASUREMENT_INTERVAL_MS]`, and SHALL log the chosen value. After each iteration it SHALL sleep for `tick - elapsed + WAKE_MARGIN_MS` when the iteration's work took less than the tick, otherwise for one RTOS tick, where `WAKE_MARGIN_MS` is a small positive constant that makes the wake strictly later than the interval so an early RTOS wake cannot leave the default phase short of due. The task SHALL NOT expose a settable fixed reading interval. `Control::TemperatureController::update()` SHALL still be invoked on every iteration.

#### Scenario: Only default sensors configured

- **WHEN** every configured sensor returns `requiredIntervalMs() == 0`
- **THEN** `minReadIntervalMs()` SHALL return `MEASUREMENT_INTERVAL_MS` and the task SHALL tick every `MEASUREMENT_INTERVAL_MS`

#### Scenario: A one-hertz sensor sets the tick

- **WHEN** a default-interval sensor and a sensor returning `requiredIntervalMs() == 1000` are configured
- **THEN** `minReadIntervalMs()` SHALL return `1000`

#### Scenario: An offline sensor still counts

- **WHEN** the only sensor returning `requiredIntervalMs() == 1000` is `ReadFailing` and a default-interval sensor is `Online`
- **THEN** `minReadIntervalMs()` SHALL return `1000`

#### Scenario: No sensors configured

- **WHEN** no sensor has been added
- **THEN** `minReadIntervalMs()` SHALL return `MEASUREMENT_INTERVAL_MS`, and the task SHALL keep ticking at that rate so init retries and the control loop's skipped-tick bookkeeping run

#### Scenario: Early RTOS wake does not skip a cycle

- **WHEN** the task's `vTaskDelay` returns one RTOS tick before the nominal sleep has elapsed
- **THEN** because of `WAKE_MARGIN_MS` the next `readSensors(millis())` SHALL still find the default phase due, and no 15 s cycle SHALL be skipped

