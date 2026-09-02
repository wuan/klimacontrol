# system-architecture Specification Delta

## ADDED Requirements

### Requirement: The device remains diagnosable

The firmware SHALL retain at least one working log channel on a device under test, so that a fault which produces no crash and no HTTP response can still be observed. Diagnosis SHALL NOT depend on inferring behaviour from HTTP response bodies alone, because that channel is unavailable for exactly the faults in which no response is produced.

Where the primary channel is USB CDC, its availability SHALL be verified rather than assumed: the board definition setting `ARDUINO_USB_CDC_ON_BOOT=1` is not by itself evidence that log output reaches a host.

#### Scenario: Boot log is observable

- **WHEN** a device is reset with a serial monitor attached
- **THEN** the boot log SHALL be readable, including the `Reset reason:` line

#### Scenario: A silent log channel is treated as a defect

- **WHEN** no log output can be obtained from a device over its primary channel
- **THEN** that SHALL be treated as a fault to be fixed, not worked around, because it blocks diagnosis of every other fault

### Requirement: Debug-level diagnostics are reachable

Diagnostics written at `ESP_LOGD` SHALL be reachable through a documented build configuration. `CORE_DEBUG_LEVEL` defaults to `0`, which compiles every `ESP_LOGD` call out of the binary entirely, so a debug-level log line is invisible in a default build and cannot be relied upon by a verification procedure.

Any task that asks an operator to read a debug-level log line SHALL state the build flag required to make it exist.

#### Scenario: Debug logging can be enabled

- **WHEN** a diagnostic build is produced with `-DCORE_DEBUG_LEVEL=4`
- **THEN** `ESP_LOGD` output SHALL be emitted on the active log channel

#### Scenario: Verification steps name their build requirement

- **WHEN** a verification step depends on reading an `ESP_LOGD` line
- **THEN** that step SHALL state the build flag needed, so it is not recorded as unverifiable after the fact
