## ADDED Requirements

### Requirement: Cross-task DeviceConfig reads use the snapshot accessor

Cross-task readers of `DeviceConfig` SHALL obtain their fields via `ConfigManager::getDeviceConfigSnapshot()`. Specifically, `SensorController::updateControl()`, `SensorController::isHeatingPermitted()`, and any other reader on a task other than the AsyncTCP web task SHALL take the snapshot once per read site and use the returned local copy for every field reference on that call. The const reference accessor `getDeviceConfig()` SHALL NOT be used from such a task, because a multi-field read on the reference can observe a half-updated struct during a writer on another task.

#### Scenario: updateControl reads a consistent set

- **WHEN** `updateControl()` runs on the Sensor Monitor task while a web task writer is mid-update of `safety_max_c` and `safety_hyst_c`
- **THEN** every reference to those two fields in that tick SHALL observe either the pre-update pair or the post-update pair

#### Scenario: isHeatingPermitted reads the latest enabled flag

- **WHEN** a web task writes `temperature_control_enabled = false` while the Network task is calling `isHeatingPermitted()`
- **THEN** `isHeatingPermitted()` SHALL observe either the previous value (`true`) or the new value (`false`) but not a stale value left behind by a previous write

#### Scenario: A new cross-task reader adopts the snapshot

- **WHEN** a new code path needs `DeviceConfig` from a task other than the AsyncTCP web task
- **THEN** it SHALL use `getDeviceConfigSnapshot()` rather than `getDeviceConfig()`
