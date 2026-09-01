# temperature-control Specification

## Purpose
TBD - created by archiving change baseline-capabilities. Update Purpose after archive.
## Requirements
### Requirement: Enable/disable state

The firmware SHALL persist a boolean `temperature_control_enabled` in `DeviceConfig`. The default on a freshly provisioned device SHALL be disabled.

#### Scenario: Default-off

- **WHEN** a freshly provisioned device boots for the first time
- **THEN** `DeviceConfig.temperature_control_enabled` SHALL be `false` and the controller SHALL produce no output

#### Scenario: Toggling via API

- **WHEN** `POST /api/control/enable` is sent
- **THEN** `temperature_control_enabled` SHALL transition to `true` and SHALL be persisted to NVS

### Requirement: Control active state

The firmware SHALL track whether the temperature control is actively producing non-zero output. The `SensorController` SHALL store the last computed control output value and expose a method `isControlActive()` that returns `true` when the last output was greater than `0.0`. The stored output SHALL be updated on every call to `updateControl()`.

#### Scenario: Control active when heating

- **WHEN** temperature is below setpoint and control is enabled
- **THEN** `isControlActive()` SHALL return `true`

#### Scenario: Control inactive when at setpoint

- **WHEN** temperature equals setpoint and control is enabled
- **THEN** `isControlActive()` SHALL return `false`

#### Scenario: Control inactive when disabled

- **WHEN** control is disabled via `temperature_control_enabled = false`
- **THEN** `isControlActive()` SHALL return `false`

#### Scenario: Control inactive with invalid data

- **WHEN** sensor data is invalid or unavailable
- **THEN** `isControlActive()` SHALL return `false`

#### Scenario: Control active state persists between cycles

- **WHEN** `updateControl()` is called and produces output `0.5`
- **AND** `isControlActive()` is called before the next `updateControl()`
- **THEN** `isControlActive()` SHALL return `true`

### Requirement: Setpoint range

The firmware SHALL store the target temperature in
`DeviceConfig.target_temperature`. The default SHALL be `22.0` °C. Setpoints
SHALL be validated against the range `[10.0, 30.0]` °C and out-of-range
setpoints SHALL be rejected at the point of request, leaving the stored value
unchanged.

Validation exists at two further layers with distinct roles, and they SHALL NOT
be treated as interchangeable:

- `SensorController::setTargetTemperature()` clamps to `[10.0, 30.0]`. This is
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

The control loop SHALL run inside the Sensor Monitor task on each sensor read cycle (1-second cadence by default). On each iteration the controller SHALL call `updateControl()`. The call SHALL be skipped when control is disabled or when no valid sensor data is available; in those cases the controller's effective output SHALL be `0.0`.

#### Scenario: No valid data

- **WHEN** all sensors are in `InitFailed` or `ReadFailing` state
- **THEN** `updateControl()` SHALL not be invoked and the controller output SHALL be reported as `0.0`

### Requirement: PID algorithm

The controller SHALL implement a PID algorithm. On each call: `error = setpoint − process_variable`, where `process_variable` is the current averaged temperature from `SensorController::getTemperature()`. The output SHALL be `P + I + D` where `P = Kp · error`, `I = Ki · integral(error)`, `D = Kd · derivative(error)`. The output SHALL be clamped to `[0.0, 1.0]`.

#### Scenario: At setpoint

- **WHEN** the current temperature exactly matches the setpoint
- **THEN** `error = 0`, `P = 0`, and the resulting output SHALL be `0.0` (modulo any decaying integral term)

#### Scenario: Below setpoint

- **WHEN** the current temperature is below the setpoint by 2.0 °C
- **THEN** the output SHALL be positive and clamped to no more than `1.0`

### Requirement: PID parameter configurability

The gains `Kp`, `Ki`, `Kd` SHALL be tunable. Defaults SHALL produce a stable response on the target hardware.

#### Scenario: Tuning gains

- **WHEN** new gain values are loaded
- **THEN** subsequent `updateControl()` calls SHALL use the new values

### Requirement: Anti-windup

The integral term SHALL be clamped to the output range to prevent windup when the controller is saturated.

#### Scenario: Saturated controller

- **WHEN** the output is saturated at `1.0` for an extended period due to a persistent large error
- **THEN** the integral term SHALL be clamped so that, upon returning to the setpoint, the integral does not cause excessive overshoot

### Requirement: Safety limits

The controller SHALL implement maximum-output and maximum/minimum-temperature safety limits. If the measured temperature exceeds the configured safety threshold, the controller SHALL stop driving any output.

#### Scenario: Over-temperature shutoff

- **WHEN** the measured temperature exceeds the configured upper safety limit
- **THEN** `updateControl()` SHALL produce `0.0` regardless of the error term, until the temperature returns within limits

### Requirement: Bumpless controller restart

The PID state SHALL be instance state of the controller rather than
function-local `static` storage, so that it is not shared between controller
instances. That state comprises the integral accumulator, the previous error,
and the timestamp of the last computation.

`updateControl()` SHALL detect its own resumption: whenever a tick performs a
full computation and the immediately preceding tick did not (because control was
disabled, because sensor data was invalid, or because it is the first tick after
boot), the controller SHALL reset the integral accumulator and the previous
error to zero and SHALL reseat the last-computation timestamp to the current
time before computing.

This resumption check SHALL be performed inside `updateControl()`, on the task
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

- **WHEN** `updateControl()` performs its first full computation after boot
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
- **THEN** the Sensor Monitor task SHALL still call `updateControl()` on each
  sensor tick so it can mark the tick as skipped
- **AND** the caller SHALL NOT wrap the call in its own enabled-check, because
  skipping the call entirely leaves the controller believing it is still running
  and charges its integral with the whole disabled duration on resumption

#### Scenario: Enable does not write PID state from the web task

- **WHEN** `POST /api/control/enable` is handled on the web-server task
- **THEN** the handler SHALL write only configuration state
- **AND** the PID accumulators SHALL be reset by the control loop on its next
  tick, not by the handler

