# Spec Delta

## ADDED Requirements

### Requirement: Control state is republished on every control-loop computation

The firmware SHALL republish the thermostat's full control and actuator state — setpoint, control-enabled flag, PID output and confirmed heating — on every control-loop computation, once per configured control interval. This SHALL be independent of the publish-on-change behaviour and of the sensor measurement interval, so a subscriber observes the setpoint and demand of each run even when nothing changed.

#### Scenario: Full set republished each computation

- **WHEN** the control loop completes a PID computation
- **THEN** all four control-state topics SHALL be published, including `{prefix}/target_temperature`, even if their values are unchanged since the previous publish

#### Scenario: Republish cadence follows the control interval

- **WHEN** the control interval is 60 seconds and the control loop computes at 60-second spacing
- **THEN** the control-state set SHALL be published approximately once per minute

#### Scenario: Not gated by the measurement interval or the settle period

- **WHEN** a control-loop computation occurs before the sensor measurement interval has elapsed, or within 60 seconds of boot
- **THEN** the control-state set SHALL still be published
