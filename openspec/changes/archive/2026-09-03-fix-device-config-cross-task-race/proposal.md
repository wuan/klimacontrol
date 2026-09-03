# Cross-task `DeviceConfig` reads are torn

## Why

`Config::ConfigManager::deviceConfig` is shared between the AsyncTCP web task
that writes it (every `updateXxx()` invokes `deviceConfig.field = value`
unlocked, `Config.cpp:244-345`) and the Sensor Monitor task that reads it
inside `updateControl()` (`SensorController.cpp:552,583,585-586,593,618,642,659`).

No individual field tears on ESP32-S2 — 32-bit aligned loads and stores are
atomic. What tears is the **set**. A tick that is mid-`updateControl()` can
read the new `target_temperature` together with the old `safety_max_c`, the new
`temperature_control_enabled` with the old `control_interval_s`, or any other
half-updated combination. The over-temperature shutoff can engage against one
limit and release against another; the PID can charge its integral against a
`dt` measured under the old interval but with the new target. None of these
are diagnosed today because they are intermittent, single-tick, and silent.

The project has already solved exactly this race for the three PID gains via
`pendingGains` / `gainsChangeRequested` (`SensorController.h:113-114`). The
five `DeviceConfig` fields the control loop reads (`target_temperature`,
`temperature_control_enabled`, `safety_max_c`, `safety_hyst_c`,
`control_interval_s`) deserve the same treatment.

## What Changes

- A spinlock (`std::atomic_flag`) on `ConfigManager`, used to serialize every
  write to the `deviceConfig` cache.
- A new `ConfigManager::getDeviceConfigSnapshot()` that takes the lock, copies
  the struct, releases.
- `updateControl()` takes one snapshot at the top and reads everything from
  it, replacing every `config.getDeviceConfig().xxx` with `cfg.xxx`.
- `SensorController::isHeatingPermitted()` and `Network.cpp:586`
  (`heatingActuator.configure(config.getDeviceConfig())`) move to the snapshot
  too — they are also cross-task readers of fields from the same struct.
- A native multi-threaded test driving concurrent writers and readers,
  asserting that every snapshot is internally consistent.
- Spec requirements added to `configuration` (atomic snapshot) and
  `system-architecture` (cross-task readers must use it).

### Non-goals

- A wholesale rewrite of `ConfigManager` to be entirely thread-safe. The five
  readers this change touches are the only ones that read multiple fields from
  the struct. Single-field readers (e.g. `isControlEnabled()`) are not part of
  this race and continue to use `getDeviceConfig()` directly.
- A new locking discipline for `WifiConfig`, `MqttConfig`, `EnergyConfig`,
  `SyslogConfig`, `DisplayConfig`, or `SensorConfig`. None of these structs
  have cross-task readers today (verify in a follow-up).
- Restructuring the gains-pattern path. It already works and stays as-is.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `configuration`: add a requirement that `ConfigManager` provide a snapshot
  accessor that returns an indivisible copy of `DeviceConfig`, and that every
  `updateXxx()` method serialize its cache write.
- `system-architecture`: add a requirement that cross-task readers of
  `DeviceConfig` go through the snapshot accessor, not the const reference.

## Impact

- **Source**: `src/Config.{h,cpp}` (spinlock + snapshot method, all cache
  writes wrapped), `src/SensorController.cpp` (snapshot at the top of
  `updateControl()`, plus `isHeatingPermitted()`), `src/Network.cpp` (line
  586).
- **Tests**: `test/test_device_config_snapshot/` — native, multi-threaded,
  asserting no torn snapshot under heavy concurrent `updateXxx()` traffic.
- **No web, no NVS schema, no hardware impact.**
- **Not blocked.** The fix is independent of any other pending change.
