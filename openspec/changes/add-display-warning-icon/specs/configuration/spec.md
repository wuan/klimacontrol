# Spec Delta — configuration

## ADDED Requirements

### Requirement: Display warning threshold configuration

`DisplayConfig` SHALL carry the user-configurable warning thresholds: a frost
temperature in degrees Celsius and a relative-humidity limit in percent. Each
SHALL have a disabled state, SHALL default to disabled, and SHALL be persisted
to NVS under a key of at most 15 characters checked at compile time, following
the existing NVS key rule.

`validateDisplayConfig()` SHALL fall back to the documented default for any
stored value outside its permitted range, matching the fail-to-default
convention of the other display fields.

The thresholds configure only the user threshold warnings; the always-on
conditions (sensor invalid, over-temperature safety trip, controller
UNCERTAIN) SHALL NOT be disableable through configuration.

#### Scenario: Factory defaults disable threshold warnings

- **WHEN** a device boots with no warning threshold keys in NVS
- **THEN** both thresholds SHALL read as disabled and no threshold warning SHALL ever be evaluated

#### Scenario: Corrupt stored threshold falls back to disabled

- **WHEN** a stored frost threshold is outside its permitted range
- **THEN** the loaded configuration SHALL have the threshold disabled rather than an arbitrary value

#### Scenario: Enabled thresholds persist

- **WHEN** an operator sets a frost threshold and a humidity limit via the settings and the device restarts
- **THEN** both values SHALL survive the restart and the corresponding warnings SHALL be evaluated against them
