## MODIFIED Requirements

### Requirement: Tabbed settings modal

The settings page SHALL organize configuration into tabs covering at least: Device, WiFi, MQTT, OTA, System.

The Device section SHALL include a timezone selector: a dropdown of common zones
whose values are POSIX TZ strings, plus a **Custom** entry revealing a free-text
field for any zone not listed. The zone table SHALL live in the page, not in
firmware flash, so it can be corrected or extended without a firmware change.

The selector SHALL display the current local time as reported by
`GET /api/settings/timezone`, so the user can confirm the choice took effect, and
SHALL indicate when the clock has not yet synced rather than showing a
placeholder time.

#### Scenario: Tab rendering

- **WHEN** the settings page loads
- **THEN** the five tabs above SHALL be present and clicking each SHALL display its associated form without navigating away

#### Scenario: Stored zone is reflected in the dropdown

- **WHEN** the settings page loads and the stored timezone matches a listed zone
- **THEN** that zone SHALL be pre-selected and the custom field SHALL be hidden

#### Scenario: Unlisted zone falls back to custom

- **WHEN** the stored timezone does not match any listed zone
- **THEN** the **Custom** entry SHALL be selected and the free-text field SHALL be populated with the stored string

#### Scenario: Saving does not warn about a restart

- **WHEN** the operator saves a timezone
- **THEN** the page SHALL confirm the change and refresh the displayed local time, without telling the user the device is restarting
