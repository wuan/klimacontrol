## ADDED Requirements

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
