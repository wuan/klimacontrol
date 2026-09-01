# web-interface Specification Delta

## ADDED Requirements

### Requirement: Tuning section on the settings page

The settings page SHALL provide a section for the PID gains, the cycle period, the actuator travel time, the control interval and the safety limit, backed by `GET`/`POST /api/tuning`. Each input SHALL state its unit and accepted range, and a rejected submission SHALL surface the device's reason rather than silently reverting. The section SHALL warn that these settings drive a physical valve.

#### Scenario: Loading current values

- **WHEN** the settings page opens
- **THEN** the tuning inputs SHALL be populated from `GET /api/tuning`

#### Scenario: Rejected submission is explained

- **WHEN** a submitted value is rejected by the device
- **THEN** the page SHALL display the reason and restore the previously stored value

### Requirement: Autotune progress and control

The web UI SHALL provide a way to start an autotune, abort a running one, and view progress. Because a run takes hours, the UI SHALL make the expected duration clear before the run starts, SHALL reconstruct its entire view from `GET /api/autotune/status` after a page reload, and SHALL keep an abort control reachable at all times while a run is active. The UI SHALL NOT hold run state that exists only in the page.

On completion the derived gains SHALL be shown alongside the current values, with an explicit accept action; the UI SHALL NOT apply them automatically unless auto-apply was selected when the run started.

#### Scenario: Duration is stated before starting

- **WHEN** the user opens the autotune control
- **THEN** the UI SHALL state that a run typically takes several hours and will make the room temperature oscillate

#### Scenario: Progress after a reload

- **WHEN** the page is reloaded during a run
- **THEN** the UI SHALL show the current state, elapsed time and cycles completed, reconstructed from the status endpoint

#### Scenario: Abort always reachable

- **WHEN** a run is active
- **THEN** an abort control SHALL be visible without navigating away from the page showing progress

#### Scenario: Derived gains presented for review

- **WHEN** a run completes without auto-apply
- **THEN** the derived gains SHALL be shown next to the current gains with an explicit accept action
- **AND** the stored gains SHALL remain unchanged until the user accepts

#### Scenario: Abort reason surfaced

- **WHEN** a run aborts, including because the device restarted mid-run
- **THEN** the UI SHALL display the recorded reason rather than returning silently to idle

## MODIFIED Requirements

### Requirement: Symbol-based control state display

The dashboard SHALL display the temperature control state using visual symbols instead of text. The control bar SHALL show one of three symbols indicating the control state: a minus sign (`−`) for **Inactive** (control disabled), a hollow circle (`○`) for **Active Off** (control enabled but the valve is currently closed), or a filled circle (`●`) for **Active On** (control enabled and the valve is currently open). The symbols SHALL be styled with CSS for visibility and color-coded: red for inactive, purple for active off, green for active on.

Under time-proportional output the symbol tracks the valve rather than the controller's intent, so a device calling for 30 % heat alternates between the hollow and filled circle across each cycle. While an autotune run is active the dashboard SHALL indicate that the normal controller is not in charge of the output.

#### Scenario: Inactive symbol displayed when control disabled

- **WHEN** `control_enabled` is `false`
- **THEN** the dashboard SHALL display the minus sign symbol (`−`) in red

#### Scenario: Filled circle while the valve is open

- **WHEN** control is enabled and the valve is currently commanded open
- **THEN** the dashboard SHALL display the filled circle symbol (`●`) in green

#### Scenario: Hollow circle during the closed interval

- **WHEN** control is enabled with a non-zero duty and the cycle is in its closed interval
- **THEN** the dashboard SHALL display the hollow circle symbol (`○`) in purple

#### Scenario: Autotune indicated

- **WHEN** an autotune run is active
- **THEN** the dashboard SHALL show that the output is under autotune control rather than presenting the symbol as normal PID activity