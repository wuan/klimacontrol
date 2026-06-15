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

### Requirement: Calculated measurements

`SensorController` SHALL emit calculated measurements derived from sensed values. Dew point SHALL be calculated from temperature and relative humidity using the Magnus formula with constants `a = 17.625`, `b = 243.04`. Sea-level pressure SHALL be calculated from absolute pressure and configured elevation using the hypsometric formula.

#### Scenario: Dew point calculation

- **WHEN** both temperature and relative humidity are available
- **THEN** `getDewPoint()` SHALL return the Magnus-formula result and the corresponding measurement SHALL be marked `calculated = true`

#### Scenario: Missing inputs

- **WHEN** temperature is available but relative humidity is not
- **THEN** `getDewPoint()` SHALL return `NAN` and no dew-point measurement SHALL be added to the snapshot

### Requirement: SensorController aggregation

`SensorController` SHALL own all sensor instances, expose `addSensor(std::unique_ptr<Sensor::Sensor>)`, `readSensors()`, `getMeasurements()`, `getTemperature()`, `getRelativeHumidity()`, `getDewPoint()`, `hasConnectedSensors()`, and `isDataValid()`. When multiple sensors of the same measurement type are present, the controller SHALL average their values.

#### Scenario: Two temperature sensors

- **WHEN** two `SHT4x` sensors report temperatures `T1` and `T2`
- **THEN** `getTemperature()` SHALL return `(T1 + T2) / 2`

#### Scenario: No connected sensors

- **WHEN** no sensors are present or all sensors are in `InitFailed` state
- **THEN** `hasConnectedSensors()` SHALL return false and `isDataValid()` SHALL return false

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

