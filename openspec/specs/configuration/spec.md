# configuration Specification

## Purpose
TBD - created by archiving change baseline-capabilities. Update Purpose after archive.
## Requirements
### Requirement: NVS namespace

All persistent firmware configuration SHALL live under the ESP32 Preferences namespace `"klima"`. A single `ConfigManager` SHALL own NVS access and SHALL expose a `begin()` method called from `setup()` before any other config is read.

#### Scenario: Singleton initialization

- **WHEN** `config.begin()` is called from `setup()`
- **THEN** subsequent reads of any config struct SHALL succeed and SHALL not require any per-call NVS open

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

### Requirement: Partial update API

`ConfigManager` SHALL provide field-scoped update methods that update both the cached value and the corresponding NVS key without rewriting unrelated fields: `updateDeviceName(const char*)`, `updateTargetTemperature(float)`, `updateTemperatureControlEnabled(bool)`, `updateElevation(float)`, `updateSensorI2CAddress(uint8_t)`.

#### Scenario: Updating one field

- **WHEN** `updateTargetTemperature(23.5)` is called
- **THEN** the cached `DeviceConfig.target_temperature` SHALL change to `23.5` and only the corresponding NVS key SHALL be written

### Requirement: Device ID derivation

The firmware SHALL derive the device identifier from the last 3 bytes of the WiFi MAC address. The format SHALL be uppercase hex with no separators (e.g., `D3E4F5`).

#### Scenario: Stable across boots

- **WHEN** `DeviceId::getDeviceId()` is called twice on the same hardware
- **THEN** the returned identifier SHALL be identical across calls and across boots

### Requirement: Connection failure tracking

`ConfigManager` SHALL persist a `connection_failures` counter for WiFi association attempts. It SHALL expose `incrementConnectionFailures()` (writes to NVS, returns the new value), `resetConnectionFailures()`, and `getConnectionFailures()`. The counter SHALL be reset to 0 on a successful association.

#### Scenario: Incrementing

- **WHEN** `incrementConnectionFailures()` is called with a current value of 2
- **THEN** the in-memory and persisted value SHALL both become 3 and the call SHALL return 3

### Requirement: Restart management

`ConfigManager` SHALL provide deferred restart scheduling so callers can complete an HTTP response before the device reboots. The API SHALL be `requestRestart(uint32_t delayMs)`, `isRestartPending()`, and `checkRestart()` (called from a main loop).

The deferred-restart state (the "requested" flag and the deadline timestamp) SHALL be stored in a single 64-bit word (low bit = flag, upper 63 bits = unsigned deadline) and SHALL be published and consumed as one indivisible pair — a reader in the main loop SHALL observe either the pre-request state ("not requested") or the post-request state ("requested with this exact deadline"), never a torn combination of the two. The producer and consumer SHALL serialize their access via a lightweight spinlock (e.g. `std::atomic_flag`) or a lock-free equivalent. The deadline comparison in `checkRestart()` SHALL continue to use the existing wrap-safe `int32_t` math (`static_cast<int32_t>(millis() - deadline) >= 0`) so behavior is unchanged ~49 days after boot.

#### Scenario: Deferred restart after settings change

- **WHEN** an HTTP handler calls `config.requestRestart(1000)` and returns its response
- **THEN** the device SHALL restart approximately 1 second later, after the response has been flushed to the client

#### Scenario: Atomic state handoff

- **WHEN** one task calls `config.requestRestart(500)` while another task concurrently calls `config.isRestartPending()`
- **THEN** `isRestartPending()` SHALL return either `false` (no request yet) or `true` (request observed with the matching 500 ms deadline), and SHALL never observe a "requested" flag paired with a stale or zeroed deadline

### Requirement: Validation ranges

`validateDeviceConfig()` SHALL clamp or reset out-of-range device configuration values, including the target temperature, the elevation, and the sensor I2C address. It SHALL additionally replace an empty or implausible `timezone` with `UTC0`.

#### Scenario: Implausible timezone is reset

- **WHEN** `validateDeviceConfig()` is called with an empty timezone, or one containing non-printable characters
- **THEN** the timezone SHALL be set to `UTC0`

#### Scenario: Valid timezone is preserved

- **WHEN** `validateDeviceConfig()` is called with `CET-1CEST,M3.5.0,M10.5.0/3`
- **THEN** the timezone SHALL be left unchanged

### Requirement: Factory reset

The firmware SHALL provide a `reset()` method that clears every key in the `"klima"` namespace and the in-memory cache, then triggers a restart on the next loop iteration.

#### Scenario: Clearing all config

- **WHEN** `reset()` is called on a device with custom WiFi credentials
- **THEN** the next boot SHALL see `isConfigured() == false` and enter AP mode

### Requirement: Actuator assignment and timing configuration

`DeviceConfig` SHALL carry the actuator's manifold address, the channel id within that manifold, the time-proportional cycle period, the actuator travel time, and the over-temperature safety limit with its release hysteresis. All SHALL be persisted to NVS and validated on load, with a value failing validation falling back to its documented default rather than being clamped.

Every NVS key added SHALL be at most 15 characters, because `Preferences::putX()` fails silently on a longer key and the matching getter then returns the supplied default — a failure mode that has already cost this project a full debugging session.

Defaults SHALL suit underfloor heating: a 20-minute cycle and a 3-minute actuator travel time. The pair SHALL satisfy the four-strokes-per-cycle rule; specifying them independently is how an earlier draft ended up with defaults that violated it. The channel assignment SHALL have no default, because guessing which zone a device heats is worse than refusing to act.

#### Scenario: Defaults on a fresh device

- **WHEN** a freshly provisioned device boots
- **THEN** the cycle period SHALL be 20 minutes and the actuator travel time 3 minutes
- **AND** no actuator channel SHALL be assigned

#### Scenario: No assignment means no actuation

- **WHEN** no manifold address or channel id is configured
- **THEN** temperature control SHALL NOT be enablable, and the reason SHALL be reported

#### Scenario: Assignment survives a reboot

- **WHEN** an actuator assignment is stored and the device restarts
- **THEN** the assignment SHALL be restored from NVS

#### Scenario: Cycle and travel time are validated together

- **WHEN** the persisted cycle period is less than four times the persisted actuator travel time
- **THEN** both SHALL fall back to their defaults, because the pair is only meaningful together

#### Scenario: NVS keys are within the length limit

- **WHEN** a key is added for any of these fields
- **THEN** it SHALL be at most 15 characters, enforced at compile time

### Requirement: PID gain and control interval configuration

`DeviceConfig` SHALL carry the PID gains `kp`, `ki` and `kd` and the control interval in seconds. All SHALL be persisted to NVS and validated on load, with a value failing validation falling back to its documented default rather than being clamped.

Every NVS key added SHALL be at most 15 characters and SHALL be checked at compile time, because `Preferences::putX()` fails silently on a longer key and the matching getter then returns the supplied default.

The four fields SHALL be written as a set through a single partial-update method, in the shape of the existing actuator timing update, because a set of gains is only meaningful together: storing three of four and reverting one leaves a controller nobody configured.

Validation SHALL be understood as a guard against a mistyped order of magnitude rather than as a judgement about tuning quality, and the permitted ranges SHALL be documented as such. `kp` SHALL be strictly positive. `ki` and `kd` MAY be zero. The default `ki` SHALL be small enough that a sustained one-degree error takes on the order of an hour to accumulate meaningful output, and the default `kd` SHALL be zero.

#### Scenario: Defaults on a fresh device

- **WHEN** a freshly provisioned device boots with no stored gains
- **THEN** the documented default gains and control interval SHALL be in force
- **AND** the default `kd` SHALL be zero

#### Scenario: Gains survive a reboot

- **WHEN** gains are stored and the device restarts
- **THEN** they SHALL be restored from NVS

#### Scenario: Out-of-range gain falls back

- **WHEN** a persisted gain is non-finite or outside its documented range
- **THEN** that field SHALL fall back to its default, and the remaining fields SHALL be left as stored

#### Scenario: A zero proportional gain is not a valid stored value

- **WHEN** a persisted `kp` is zero
- **THEN** it SHALL fall back to its default rather than being kept

#### Scenario: NVS keys are within the length limit

- **WHEN** a key is added for any of these fields
- **THEN** it SHALL be at most 15 characters, enforced at compile time

#### Scenario: Gains are updated as a set

- **WHEN** the tuning fields are written
- **THEN** they SHALL be applied through a single update taking all four values together
