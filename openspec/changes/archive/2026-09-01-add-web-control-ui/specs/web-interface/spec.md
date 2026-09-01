# web-interface Specification Delta

## MODIFIED Requirements

### Requirement: Temperature control UI

The dashboard SHALL provide a control for adjusting the target temperature in
0.5 °C increments and a toggle for enabling/disabling temperature control.
Adjustments SHALL be sent to `POST /api/temperature/target`; the enable/disable
toggle SHALL call `POST /api/control/enable` or `/api/control/disable`. All
three requests SHALL carry the `X-Requested-With: KlimaControl` CSRF header.

The stepper SHALL clamp the requested value client-side to the range
`[10.0, 30.0]` °C so that no out-of-range request is issued during normal use.
The stepper SHALL remain operable while temperature control is disabled, so a
target can be set before control is switched on.

The stepper SHALL render each tap immediately from local state and SHALL
coalesce a burst of taps into a single request. While a local adjustment is
pending or its request is in flight, polled `/api/status` responses SHALL NOT
overwrite the displayed setpoint; all other polled fields SHALL continue to
update normally.

#### Scenario: Increment by half a degree

- **WHEN** the user clicks the `+` button twice
- **THEN** the displayed setpoint SHALL rise by 1.0 °C in total
- **AND** the resulting setpoint SHALL be sent to `POST /api/temperature/target`

#### Scenario: Burst of taps coalesces into one request

- **WHEN** the user taps `+` three times in rapid succession from 22.0 °C
- **THEN** the display SHALL show 22.5, 23.0 and 23.5 °C as each tap lands
- **AND** exactly one `POST /api/temperature/target` request SHALL be issued,
  with value `23.5`

#### Scenario: Upper rail

- **WHEN** the displayed setpoint is 30.0 °C and the user clicks `+`
- **THEN** the displayed setpoint SHALL remain 30.0 °C
- **AND** no request SHALL be issued

#### Scenario: Lower rail

- **WHEN** the displayed setpoint is 10.0 °C and the user clicks `−`
- **THEN** the displayed setpoint SHALL remain 10.0 °C
- **AND** no request SHALL be issued

#### Scenario: Poll does not revert an in-progress adjustment

- **WHEN** the user has adjusted the setpoint and the request has not yet
  resolved
- **AND** a scheduled `/api/status` poll returns the previous setpoint
- **THEN** the displayed setpoint SHALL remain the user's adjusted value
- **AND** the temperature, humidity and control-state fields SHALL still be
  updated from that response

#### Scenario: Poll resumes ownership after the request resolves

- **WHEN** a setpoint request has resolved
- **AND** a subsequent `/api/status` poll returns a `target_temperature`
- **THEN** the displayed setpoint SHALL be updated from the polled value

#### Scenario: Setpoint adjustable while control is disabled

- **WHEN** `control_enabled` is `false` and the user clicks `+`
- **THEN** the setpoint SHALL be adjusted and persisted as normal
- **AND** the control state SHALL remain disabled

#### Scenario: Toggling control on

- **WHEN** the user switches the control toggle on
- **THEN** `POST /api/control/enable` SHALL be issued
- **AND** the toggle SHALL reflect the enabled state without waiting for the
  next poll

#### Scenario: Toggling control off

- **WHEN** the user switches the control toggle off
- **THEN** `POST /api/control/disable` SHALL be issued

#### Scenario: Poll does not flip the toggle mid-request

- **WHEN** the user has switched the toggle and the request has not yet resolved
- **AND** a scheduled `/api/status` poll returns the previous `control_enabled`
  value
- **THEN** the toggle SHALL remain in the position the user selected

#### Scenario: Rejected setpoint is reconciled

- **WHEN** a `POST /api/temperature/target` request returns HTTP 4xx
- **THEN** the dashboard SHALL restore the displayed setpoint from the device's
  reported `target_temperature` rather than leaving the rejected value on screen

### Requirement: Dashboard contents

The dashboard SHALL display, at minimum: current temperature, current relative
humidity, target temperature, control enabled/disabled state, per-sensor
connection status, and WiFi connection status.

The target temperature and the control enabled/disabled state SHALL be
presented as interactive controls (see "Temperature control UI"), not as
read-only text. The symbol-based control state indicator SHALL be retained
alongside the enable toggle: the toggle reports what the user requested, the
symbol reports what the controller is currently doing, and these differ
whenever control is enabled but output is zero.

#### Scenario: All-fields render

- **WHEN** the dashboard receives a valid `/api/status` response
- **THEN** all six fields above SHALL be rendered in the UI

#### Scenario: Toggle and symbol both present

- **WHEN** `control_enabled` is `true` and `control_active` is `false`
- **THEN** the toggle SHALL be shown in the on position
- **AND** the hollow circle symbol (`○`) SHALL be shown alongside it