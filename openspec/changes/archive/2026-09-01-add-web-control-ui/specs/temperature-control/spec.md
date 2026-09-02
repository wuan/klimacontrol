# temperature-control Specification Delta

## ADDED Requirements

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

## MODIFIED Requirements

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