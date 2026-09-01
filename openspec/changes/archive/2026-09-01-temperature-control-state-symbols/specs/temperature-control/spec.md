# temperature-control Specification Delta

## ADDED Requirements

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
