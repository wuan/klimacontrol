# configuration Specification Delta

## ADDED Requirements

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
