# test-temperature-control Specification

## Purpose

Defines requirements for the temperature control test suite that verifies PID algorithm correctness, setpoint handling, and control enable/disable behavior.

## Requirements

### Requirement: PID calculation tests

The test suite SHALL verify the PID control algorithm produces correct output for proportional, integral, and derivative terms.

#### Scenario: Proportional term only

- **WHEN** Kp=1.0, Ki=0, Kd=0, error=2.0, dt=1.0
- **THEN** output SHALL be 2.0 (Kp × error)

#### Scenario: Integral term accumulation

- **WHEN** Kp=0, Ki=1.0, error=1.0, dt=1.0, running sum=0
- **THEN** output SHALL be 1.0 (Ki × error × dt added to sum)

#### Scenario: Derivative term with positive error change

- **WHEN** Kp=0, Ki=0, Kd=1.0, error changes from 1.0 to 2.0, dt=1.0
- **THEN** output SHALL be 1.0 (Kd × Δerror / dt)

#### Scenario: Derivative term guard against dt=0

- **WHEN** Kd=1.0, error changes from 1.0 to 2.0, dt=0
- **THEN** output SHALL be 0 (not NaN or Inf)

### Requirement: Setpoint change tests

The test suite SHALL verify behavior when target temperature changes.

#### Scenario: Setpoint increase

- **WHEN** target temperature changes from 20°C to 25°C
- **THEN** error (target - current) increases accordingly

#### Scenario: Setpoint decrease

- **WHEN** target temperature changes from 25°C to 20°C
- **THEN** error (target - current) decreases accordingly

### Requirement: Control enable/disable tests

The test suite SHALL verify temperature control can be enabled and disabled.

#### Scenario: Disabled control produces zero output

- **WHEN** control is disabled
- **THEN** updateControl() SHALL return 0

#### Scenario: Enabled control produces PID output

- **WHEN** control is enabled with valid sensor data
- **THEN** updateControl() SHALL return computed PID value

### Requirement: Edge cases

The test suite SHALL verify edge cases are handled correctly.

#### Scenario: NaN sensor reading

- **WHEN** sensor returns NAN for temperature
- **THEN** control output SHALL be 0 and dataValid SHALL be false

#### Scenario: Control output saturation

- **WHEN** PID output exceeds configured output range
- **THEN** output SHALL be clamped to min/max bounds