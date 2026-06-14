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
- `DeviceConfig` — `device_id`, `device_name`, `sensor_i2c_address`, `target_temperature`, `temperature_control_enabled`, `elevation`.
- `MqttConfig` — `host`, `port`, `username`, `password`, `prefix`, `interval`, `enabled`.
- `SensorConfig` — `assignments` string.
- `EnergyConfig` — `wifi_power`, `wifi_sleep_mode`.
- `SyslogConfig` — `host`, `port`, `enabled`.

Each struct SHALL be returned by value from a corresponding `load…Config()` method, and saved via a paired `save…Config()` or partial-update method.

#### Scenario: Load round-trip

- **WHEN** a config struct is loaded, modified, saved, and loaded again
- **THEN** the second load SHALL return the modified values

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

`ConfigManager` SHALL provide `validateDeviceConfig()` enforcing: target temperature in `[10.0, 30.0]` °C, elevation in `[-500.0, 9000.0]` m, sensor I2C address in `[0x08, 0x77]`. Out-of-range values SHALL be rejected without being written.

#### Scenario: Reject out-of-range setpoint

- **WHEN** `updateTargetTemperature(50.0)` is called
- **THEN** the call SHALL fail validation and neither the cache nor NVS SHALL be modified

### Requirement: Factory reset

The firmware SHALL provide a `reset()` method that clears every key in the `"klima"` namespace and the in-memory cache, then triggers a restart on the next loop iteration.

#### Scenario: Clearing all config

- **WHEN** `reset()` is called on a device with custom WiFi credentials
- **THEN** the next boot SHALL see `isConfigured() == false` and enter AP mode

