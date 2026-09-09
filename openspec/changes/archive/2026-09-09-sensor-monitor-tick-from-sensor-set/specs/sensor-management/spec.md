# sensor-management Specification Delta

## MODIFIED Requirements

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

## ADDED Requirements

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
