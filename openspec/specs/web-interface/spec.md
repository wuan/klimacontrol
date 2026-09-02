# web-interface Specification

## Purpose
TBD - created by archiving change baseline-capabilities. Update Purpose after archive.
## Requirements
### Requirement: Embedded web assets

The firmware SHALL serve HTML, CSS, and JavaScript as in-memory C++ string literals embedded in the firmware binary. The firmware SHALL NOT depend on a filesystem (LittleFS / SPIFFS) for web assets.

#### Scenario: No filesystem mount required

- **WHEN** the firmware is freshly flashed
- **THEN** the web UI SHALL be available without any data-partition upload step

### Requirement: Two pages

The firmware SHALL serve a main dashboard at `GET /` and a settings page at `GET /settings`. In AP mode only the settings page SHALL be reachable; the dashboard SHALL redirect to `/settings` until WiFi is configured.

#### Scenario: STA dashboard load

- **WHEN** the user opens `/` on an STA-mode device
- **THEN** the dashboard HTML SHALL load and populate from `/api/status`

#### Scenario: AP mode lands on settings

- **WHEN** the user opens any page on an AP-mode device
- **THEN** they SHALL land on the settings page (directly or via the captive-portal redirect)

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

### Requirement: Symbol-based control state display

The dashboard SHALL display the temperature control state using visual symbols instead of text. The control bar SHALL show one of three symbols indicating the control state: a minus sign (`\u2212`) for **Inactive** (control disabled), a hollow circle (`\u25CB`) for **Active Off** (control enabled but output is zero), or a filled circle (`\u25CF`) for **Active On** (control enabled and output is non-zero). The symbols SHALL be styled with CSS for visibility and color-coded: red for inactive, purple for active off, green for active on.

#### Scenario: Inactive symbol displayed when control disabled

- **WHEN** `control_enabled` is `false`
- **THEN** the dashboard SHALL display the minus sign symbol (`\u2212`) in red

#### Scenario: Active Off symbol displayed at setpoint

- **WHEN** `control_enabled` is `true` and `control_active` is `false`
- **THEN** the dashboard SHALL display the hollow circle symbol (`\u25CB`) in purple

#### Scenario: Active On symbol displayed when heating/cooling

- **WHEN** `control_enabled` is `true` and `control_active` is `true`
- **THEN** the dashboard SHALL display the filled circle symbol (`\u25CF`) in green

#### Scenario: API fallback for old firmware

- **WHEN** the `/api/status` response does not include `control_active`
- **AND** `control_enabled` is `true`
- **THEN** the dashboard SHALL display the hollow circle symbol (`\u25CB`) with the *active off* colour, reporting "enabled, output unknown" rather than claiming the device is actively heating

#### Scenario: API fallback when field missing and disabled

- **WHEN** the `/api/status` response does not include `control_active`
- **AND** `control_enabled` is `false`
- **THEN** the dashboard SHALL display the minus sign symbol (`\u2212`)

### Requirement: Polled live updates

The dashboard SHALL refresh its data by polling `/api/status` every 2 seconds. The firmware SHALL NOT use WebSockets or Server-Sent Events.

#### Scenario: Polling cadence

- **WHEN** the dashboard is open and active
- **THEN** the browser SHALL issue one `GET /api/status` request roughly every 2 seconds, and updates SHALL be applied to the DOM without a full page reload

### Requirement: Vanilla JavaScript only

The dashboard and settings page SHALL use vanilla JavaScript (no React/Vue/etc.) and the `fetch` API for HTTP requests. The UI SHALL include toast notifications for transient feedback and confirmation dialogs for destructive operations (factory reset, OTA install).

#### Scenario: Confirm before factory reset

- **WHEN** the user clicks Factory Reset
- **THEN** a confirmation dialog SHALL appear before `POST /api/settings/factory-reset` is sent

### Requirement: Device Info page exists

The firmware SHALL serve a Device Info page at `GET /about`. The
page SHALL be reachable from the main dashboard via a "Device Info"
link in the navigation footer, and SHALL be served as embedded
HTML (not from a filesystem) consistent with the existing pages.

#### Scenario: Loading the Device Info page

- **WHEN** a browser requests `/about` on a device in STA mode
- **THEN** the response SHALL be an HTML page that calls
  `GET /api/about` to populate its fields

### Requirement: Device Info page memory section

The Device Info page SHALL include a "Memory" section that lists,
at minimum, `free_heap`, `min_free_heap`, `heap_size`, and
`largest_free_block`. Each value SHALL be formatted as a
human-readable size in bytes/KB/MB (e.g. `81.5 KB`). The
`largest_free_block` value SHALL be sourced from the
`largest_free_block` field of `GET /api/about`.

#### Scenario: All memory fields render

- **WHEN** the Device Info page receives a valid `/api/about`
  response containing `free_heap`, `min_free_heap`, `heap_size`,
  and `largest_free_block`
- **THEN** all four fields SHALL be visible in the Memory section
  of the page

### Requirement: Main dashboard does not render diagnostic heap fields

The main dashboard at `GET /` SHALL NOT render `largest_free_block`
(or any other heap-fragmentation diagnostic) inline. Heap
diagnostics belong on the Device Info page; the main dashboard
SHALL stay focused on at-a-glance environmental readings
(temperature, relative humidity, dew point, target, control state).

#### Scenario: Dashboard payload sources

- **WHEN** the main dashboard is loaded in a browser
- **THEN** the page does not contain a DOM element bound to
  `largest_free_block` and its JavaScript does not read the
  `largest_free_block` key from `/api/status`

### Requirement: Settings page sections

The settings page SHALL organize configuration into distinct sections covering at least: Device Name, Elevation, Timezone, I2C Sensors, MQTT, E-Paper Display, Syslog, WiFi, Energy, OTA, System.

Sections SHALL be rendered as stacked blocks on a single page, each with its own heading and its own save action, so that saving one section does not submit the others.

The Timezone section SHALL provide a dropdown of common zones whose values are POSIX TZ strings, plus a **Custom** entry revealing a free-text field for any zone not listed. The zone table SHALL live in the page, not in firmware flash, so it can be corrected or extended without a firmware change.

The section SHALL display the current device time as reported by `GET /api/settings/timezone`, so the user can confirm the choice took effect, and SHALL indicate when the clock has not yet synced rather than showing a placeholder time.

#### Scenario: Section rendering

- **WHEN** the settings page loads
- **THEN** the sections above SHALL each be present as a headed block on the page, and each SHALL be reachable by scrolling without navigating away

#### Scenario: Stored zone is reflected in the dropdown

- **WHEN** the settings page loads and the stored timezone matches a listed zone
- **THEN** that zone SHALL be pre-selected and the custom field SHALL be hidden

#### Scenario: Unlisted zone falls back to custom

- **WHEN** the stored timezone does not match any listed zone
- **THEN** the **Custom** entry SHALL be selected and the free-text field SHALL be populated with the stored string

#### Scenario: Clock not yet synced

- **WHEN** the settings page loads before NTP has synced
- **THEN** the Timezone section SHALL indicate that it is waiting for NTP rather than rendering a placeholder time

#### Scenario: Saving does not warn about a restart

- **WHEN** the operator saves a timezone
- **THEN** the page SHALL confirm the change and refresh the displayed device time, without telling the user the device is restarting

### Requirement: Autotune controls and status

The web UI SHALL provide a way to start an autotune run, abort a running one, view its progress, and accept a derived result. Progress SHALL be reconstructed from `GET /api/autotune/status` so that it survives a page reload, and an abort control SHALL be reachable whenever a run is active.

#### Scenario: Progress survives a reload

- **WHEN** the page is reloaded during a run
- **THEN** the state, elapsed time and completed cycles SHALL be shown, rebuilt from the status endpoint

#### Scenario: Abort reachable while running

- **WHEN** a run is active
- **THEN** an abort control SHALL be visible

#### Scenario: Abort reason surfaced

- **WHEN** a run aborts for any reason
- **THEN** the recorded reason SHALL be displayed rather than the UI returning silently to idle

### Requirement: Autotune UI states its current limitations

Because the device has no heating output, a run cannot converge: the plant does not respond to the relay, so every run ends in a timeout. The UI SHALL say so before a run is started, so that the expected outcome is not read as a malfunction.

The UI SHALL also state that an accepted result is applied in memory only and does not survive a restart, rather than presenting acceptance as a durable change.

#### Scenario: Limitation stated before starting

- **WHEN** the user views the autotune controls
- **THEN** the UI SHALL state that runs cannot converge until a heating output exists

#### Scenario: Timeout is not presented as a fault

- **WHEN** a run ends in a settling or run timeout
- **THEN** the UI SHALL report the reason without implying a malfunction

#### Scenario: Acceptance is described as temporary

- **WHEN** derived gains are offered for acceptance
- **THEN** the UI SHALL state that they are not persisted and are lost on restart

### Requirement: Control parameters panel

The dashboard SHALL provide a collapsible panel showing the controller's live state and tuning parameters, backed by `GET /api/control`. It SHALL be fetched on demand rather than on the dashboard's polling timer, following the existing "Show Measurements" pattern, so that a client which is not looking at it costs the device nothing.

The panel SHALL present the controller output as a percentage of its clamp range, alongside a visual indication of magnitude. A percentage SHALL be shown rather than only the existing three-state symbol, because that symbol reports whether demand is non-zero and is therefore identical at 1 % and at 100 % demand.

The existing symbol SHALL be retained, as a glanceable summary that the e-paper footer mirrors.

#### Scenario: Panel is collapsed by default

- **WHEN** the dashboard first loads
- **THEN** the control parameters panel SHALL be hidden and `GET /api/control` SHALL NOT have been requested

#### Scenario: Showing the panel

- **WHEN** the user opens the panel
- **THEN** `GET /api/control` SHALL be requested and the values rendered

#### Scenario: Panel refreshes while visible

- **WHEN** the panel is open
- **THEN** it SHALL refresh on the dashboard's existing polling cadence
- **AND** SHALL stop refreshing once hidden

#### Scenario: Demand shown as a percentage

- **WHEN** the controller output is `0.42` with a clamp range of `0.0` to `1.0`
- **THEN** the panel SHALL show `42 %`

#### Scenario: Missing temperature is handled

- **WHEN** the response omits temperature and error because no valid reading exists
- **THEN** the panel SHALL render placeholders for those rows rather than `undefined`

#### Scenario: Symbol retained

- **WHEN** the panel is open
- **THEN** the three-state control symbol SHALL still be shown in the control bar

