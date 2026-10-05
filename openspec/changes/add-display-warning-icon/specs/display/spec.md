# Spec Delta — display

## ADDED Requirements

### Requirement: Warning icon layout

The e-paper value block SHALL include warning slots in both margins — the left
margin and its mirror image in the right margin: a drawn warning triangle
(filled, with the panel's existing drawn-symbol technique) sized to roughly
half the value block's height, with the active warning's label in the built-in
5x7 font directly below it. Both slots SHALL show the same token.

Each slot SHALL lie entirely inside the partial-refresh window. The
temperature, the humidity and the footer SHALL keep their existing geometry and
position whether or not the warning slots are occupied — the warning is purely
additive, and showing or clearing it SHALL NOT shift or rescale any other
element.

#### Scenario: Warning shown beside unchanged values

- **WHEN** a warning becomes active while the readings are stable
- **THEN** the panel SHALL show the triangle and label in both margins, and the temperature, humidity and footer SHALL remain in their existing positions

#### Scenario: Warning cleared restores the empty margins

- **WHEN** the active warning clears
- **THEN** both margins SHALL be blank again and no other element SHALL have moved

#### Scenario: Warning refresh is partial, not full

- **WHEN** a warning appears or clears and no other displayed value has changed
- **THEN** the repaint SHALL be a partial refresh inside the existing window, and SHALL NOT introduce a full-panel refresh

### Requirement: Warning conditions, labels and priority

The warning evaluation SHALL consider exactly these conditions, each mapped to
a fixed uppercase label from a compile-time vocabulary:

| Condition | Label |
|---|---|
| Over-temperature safety trip active | `OVERHEAT` |
| Temperature below the configured frost threshold | `FROST` |
| Sensor snapshot invalid | `SENSOR` |
| Control state is UNCERTAIN | `ACTUATOR` |
| Humidity above the configured humidity threshold | `HUMID` |

When several conditions are active at once, the panel SHALL show exactly one
warning: the highest-priority one in the order listed above. WiFi and MQTT
connectivity SHALL NOT be warning conditions.

Each threshold warning SHALL engage with hysteresis: the humidity warning fires
at or above the threshold and releases below it by a fixed margin; the frost
warning fires at or below the threshold and releases above it by a fixed
margin — so a reading hovering at the boundary does not toggle the warning.

#### Scenario: Threshold crossing engages the warning

- **WHEN** the humidity rises to the configured threshold
- **THEN** the `HUMID` warning SHALL become active on a subsequent evaluation

#### Scenario: Boundary hover does not toggle

- **WHEN** the humidity oscillates within the release margin either side of the threshold
- **THEN** the warning SHALL stay in whatever state it had, and no refresh SHALL be triggered by the oscillation alone

#### Scenario: Most severe wins

- **WHEN** the over-temperature safety trip and the humidity warning are both active
- **THEN** the panel SHALL show `OVERHEAT`, and the `HUMID` warning SHALL NOT be displayed while the higher-priority condition persists

#### Scenario: Sensor loss is explained

- **WHEN** the sensor snapshot becomes invalid so the values render as placeholders
- **THEN** the `SENSOR` warning SHALL be shown, naming why the placeholders are shown

### Requirement: Warning anti-flap dwell

A warning SHALL be cleared only after its condition has been continuously
inactive for a minimum dwell time. Warning onset SHALL NOT be delayed by the
dwell.

The dwell time SHALL be a compile-time constant, measured with the same
wrap-safe unsigned elapsed-time comparison used elsewhere in the refresh
scheduling, and SHALL be unit-tested in the `native` environment together with
the rest of the warning decision logic.

#### Scenario: Warning clears after the dwell

- **WHEN** the humidity falls below the release margin and stays there for at least the dwell time
- **THEN** the `HUMID` warning SHALL be cleared on a subsequent evaluation

#### Scenario: Brief recovery does not clear the warning

- **WHEN** the condition becomes inactive for less than the dwell time and then active again
- **THEN** the warning SHALL remain shown without interruption

#### Scenario: Onset is immediate

- **WHEN** a warning condition becomes active
- **THEN** the warning SHALL be shown without waiting for any dwell time

### Requirement: Warning changes drive the refresh decision

The refresh policy SHALL treat a change of the displayed warning token —
including the transition to and from "no warning" — as a change worth showing,
subject to the same partial/full promotion rules as every other refresh.

Warning onset SHALL bypass the minimum-interval floor: the refresh showing a
new warning SHALL happen on the tick the warning is first evaluated as active.
Warning clearance SHALL be subject to the floor like any other change.

#### Scenario: Onset repaints immediately

- **WHEN** a warning becomes active sooner than the configured minimum interval after the previous refresh
- **THEN** the panel SHALL still be refreshed on the current tick to show it

#### Scenario: Clearance respects the interval floor

- **WHEN** a warning clears sooner than the configured minimum interval after the previous refresh
- **THEN** the blank margins SHALL be rendered once the interval has passed, and not before

#### Scenario: Unchanged warning triggers nothing

- **WHEN** the same warning stays active across many ticks
- **THEN** no refresh SHALL be scheduled on the warning's account alone
