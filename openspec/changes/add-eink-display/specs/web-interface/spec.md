## MODIFIED Requirements

### Requirement: Tabbed settings modal

The settings page SHALL organize configuration into tabs covering at least: Device, WiFi, MQTT, OTA, Display, System.

The Display tab SHALL provide an enable toggle (default off), a rotation selector covering 0/90/180/270 degrees, and a refresh-interval field in seconds. Saving the tab SHALL POST to `/api/display` with the `X-Requested-With: KlimaControl` header and SHALL inform the user that the device will restart to apply the change.

#### Scenario: Tab rendering

- **WHEN** the settings page loads
- **THEN** the six tabs above SHALL be present and clicking each SHALL display its associated form without navigating away

#### Scenario: Display tab reflects persisted state

- **WHEN** the Display tab is opened
- **THEN** its fields SHALL be populated from `GET /api/display`, showing the display as disabled on a device that has never configured it

#### Scenario: Saving the Display tab

- **WHEN** the operator changes the display settings and saves
- **THEN** the page SHALL POST to `/api/display` with the CSRF header and SHALL indicate that a restart is being scheduled
