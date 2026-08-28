## MODIFIED Requirements

<!-- NOTE: this requirement is renamed from "Tabbed settings modal" to
     "Settings page sections" by the `add-eink-display` change. Archive that
     change first, or this delta will not match an existing requirement. -->

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
