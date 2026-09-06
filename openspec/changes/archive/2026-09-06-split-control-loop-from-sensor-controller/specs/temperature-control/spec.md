## ADDED Requirements

### Requirement: Control loop is decoupled from sensor acquisition

The heating control loop SHALL be implemented by `Control::TemperatureController` (`src/control/`), a class that holds no reference to `SensorController` and performs no sensor access of its own. Its process value SHALL be supplied by the caller as parameters of `update(float temperature, bool valid, uint32_t nowMs)`, where `temperature` is the current temperature or `NAN`, `valid` is the sensor cache's validity flag, and `nowMs` is the caller's clock. The Sensor Monitor task SHALL obtain both input values from one `SensorController::getProcessValue()` call so that temperature and validity describe the same instant.

`isHeatingPermitted()` SHALL be answered from the inputs of the most recent `update()` call rather than from a live sensor read. It SHALL return `false` before the first `update()` after boot. Its staleness is therefore bounded by the Sensor Monitor cadence (one second by default); the over-temperature shutoff engages on the same tick that observes invalid input, so the actuator's behaviour is unchanged by this bound.

The class SHALL be constructible and fully exercisable in the native build with only a `Config::ConfigManager`, so that control-loop behaviour is tested against the shipped code rather than a re-implementation.

#### Scenario: Loop runs without any sensor object

- **WHEN** a native test constructs `Control::TemperatureController` with a `ConfigManager` and calls `update(18.0f, true, 1000)` with control enabled and a setpoint of 22 °C
- **THEN** the call SHALL compute an output greater than zero without any `SensorController` or `Sensor::Sensor` having been constructed

#### Scenario: Process value is one consistent pair

- **WHEN** the Sensor Monitor task hands the loop its input on a tick
- **THEN** temperature and validity SHALL come from a single lock acquisition of the sensor cache, not from separate `getTemperature()` and `isDataValid()` calls

#### Scenario: Heating is not permitted before the first tick

- **WHEN** the device has booted with control enabled and the Sensor Monitor task has not yet called `update()`
- **THEN** `isHeatingPermitted()` SHALL return `false`

#### Scenario: Heating permission follows the last tick's inputs

- **WHEN** the previous `update()` was called with `valid = false` and the Network task calls `isHeatingPermitted()` before the next tick
- **THEN** it SHALL return `false`, even if the sensor cache has become valid in the meantime

## MODIFIED Requirements

### Requirement: Setpoint range

The firmware SHALL store the target temperature in
`DeviceConfig.target_temperature`. The default SHALL be `22.0` °C. Setpoints
SHALL be validated against the range `[10.0, 30.0]` °C and out-of-range
setpoints SHALL be rejected at the point of request, leaving the stored value
unchanged.

Validation exists at two further layers with distinct roles, and they SHALL NOT
be treated as interchangeable:

- `Control::TemperatureController::setTargetTemperature()` clamps to `[10.0, 30.0]`. This is
  the last line of defence for non-HTTP callers, such as restoring a value from
  NVS at boot.
- `Config::updateTargetTemperature()` substitutes the `22.0` °C default. This
  guards against a corrupt or absent NVS value, not against user input.

#### Scenario: Valid setpoint

- **WHEN** the user requests a setpoint of `23.5` °C
- **THEN** the controller's target SHALL become `23.5` and the value SHALL be
  persisted

#### Scenario: Invalid setpoint

- **WHEN** the user requests a setpoint of `40.0` °C
- **THEN** the request SHALL be rejected and the controller's target SHALL
  remain unchanged

#### Scenario: Corrupt persisted setpoint

- **WHEN** a value outside `[10.0, 30.0]` is read from NVS at boot
- **THEN** the effective target SHALL fall back to `22.0` °C rather than being
  clamped to the nearest bound

### Requirement: Control loop scheduling

The control loop SHALL run inside the Sensor Monitor task on each sensor read cycle (1-second cadence by default). On each iteration the task SHALL read the sensors, take one `SensorController::getProcessValue()`, and call `Control::TemperatureController::update(temperature, valid, nowMs)` with the result. When control is disabled or no valid sensor data is available the call SHALL still be made, with `valid = false` in the latter case, and the controller's effective output SHALL be `0.0`.

`update()` SHALL be invoked on every sensor read cycle regardless of the control interval, so that a cycle on which the loop declines to compute — because control is disabled, no valid reading exists, the over-temperature shutoff is engaged, or an autotune run owns the output — is still marked as a skipped tick. Decimating the invocation rather than the computation SHALL NOT be done, because the next computing tick would then measure an elapsed time spanning the whole gap and saturate the integral term on that tick.

A cycle on which the PID merely does not compute *because the control interval has not yet elapsed* SHALL NOT be marked as skipped. Marking it would make every computation a bumpless restart, so the integral accumulator would be discarded before it could ever carry from one computation to the next and `Ki` would have no effect at any configured value. Such a cycle SHALL leave the accumulated controller state untouched, and the elapsed time the next computation measures SHALL be the real interval since the last computation.

The PID computation SHALL be decimated to a configurable control interval, defaulting to 60 seconds, because a plant whose time constant is measured in hours does not benefit from a 1-second loop and the derivative term at that cadence responds mostly to sensor noise. The interval SHALL be a lower bound on the spacing between computations rather than a schedule: a late tick SHALL compute late, and the elapsed time SHALL be measured rather than assumed. Interval arithmetic SHALL remain correct across the `millis()` rollover. The clock SHALL be the `nowMs` parameter, never an internal `millis()` read, so cadence is testable in the native build.

The following SHALL NOT be decimated, and SHALL be evaluated on every sensor read cycle:

- The over-temperature shutoff, because a safety limit observed up to a full control interval late is a weaker guarantee than the safety-limits requirement describes.
- The autotuner's update while a run is active, because it measures the amplitude of an induced oscillation and coarser sampling biases that measurement.

#### Scenario: No valid data

- **WHEN** all sensors are in `InitFailed` or `ReadFailing` state
- **THEN** `update()` SHALL still be invoked with `valid = false` and the controller output SHALL be reported as `0.0`

#### Scenario: PID computes on the control interval

- **WHEN** the control interval is 60 seconds and sensors read every second
- **THEN** the PID SHALL compute approximately once per 60 sensor reads

#### Scenario: Skipped ticks are still marked

- **WHEN** a sensor tick occurs on which the loop declines to compute because control is disabled, no valid reading exists, the shutoff is engaged, or a run is active
- **THEN** `update()` SHALL still be invoked, the tick SHALL be marked as skipped, and the controller SHALL NOT subsequently measure an elapsed time spanning the skipped ticks

#### Scenario: A merely decimated tick is not a skipped tick

- **WHEN** a sensor tick occurs on which the PID does not compute only because the control interval has not yet elapsed
- **THEN** the controller SHALL NOT be suspended, and the integral accumulated so far SHALL survive to the next computation

#### Scenario: Integral action survives decimation

- **WHEN** the controller runs for several control intervals against a sustained error with a non-zero `Ki`
- **THEN** the integral term SHALL have accumulated, rather than having been reset on each computation

#### Scenario: Safety shutoff is not delayed by the control interval

- **WHEN** the temperature crosses the over-temperature limit with a 60-second control interval configured
- **THEN** the shutoff SHALL engage within one sensor read cycle rather than waiting for the next PID computation

#### Scenario: Autotune sampling is not decimated

- **WHEN** an autotune run is active with a 60-second control interval configured
- **THEN** the autotuner SHALL be updated on every sensor read cycle

#### Scenario: Interval survives the rollover

- **WHEN** the control interval elapses across the `millis()` rollover
- **THEN** the PID SHALL compute on schedule rather than stalling for the length of the counter

#### Scenario: Cadence is testable natively

- **WHEN** a native test calls `update()` with a 60 s control interval at `nowMs = 1000, 2000, …, 121000`
- **THEN** the PID SHALL have computed exactly twice (at 60 s and at 120 s: the decimation baseline starts at zero, so the first computation is one interval after boot rather than on the first tick), observable through `isControlRunning()` and the returned output

### Requirement: PID algorithm

The controller SHALL implement a PID algorithm. On each call: `error = setpoint − process_variable`, where `process_variable` is the temperature passed to `update()` by the Sensor Monitor task (the first temperature measurement in the sensor cache, see `sensor-management`). The output SHALL be `P + I + D` where `P = Kp · error`, `I = Ki · integral(error)`, `D = Kd · derivative(error)`. The output SHALL be clamped to `[0.0, 1.0]`.

#### Scenario: At setpoint

- **WHEN** the current temperature exactly matches the setpoint
- **THEN** `error = 0`, `P = 0`, and the resulting output SHALL be `0.0` (modulo any decaying integral term)

#### Scenario: Below setpoint

- **WHEN** the current temperature is below the setpoint by 2.0 °C
- **THEN** the output SHALL be positive and clamped to no more than `1.0`

### Requirement: PID parameter configurability

The gains `Kp`, `Ki` and `Kd` SHALL be stored in `DeviceConfig`, persisted to NVS, and validated on load. Defaults SHALL produce a stable response on the target hardware and SHALL be consistent with the tuning method the firmware ships: because the autotuner derives a PI controller with `Kd = 0` by Tyreus–Luyben, the default `Kd` SHALL be zero and the default `Ki` SHALL be of an order suited to a plant whose time constant is hours rather than seconds.

A gain change SHALL be treated as a discontinuity: the controller SHALL be suspended so that the next tick restarts bumplessly, because an integral accumulated under the old gains does not mean the same thing under the new ones. Reusing the bumpless-restart path SHALL be preferred to introducing a second way to reset controller state.

Gains SHALL be applied to the running controller only by the task that owns it. A gain change originating on another task SHALL be handed over as a request consumed on the control task, rather than written directly into controller state, so that the controller remains single-writer.

`Kp` SHALL NOT be permitted to be zero, because a zero proportional gain disables control while control still reports itself as enabled. `Ki` and `Kd` MAY be zero.

#### Scenario: Tuning gains

- **WHEN** new gain values are loaded
- **THEN** subsequent `update()` calls SHALL use the new values

#### Scenario: Gains survive a restart

- **WHEN** gains are stored and the device restarts
- **THEN** the stored gains SHALL be in force rather than the compiled-in defaults

#### Scenario: A gain change suspends the controller

- **WHEN** any gain is changed while the controller is running
- **THEN** the controller SHALL be suspended and the next computing tick SHALL restart bumplessly with a zero integral

#### Scenario: A cross-task gain change is deferred

- **WHEN** a gain change is requested from the web server task
- **THEN** it SHALL be applied by the control task on a subsequent tick, and the requesting task SHALL NOT write controller state directly

#### Scenario: Invalid gains fall back rather than refusing to boot

- **WHEN** a persisted gain is absent, non-finite or outside its documented range
- **THEN** that field SHALL fall back to its default and the device SHALL boot normally

#### Scenario: Zero proportional gain is refused

- **WHEN** a `Kp` of zero is submitted
- **THEN** it SHALL be rejected and the stored gains SHALL be unchanged

### Requirement: Bumpless controller restart

The PID state SHALL be instance state of the controller rather than
function-local `static` storage, so that it is not shared between controller
instances. That state comprises the integral accumulator, the previous error,
and the timestamp of the last computation.

`update()` SHALL detect its own resumption: whenever a tick performs a
full computation and the immediately preceding tick did not (because control was
disabled, because sensor data was invalid, or because it is the first tick after
boot), the controller SHALL reset the integral accumulator and the previous
error to zero and SHALL reseat the last-computation timestamp to the current
time before computing.

This resumption check SHALL be performed inside `update()`, on the task
that owns the control loop. Other tasks SHALL NOT write PID state;
`setControlEnabled()` SHALL remain a configuration write only. This keeps the
PID state single-writer and free of the read-modify-write race that would arise
if the web-server task reset it concurrently with a control tick.

#### Scenario: Resuming after a long disabled period

- **WHEN** temperature control has been disabled for one hour
- **AND** control is re-enabled while the measured temperature is 0.1 °C below
  the setpoint
- **THEN** the elapsed disabled time SHALL NOT be applied as `dt` to the
  integral term
- **AND** the first computed output SHALL be proportional to the 0.1 °C error
  rather than saturated at the maximum output

#### Scenario: Resuming after a sensor dropout

- **WHEN** sensor data has been invalid for five minutes while control remained
  enabled
- **AND** valid sensor data returns
- **THEN** the controller SHALL reset its integral accumulator and previous
  error and reseat its timestamp before computing
- **AND** the output SHALL NOT be saturated by the dropout duration

#### Scenario: First tick after boot

- **WHEN** `update()` performs its first full computation after boot
- **THEN** the uptime at that moment SHALL NOT be applied as `dt`

#### Scenario: Consecutive running ticks are unaffected

- **WHEN** two successive ticks both perform a full computation
- **THEN** the second SHALL NOT reset the integral accumulator, and `dt` SHALL
  be the interval between the two ticks

#### Scenario: First output after resumption is proportional only

- **WHEN** a tick resets the PID state on resumption
- **THEN** `dt` for that tick SHALL be zero, the integral increment SHALL be
  zero, and the derivative term SHALL be zero
- **AND** the output SHALL equal the clamped proportional term

#### Scenario: The control loop is invoked on every tick

- **WHEN** temperature control is disabled
- **THEN** the Sensor Monitor task SHALL still call `update()` on each
  sensor tick so it can mark the tick as skipped
- **AND** the caller SHALL NOT wrap the call in its own enabled-check, because
  skipping the call entirely leaves the controller believing it is still running
  and charges its integral with the whole disabled duration on resumption

#### Scenario: Enable does not write PID state from the web task

- **WHEN** `POST /api/control/enable` is handled on the web-server task
- **THEN** the handler SHALL write only configuration state
- **AND** the PID accumulators SHALL be reset by the control loop on its next
  tick, not by the handler
