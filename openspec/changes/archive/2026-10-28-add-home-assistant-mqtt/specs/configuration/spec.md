# Spec Delta

## MODIFIED Requirements

### Requirement: Configuration structs

The firmware SHALL group configuration values into domain-specific structs:

- `WiFiConfig` — `ssid`, `password`, `configured`, `connection_failures`.
- `DeviceConfig` — `device_id`, `device_name`, `sensor_i2c_address`, `target_temperature`, `temperature_control_enabled`, `elevation`, `timezone`.
- `MqttConfig` — `host`, `port`, `username`, `password`, `prefix`, `interval`, `enabled`, `ha_discovery`.
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

#### Scenario: MQTT discovery flag round-trip

- **WHEN** `MqttConfig.ha_discovery` is set, saved, and the config is loaded again
- **THEN** the reloaded `ha_discovery` SHALL match the saved value
