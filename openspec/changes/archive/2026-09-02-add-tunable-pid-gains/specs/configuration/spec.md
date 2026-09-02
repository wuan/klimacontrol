# configuration Specification Delta

## ADDED Requirements

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