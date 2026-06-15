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

The dashboard SHALL display, at minimum: current temperature, current relative humidity, target temperature, control enabled/disabled state, per-sensor connection status, and WiFi connection status.

#### Scenario: All-fields render

- **WHEN** the dashboard receives a valid `/api/status` response
- **THEN** all six fields above SHALL be rendered as text in the UI

### Requirement: Temperature control UI

The dashboard SHALL provide a control for adjusting the target temperature in 0.5 °C increments and a toggle for enabling/disabling temperature control. Adjustments SHALL be sent to `POST /api/temperature/target`; the enable/disable toggle SHALL call `POST /api/control/enable` or `/api/control/disable`.

#### Scenario: Increment by half a degree

- **WHEN** the user clicks the `+` button twice
- **THEN** two requests SHALL be issued, raising the setpoint by 1.0 °C in total

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

### Requirement: Tabbed settings modal

The settings page SHALL organize configuration into tabs covering at least: Device, WiFi, MQTT, OTA, System.

#### Scenario: Tab rendering

- **WHEN** the settings page loads
- **THEN** the five tabs above SHALL be present and clicking each SHALL display its associated form without navigating away

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

