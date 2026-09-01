# heating-actuator Specification Delta

## ADDED Requirements

### Requirement: Physical output pin

The firmware SHALL drive exactly one GPIO as the heating demand output, switching an external relay or solid-state relay that energises the zone valve actuator. The pin assignment SHALL be a compile-time constant guarded by `static_assert` against collision with the e-paper panel pins, the STEMMA QT bus, the NeoPixel pins and the USB pins, following the precedent set by `src/display/DisplayPins.h`. The pin SHALL NOT be a strapping pin, whose level during reset is not under firmware control.

#### Scenario: Pin collision fails the build

- **WHEN** the actuator pin is assigned a GPIO already used by the display, the sensor bus or the status LED
- **THEN** the build SHALL fail on a `static_assert` rather than producing firmware that contends for the pin

#### Scenario: Assignment is not runtime-configurable

- **WHEN** a user attempts to change the actuator pin
- **THEN** no API or UI SHALL expose it, because a bad value could both break the web UI needed to correct it and hold a valve open

### Requirement: Boot-safe and failsafe output state

The output SHALL be wired active-high into a normally-closed valve, so that an undriven pin corresponds to a closed valve. The firmware SHALL drive the pin LOW during initialisation before the control loop starts, and SHALL leave the valve closed in every condition in which it is not deliberately calling for heat.

#### Scenario: Valve closed at boot

- **WHEN** the device powers on or resets
- **THEN** the actuator pin SHALL be driven LOW before the Sensor Monitor task begins, and the valve SHALL be closed

#### Scenario: Valve closed after a crash

- **WHEN** the firmware panics or the watchdog resets the device
- **THEN** the undriven pin SHALL de-energise the relay and the normally-closed valve SHALL close without firmware involvement

#### Scenario: Valve closed when control is disabled

- **WHEN** `temperature_control_enabled` is `false`
- **THEN** the output SHALL be off and the valve SHALL be closed

#### Scenario: Valve closed on sensor loss

- **WHEN** sensor data is invalid or the temperature reading is NaN
- **THEN** the output SHALL be off and the valve SHALL be closed

### Requirement: Time-proportional output

The controller output in the range `[0.0, 1.0]` SHALL be converted to valve open time by time-proportional output over a configurable cycle period. Within each cycle the valve SHALL be commanded open for `duty × T_cycle` and closed for the remainder. The duty SHALL be latched at the start of each cycle and SHALL NOT be re-evaluated mid-cycle, so that the fraction of the cycle spent open corresponds to the commanded duty.

#### Scenario: Duty translated to open time

- **WHEN** the controller output is `0.30` and the cycle period is 15 minutes
- **THEN** the valve SHALL be commanded open for 4.5 minutes and closed for 10.5 minutes

#### Scenario: Duty latched for the cycle

- **WHEN** the controller output changes from `0.30` to `0.60` part-way through a cycle
- **THEN** the current cycle SHALL complete on the latched `0.30`
- **AND** the following cycle SHALL use `0.60`

#### Scenario: Zero duty

- **WHEN** the controller output is `0.0`
- **THEN** the valve SHALL remain closed for the whole cycle and no switching SHALL occur

#### Scenario: Full duty

- **WHEN** the controller output is `1.0`
- **THEN** the valve SHALL remain open for the whole cycle and no switching SHALL occur

### Requirement: Minimum dwell honours valve travel time

The cycle period SHALL be validated to be at least four times the configured actuator travel time, and SHALL default to 15 minutes. Duties that would command an open or closed interval shorter than the actuator travel time SHALL be snapped to fully closed or fully open for that cycle, because a valve asked to perform a stroke it cannot complete delivers an amount of heat unrelated to the commanded duty.

#### Scenario: Very low duty snaps to closed

- **WHEN** the duty would command an open interval shorter than the actuator travel time
- **THEN** the valve SHALL remain closed for the whole cycle

#### Scenario: Very high duty snaps to open

- **WHEN** the duty would command a closed interval shorter than the actuator travel time
- **THEN** the valve SHALL remain open for the whole cycle

#### Scenario: Cycle period too short is rejected

- **WHEN** a cycle period shorter than four times the actuator travel time is requested
- **THEN** the request SHALL be rejected and the stored cycle period SHALL remain unchanged

### Requirement: Valve state is the reported control activity

`isControlActive()` SHALL report whether the valve is currently commanded open, replacing the previous definition of "the last computed output was greater than zero". The dashboard control symbol, the `/api/status` `control_active` field and the e-paper footer symbol SHALL all reflect valve state.

#### Scenario: Active while the valve is open

- **WHEN** the duty is `0.30` and the cycle is in its open interval
- **THEN** `isControlActive()` SHALL return `true`

#### Scenario: Inactive during the closed interval of a non-zero duty

- **WHEN** the duty is `0.30` and the cycle is in its closed interval
- **THEN** `isControlActive()` SHALL return `false`, because the valve is shut even though the controller is calling for heat