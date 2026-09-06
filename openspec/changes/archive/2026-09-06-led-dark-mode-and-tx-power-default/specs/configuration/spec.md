## ADDED Requirements

### Requirement: LED dark-mode threshold in EnergyConfig

`EnergyConfig` SHALL contain `uint16_t led_dark_after_s`, the number of seconds of sustained normal LED operation after which the status LED renders dark. The default SHALL be `300`. The value `0` SHALL mean dark mode is disabled. It SHALL be persisted under the NVS key `led_dark_s`. `validateEnergyConfig()` SHALL clamp values above `3600` to `3600`.

#### Scenario: Default on an unconfigured device

- **WHEN** `loadEnergyConfig()` is called on a device whose NVS contains no `led_dark_s` key
- **THEN** the returned `led_dark_after_s` SHALL be `300`

#### Scenario: Round-trip

- **WHEN** `led_dark_after_s` is set to `60`, saved via `saveEnergyConfig()`, and loaded again
- **THEN** the loaded value SHALL be `60`

#### Scenario: Zero is preserved

- **WHEN** `validateEnergyConfig()` is called with `led_dark_after_s = 0`
- **THEN** the value SHALL remain `0`

#### Scenario: Out-of-range value is clamped

- **WHEN** `validateEnergyConfig()` is called with `led_dark_after_s = 7200`
- **THEN** the value SHALL be `3600`

### Requirement: Default WiFi TX power is 13 dBm

`Constants::DEFAULT_WIFI_POWER` SHALL be `52`, the raw `wifi_power_t` value for 13 dBm, so that the default matches its documented intent and the settings UI fallback. `validateEnergyConfig()` SHALL reset an invalid `wifi_power` to this constant.

#### Scenario: Default on an unconfigured device

- **WHEN** `loadEnergyConfig()` is called on a device whose NVS contains no `energy_wifi_pw` key
- **THEN** the returned `wifi_power` SHALL be `52`

#### Scenario: Invalid stored value falls back to 13 dBm

- **WHEN** `validateEnergyConfig()` is called with `wifi_power = 99`
- **THEN** `wifi_power` SHALL be `52`

#### Scenario: Explicitly saved value is retained

- **WHEN** NVS holds `energy_wifi_pw = 68` from a previous firmware version
- **THEN** `loadEnergyConfig()` SHALL return `68` unchanged

## MODIFIED Requirements

### Requirement: Configuration structs

The firmware SHALL group configuration values into domain-specific structs:

- `WiFiConfig` — `ssid`, `password`, `configured`, `connection_failures`.
- `DeviceConfig` — `device_id`, `device_name`, `sensor_i2c_address`, `target_temperature`, `temperature_control_enabled`, `elevation`, `timezone`.
- `MqttConfig` — `host`, `port`, `username`, `password`, `prefix`, `interval`, `enabled`.
- `SensorConfig` — `assignments` string.
- `EnergyConfig` — `wifi_power`, `wifi_sleep_mode`, `led_dark_after_s`.
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
