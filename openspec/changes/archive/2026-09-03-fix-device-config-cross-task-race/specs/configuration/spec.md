## ADDED Requirements

### Requirement: DeviceConfig snapshots are indivisible

`ConfigManager` SHALL provide a `getDeviceConfigSnapshot()` accessor that returns a `DeviceConfig` copy taken under a lock such that no concurrent writer is mid-update when the copy is taken. Every `updateXxx()` method that mutates one or more fields of the in-memory `DeviceConfig` cache SHALL hold the same lock for the duration of the cache write, so a reader on another task observes either the pre-update set of fields or the post-update set, never a mix of the two.

The existing `const DeviceConfig& getDeviceConfig()` accessor SHALL be retained for same-task readers (route handlers on the AsyncTCP task, which also write). Cross-task readers SHALL use the snapshot accessor instead.

#### Scenario: Concurrent writer and reader

- **WHEN** an HTTP handler on the web task calls `updateSafetyMax(35.0f)` followed by `updateSafetyHyst(2.0f)` while a Sensor Monitor task call to `getDeviceConfigSnapshot()` overlaps the two writes
- **THEN** the snapshot SHALL contain either both old values, both new values, or the first-old-second-new pair — never the first-new-second-old combination that the unsynchronised read could observe

#### Scenario: Snapshot during NVS write

- **WHEN** `updateTargetTemperature()` is in the middle of its NVS write
- **THEN** a concurrent `getDeviceConfigSnapshot()` SHALL proceed without blocking on the NVS write, returning either the pre-update value or the post-update value but never a half-written target_temperature

#### Scenario: Single-field readers are unaffected

- **WHEN** `getDeviceConfig()` is called from the same task that is also writing (e.g. a route handler reading back what it just wrote)
- **THEN** the const reference accessor continues to be usable without taking the snapshot copy or the lock

## MODIFIED Requirements

### Requirement: Configuration structs

The firmware SHALL group configuration values into domain-specific structs. For `DeviceConfig`, cross-task consumers SHALL obtain a copy via `getDeviceConfigSnapshot()` rather than the const reference accessor, so that a multi-field read observes an indivisible snapshot rather than a mid-update mix of old and new fields — see the *DeviceConfig snapshots are indivisible* requirement.

- `WiFiConfig` — `ssid`, `password`, `configured`, `connection_failures`.
- `DeviceConfig` — `device_id`, `device_name`, `sensor_i2c_address`, `target_temperature`, `temperature_control_enabled`, `elevation`, `timezone`, `actuator_host`, `actuator_channel`, `tpo_cycle_s`, `tpo_travel_s`, `safety_max_c`, `safety_hyst_c`, `kp`, `ki`, `kd`, `control_interval_s`.
- `MqttConfig` — `host`, `port`, `username`, `password`, `prefix`, `interval`, `enabled`.
- `SensorConfig` — `assignments` string.
- `EnergyConfig` — `wifi_power`, `wifi_sleep_mode`.
- `SyslogConfig` — `host`, `port`, `enabled`.

`DeviceConfig::timezone` SHALL be a POSIX TZ string of at most 47 characters plus terminator, stored under the NVS key `timezone`, defaulting to `UTC0`. It sits alongside `elevation` because both describe where the device physically is.

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

#### Scenario: Cross-task DeviceConfig read observes a snapshot

- **WHEN** `updateControl()` on the Sensor Monitor task reads multiple `DeviceConfig` fields while a web task is mid-update
- **THEN** the read SHALL observe a consistent set of fields (either all pre-update or all post-update values), not a torn combination
