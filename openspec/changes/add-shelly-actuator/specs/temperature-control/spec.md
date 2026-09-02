# temperature-control Specification Delta

## MODIFIED Requirements

### Requirement: Safety limits

The controller SHALL implement an over-temperature shutoff. The limit SHALL be stored in `DeviceConfig`, persisted, and default to a value above any plausible setpoint. When the measured temperature exceeds it, the controller SHALL produce zero demand and the actuator SHALL be commanded closed, regardless of the error term, until the temperature falls below the limit less a hysteresis band.

The shutoff SHALL be evaluated before the PID computation, so that a saturated integral cannot override it, and SHALL also engage when no valid temperature reading is available, because an unknown temperature is not a safe basis for delivering heat.

#### Scenario: Over-temperature shutoff

- **WHEN** the measured temperature exceeds the configured upper safety limit
- **THEN** demand SHALL be zero and the actuator SHALL be commanded closed

#### Scenario: Shutoff releases with hysteresis

- **WHEN** the temperature falls back below the limit by less than the hysteresis band
- **THEN** the shutoff SHALL remain engaged, so the valve does not chatter at the threshold

#### Scenario: Shutoff precedes the PID computation

- **WHEN** the safety limit is exceeded while the integral term is saturated
- **THEN** demand SHALL still be zero

#### Scenario: Unknown temperature is not safe

- **WHEN** no valid temperature reading is available
- **THEN** demand SHALL be zero and the actuator SHALL be commanded closed

### Requirement: Control active state

`isControlActive()` SHALL report whether the actuator is confirmed to be heating, rather than whether the last computed output was greater than zero. Under time-proportional output a non-zero demand spends part of each cycle closed, and with a remote actuator the firmware's command is a belief until the relay confirms it.

Confirmation SHALL require both the relay's reported contact state and its measured power draw to agree with the command. When observations are stale or failing, the state SHALL be reported as unknown rather than as the last believed value.

#### Scenario: Active only when confirmed

- **WHEN** the valve is commanded open and the relay confirms a closed contact with a power draw consistent with an actuator
- **THEN** `isControlActive()` SHALL return `true`

#### Scenario: Inactive during the closed interval

- **WHEN** the duty is non-zero and the cycle is in its closed interval
- **THEN** `isControlActive()` SHALL return `false`

#### Scenario: Unknown rather than assumed

- **WHEN** the actuator has not been observed within the observation timeout
- **THEN** the state SHALL be reported as unknown, and displays SHALL NOT show it as heating
