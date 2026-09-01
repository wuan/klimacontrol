# temperature-control Specification Delta

## MODIFIED Requirements

### Requirement: PID parameter configurability

The gains `Kp`, `Ki` and `Kd` SHALL be stored in `DeviceConfig`, persisted to NVS, and adjustable at runtime through the HTTP API and the web UI. They SHALL NOT be compile-time constants. Each SHALL be validated against a documented range and out-of-range values SHALL be rejected rather than clamped, following the convention established for the setpoint. Defaults SHALL be conservative values suitable for an underfloor heating plant rather than the fast-response values previously compiled in.

Changing a gain at runtime SHALL be treated as a discontinuity: the controller SHALL suspend, so that the next tick restarts bumplessly rather than carrying an integral accumulated under different gains.

#### Scenario: Tuning gains

- **WHEN** new gain values are stored
- **THEN** subsequent `updateControl()` calls SHALL use the new values
- **AND** the values SHALL survive a reboot

#### Scenario: Gain change resets the accumulator

- **WHEN** `Ki` is changed while control is running
- **THEN** the controller SHALL suspend and the next tick SHALL restart from a zeroed integral

#### Scenario: Out-of-range gain rejected

- **WHEN** a gain outside its documented range is submitted
- **THEN** the request SHALL be rejected and the stored gains SHALL remain unchanged

### Requirement: Safety limits

The controller SHALL implement an over-temperature shutoff. The limit SHALL be stored in `DeviceConfig`, persisted, and default to a value above any plausible setpoint. When the measured temperature exceeds it, `updateControl()` SHALL produce `0.0` and the actuator SHALL close the valve, regardless of the error term, until the temperature returns below the limit less a hysteresis band. The shutoff SHALL be evaluated before the PID computation, so that a saturated integral cannot override it.

The shutoff SHALL also engage when the temperature reading is unavailable, because an unknown temperature is not a safe basis for delivering heat.

#### Scenario: Over-temperature shutoff

- **WHEN** the measured temperature exceeds the configured upper safety limit
- **THEN** `updateControl()` SHALL produce `0.0` regardless of the error term
- **AND** the valve SHALL be closed

#### Scenario: Shutoff releases with hysteresis

- **WHEN** the temperature falls back below the safety limit but by less than the hysteresis band
- **THEN** the shutoff SHALL remain engaged, so the output does not chatter at the threshold

#### Scenario: Shutoff precedes the PID computation

- **WHEN** the safety limit is exceeded while the integral term is saturated
- **THEN** the output SHALL still be `0.0`

#### Scenario: Unknown temperature is not safe

- **WHEN** no valid temperature reading is available
- **THEN** the output SHALL be `0.0` and the valve SHALL be closed

### Requirement: Control loop scheduling

The control loop SHALL run inside the Sensor Monitor task. Sensors SHALL continue to be read on the existing 1-second cadence, but the PID computation SHALL be decimated to a configurable control interval defaulting to 60 seconds, because a plant with an hours-long time constant sampled every second integrates sensor noise rather than process error.

`updateControl()` SHALL continue to be invoked on every sensor tick regardless of whether control is enabled, so that it can mark skipped ticks and preserve bumpless restart. Decimation SHALL apply to the PID computation, not to the gating; an outer guard that skips the call entirely would leave the controller believing it was still running and charge its integral with the whole elapsed gap on resumption.

#### Scenario: Decimated computation

- **WHEN** the control interval is 60 seconds and sensors read every second
- **THEN** the PID SHALL compute once per 60 sensor ticks

#### Scenario: Skipped ticks still marked

- **WHEN** control is disabled
- **THEN** `updateControl()` SHALL still be called on each sensor tick and SHALL mark the controller suspended

#### Scenario: No valid data

- **WHEN** all sensors are in `InitFailed` or `ReadFailing` state
- **THEN** the controller output SHALL be `0.0` and the valve SHALL be closed

### Requirement: Control active state

The firmware SHALL track whether the heating output is currently energising the valve, and `isControlActive()` SHALL report that valve state rather than whether the last computed PID output was greater than zero. Under time-proportional output a non-zero duty spends part of each cycle closed, and reporting the controller's intent instead of the valve's state would show the device as heating while the valve is shut.

#### Scenario: Control active while the valve is open

- **WHEN** the duty is non-zero and the cycle is in its open interval
- **THEN** `isControlActive()` SHALL return `true`

#### Scenario: Control inactive during the closed interval

- **WHEN** the duty is non-zero and the cycle is in its closed interval
- **THEN** `isControlActive()` SHALL return `false`

#### Scenario: Control inactive when disabled

- **WHEN** control is disabled via `temperature_control_enabled = false`
- **THEN** `isControlActive()` SHALL return `false`

#### Scenario: Control inactive with invalid data

- **WHEN** sensor data is invalid or unavailable
- **THEN** `isControlActive()` SHALL return `false`