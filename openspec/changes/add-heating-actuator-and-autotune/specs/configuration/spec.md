# configuration Specification Delta

## ADDED Requirements

### Requirement: Control tuning configuration fields

`DeviceConfig` SHALL carry the PID gains `kp`, `ki` and `kd`, the time-proportional cycle period, the actuator travel time, the control interval, the over-temperature safety limit and its release hysteresis. All SHALL be persisted to NVS under stable `PrefsKeys` entries and SHALL be validated on load with the same ranges applied to API input. A stored value failing validation SHALL fall back to its documented default rather than being clamped, matching the existing treatment of a corrupt setpoint.

Defaults SHALL suit an underfloor heating plant: a cycle period of 15 minutes, an actuator travel time of 5 minutes, and a control interval of 60 seconds.

#### Scenario: Defaults on a fresh device

- **WHEN** a freshly provisioned device boots
- **THEN** the cycle period SHALL be 15 minutes, the actuator travel time 5 minutes and the control interval 60 seconds

#### Scenario: Tuning survives a reboot

- **WHEN** gains are changed and the device is restarted
- **THEN** the stored gains SHALL be restored from NVS

#### Scenario: Corrupt stored value falls back to default

- **WHEN** a persisted gain is read back outside its valid range
- **THEN** the documented default SHALL be used rather than the nearest bound

#### Scenario: Cycle period consistency is enforced on load

- **WHEN** the persisted cycle period is less than four times the persisted actuator travel time
- **THEN** both SHALL fall back to their defaults, because the pair is only meaningful together

### Requirement: Autotune run marker

The firmware SHALL persist a single marker recording that an autotune run was active, so that a run interrupted by a reboot can be reported as aborted rather than silently forgotten. The full run state SHALL NOT be persisted, because interrupted runs are not resumed.

#### Scenario: Marker set at start and cleared at end

- **WHEN** a run starts
- **THEN** the marker SHALL be persisted, and it SHALL be cleared when the run reaches `Done` or `Aborted`

#### Scenario: Marker found at boot

- **WHEN** the device boots and finds the marker set
- **THEN** the autotune state SHALL be reported as `Aborted` with an interrupted-by-restart reason, and the marker SHALL be cleared