# Spec Delta — web-interface

## ADDED Requirements

### Requirement: E-Paper Display value field selects

The settings page's E-Paper Display section SHALL provide a **Second value** select and a **Third value** select. The second select SHALL offer humidity, dew point, pressure, sea-level pressure, CO₂, illuminance, VOC index and PM2.5, defaulting to humidity. The third select SHALL offer none plus the same values, defaulting to none. Both SHALL load their current selections from `GET /api/display` and SHALL submit the selected tokens through `POST /api/display` with the existing CSRF header.

#### Scenario: Stored selections load into the selects

- **WHEN** the settings page loads and `GET /api/display` reports dew point second and pressure third
- **THEN** the Second value select SHALL show dew point and the Third value select SHALL show pressure

#### Scenario: Saving value selections

- **WHEN** the operator chooses VOC index second and none third and saves the display settings
- **THEN** `POST /api/display` SHALL receive `second_field: "voc_index"` and `third_field: "none"` with the CSRF header

#### Scenario: Third value none

- **WHEN** the Third value select is set to none and the settings are saved
- **THEN** the persisted third field SHALL be none and the panel SHALL use the two-line layout after restart

### Requirement: E-Paper Display field guidance

The E-Paper Display section SHALL state that sea-level pressure requires a configured elevation greater than zero, and that a selected value without a matching sensor renders a placeholder.

#### Scenario: Sea-level pressure guidance

- **WHEN** the operator selects sea-level pressure and the configured elevation is zero
- **THEN** the section SHALL state that sea-level pressure requires a configured elevation

#### Scenario: Missing sensor guidance

- **WHEN** the operator selects a value for which the device has no matching sensor
- **THEN** the section SHALL state that the panel will show a placeholder for that value
