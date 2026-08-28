## MODIFIED Requirements

### Requirement: Configuration structs

The firmware SHALL group configuration values into domain-specific structs:

- `WiFiConfig` — `ssid`, `password`, `configured`, `connection_failures`.
- `DeviceConfig` — `device_id`, `device_name`, `sensor_i2c_address`, `target_temperature`, `temperature_control_enabled`, `elevation`.
- `MqttConfig` — `host`, `port`, `username`, `password`, `prefix`, `interval`, `enabled`.
- `SensorConfig` — `assignments` string.
- `EnergyConfig` — `wifi_power`, `wifi_sleep_mode`.
- `SyslogConfig` — `host`, `port`, `enabled`.
- `DisplayConfig` — `enabled` (default `false`), `rotation` (0..3, default 0), `interval` (minimum seconds between e-paper refreshes, 10..3600, default 60), and `clear_pending` (a firmware-internal one-shot flag, never exposed through the HTTP API).

Each struct SHALL be returned by value from a corresponding `load…Config()` method, and saved via a paired `save…Config()` or partial-update method.

NVS keys SHALL remain within the ≤12-character limit documented in `PrefsKeys.h`. The display keys SHALL therefore be `disp_enabled`, `disp_rot`, `disp_intv` and `disp_clear`; the `interval` field SHALL NOT be stored under the 13-character key `disp_interval`.

#### Scenario: Load round-trip

- **WHEN** a config struct is loaded, modified, saved, and loaded again
- **THEN** the second load SHALL return the modified values

#### Scenario: Display defaults on an unconfigured device

- **WHEN** `loadDisplayConfig()` is called on a device whose NVS namespace contains none of the `disp_*` keys
- **THEN** it SHALL return `enabled = false`, `rotation = 0`, `interval = 60`, and `clear_pending = false`

#### Scenario: Display validation clamps out-of-range values

- **WHEN** `validateDisplayConfig()` is called with `rotation = 7` and `interval = 2`
- **THEN** `rotation` SHALL be clamped into 0..3 and `interval` SHALL be clamped into 10..3600

#### Scenario: The clear-pending flag survives an interrupted disable

- **WHEN** the display is disabled, `clear_pending` is persisted as `true`, and the device loses power before the panel is blanked
- **THEN** the next `loadDisplayConfig()` SHALL still report `clear_pending = true`, so the pending blanking is completed on that boot
