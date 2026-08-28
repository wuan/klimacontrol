## RENAMED Requirements

- FROM: `### Requirement: Tabbed settings modal`
- TO: `### Requirement: Settings page sections`

## MODIFIED Requirements

### Requirement: Settings page sections

The settings page SHALL organize configuration into distinct sections covering at least: Device Name, Elevation, Timezone, I2C Sensors, MQTT, E-Paper Display, Syslog, WiFi, Energy, OTA, System.

Sections SHALL be rendered as stacked blocks on a single page, each with its own heading and its own save action, so that saving one section does not submit the others.

The E-Paper Display section SHALL provide an enable toggle (default off), a rotation selector covering 0/90/180/270 degrees, and a refresh-interval field in seconds. Saving it SHALL POST to `/api/display` with the `X-Requested-With: KlimaControl` header, SHALL inform the user that the device will restart to apply the change, and SHALL warn that disabling the display blanks the panel first and so may take a few seconds.

#### Scenario: Section rendering

- **WHEN** the settings page loads
- **THEN** the sections above SHALL each be present as a headed block on the page, and each SHALL be reachable by scrolling without navigating away

#### Scenario: Sections save independently

- **WHEN** the operator changes a field in one section and activates that section's save action
- **THEN** only that section's endpoint SHALL be called, leaving the other sections' persisted settings untouched

#### Scenario: Display section reflects persisted state

- **WHEN** the E-Paper Display section is rendered
- **THEN** its fields SHALL be populated from `GET /api/display`, showing the display as disabled on a device that has never configured it

#### Scenario: Saving the Display section

- **WHEN** the operator changes the display settings and saves
- **THEN** the page SHALL POST to `/api/display` with the CSRF header and SHALL indicate that a restart is being scheduled
