# http-api Specification Delta

## ADDED Requirements

### Requirement: Control parameters endpoint

The firmware SHALL expose `GET /api/control` returning the temperature controller's live state and its tuning parameters in a single response: whether control is enabled, whether the PID is currently running, the setpoint, the current temperature, the control error, the computed output, the integral accumulator, the gains `kp`/`ki`/`kd`, and the output clamp range.

The control error SHALL be computed by the firmware as `setpoint − temperature` rather than left to the client, so the reported sign convention cannot disagree with the controller's. Fields derived from a temperature reading SHALL be omitted when no valid reading exists, matching how `/api/status` already omits `temperature`.

These fields SHALL NOT be added to `GET /api/status`, which is polled on a timer by every client; they are diagnostic detail to be fetched only when requested.

#### Scenario: Reporting control state

- **WHEN** `GET /api/control` is requested while control is enabled and sensor data is valid
- **THEN** the response SHALL include the enabled flag, running flag, setpoint, temperature, error, output, integral, gains and output range

#### Scenario: No valid temperature

- **WHEN** `GET /api/control` is requested while no valid temperature reading exists
- **THEN** the temperature and error fields SHALL be omitted
- **AND** the remaining fields SHALL still be reported

#### Scenario: Status endpoint is unchanged

- **WHEN** `GET /api/status` is requested
- **THEN** it SHALL NOT gain the gains or the integral accumulator

#### Scenario: Reading requires no CSRF header

- **WHEN** `GET /api/control` is requested without `X-Requested-With`
- **THEN** the request SHALL succeed, because it changes no state
