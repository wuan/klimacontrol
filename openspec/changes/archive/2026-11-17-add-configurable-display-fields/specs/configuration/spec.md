# Spec Delta — configuration

## ADDED Requirements

### Requirement: Display value field configuration

`DisplayConfig` SHALL carry `second_field` and `third_field`, persisted under NVS keys `disp_fld2` and `disp_fld3`, each at most 15 characters checked at compile time. `second_field` SHALL default to relative humidity; `third_field` SHALL default to none. Supported selections SHALL be humidity, dew point, pressure, sea-level pressure, CO₂, illuminance, VOC index, PM2.5, and none for the third field. Validation SHALL fall back to the documented default for unknown stored values.

#### Scenario: Factory defaults

- **WHEN** a device boots with no display value-field keys in NVS
- **THEN** `loadDisplayConfig()` SHALL return relative humidity for the second field and none for the third field

#### Scenario: Selections survive a restart

- **WHEN** dew point is stored as the second field and pressure as the third field, and the device restarts
- **THEN** the loaded configuration SHALL contain dew point and pressure respectively

#### Scenario: Unknown stored second field falls back to humidity

- **WHEN** the stored second-field value is outside the supported set
- **THEN** the loaded configuration SHALL use relative humidity

#### Scenario: Unknown stored third field falls back to none

- **WHEN** the stored third-field value is outside the supported set
- **THEN** the loaded configuration SHALL use none

#### Scenario: Third field none is preserved

- **WHEN** the third field is explicitly stored as none
- **THEN** the loaded configuration SHALL report none and the panel SHALL use the two-line layout

#### Scenario: NVS keys are within the length limit

- **WHEN** the display value-field keys are added
- **THEN** both SHALL be at most 15 characters, enforced at compile time
