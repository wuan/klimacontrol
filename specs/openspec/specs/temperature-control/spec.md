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

### Requirement: Setpoint range

The firmware SHALL store the target temperature in `DeviceConfig.target_temperature`. The default SHALL be `22.0` °C. Setpoints SHALL be validated against the range `[10.0, 30.0]` °C and out-of-range setpoints SHALL be rejected.

#### Scenario: Valid setpoint

- **WHEN** the user requests a setpoint of `23.5` °C
- **THEN** the controller's target SHALL become `23.5` and the value SHALL be persisted

#### Scenario: Invalid setpoint

- **WHEN** the user requests a setpoint of `40.0` °C
- **THEN** the request SHALL be rejected and the controller's target SHALL remain unchanged

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

