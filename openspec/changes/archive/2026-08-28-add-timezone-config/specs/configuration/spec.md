## MODIFIED Requirements

### Requirement: Configuration structs

The firmware SHALL group configuration values into domain-specific structs:

- `WiFiConfig` — `ssid`, `password`, `configured`, `connection_failures`.
- `DeviceConfig` — `device_id`, `device_name`, `sensor_i2c_address`, `target_temperature`, `temperature_control_enabled`, `elevation`, `timezone`.
- `MqttConfig` — `host`, `port`, `username`, `password`, `prefix`, `interval`, `enabled`.
- `SensorConfig` — `assignments` string.
- `EnergyConfig` — `wifi_power`, `wifi_sleep_mode`.
- `SyslogConfig` — `host`, `port`, `enabled`.

`DeviceConfig::timezone` SHALL be a POSIX TZ string of at most 47 characters
plus terminator, stored under the NVS key `timezone`, defaulting to `UTC0`. It
sits alongside `elevation` because both describe where the device physically is.

Each struct SHALL be returned by value from a corresponding `load…Config()` method, and saved via a paired `save…Config()` or partial-update method.

#### Scenario: Load round-trip

- **WHEN** a config struct is loaded, modified, saved, and loaded again
- **THEN** the second load SHALL return the modified values

#### Scenario: Timezone default on an unconfigured device

- **WHEN** `loadDeviceConfig()` is called on a device whose NVS contains no `timezone` key
- **THEN** the returned `timezone` SHALL be `UTC0`

#### Scenario: Timezone partial update

- **WHEN** `updateTimezone()` is called with a valid POSIX TZ string
- **THEN** only the timezone SHALL be written to NVS, leaving the other device fields untouched

### Requirement: Validation ranges

`validateDeviceConfig()` SHALL clamp or reset out-of-range device configuration values, including the target temperature, the elevation, and the sensor I2C address. It SHALL additionally replace an empty or implausible `timezone` with `UTC0`.

#### Scenario: Implausible timezone is reset

- **WHEN** `validateDeviceConfig()` is called with an empty timezone, or one containing non-printable characters
- **THEN** the timezone SHALL be set to `UTC0`

#### Scenario: Valid timezone is preserved

- **WHEN** `validateDeviceConfig()` is called with `CET-1CEST,M3.5.0,M10.5.0/3`
- **THEN** the timezone SHALL be left unchanged
