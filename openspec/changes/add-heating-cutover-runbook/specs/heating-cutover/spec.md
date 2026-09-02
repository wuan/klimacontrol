# heating-cutover Specification Delta

## ADDED Requirements

### Requirement: A manifold channel has exactly one writer

Each manifold channel SHALL have exactly one controller authorised to command it at any time. A channel SHALL NOT be commanded by both the legacy controller and a KlimaControl device, because two writers driving one relay chatter a thermal actuator that needs minutes per stroke, and leave both controllers holding an incorrect model of the zone.

Ownership SHALL be recorded per channel rather than treated as a global mode, because migration is gradual: the same physical relay will carry both KlimaControl-owned and legacy-owned channels for an extended period.

#### Scenario: Only one controller commands a channel

- **WHEN** a KlimaControl device has been given ownership of a channel
- **THEN** the legacy controller SHALL no longer write to that channel
- **AND** it MAY continue to read that channel's state

#### Scenario: Other channels are unaffected

- **WHEN** one channel of a manifold is migrated
- **THEN** the remaining channels on the same device SHALL continue to be written by the legacy controller

### Requirement: Handover routes through the unowned state

A cutover SHALL pass through a state in which neither controller writes the channel, and SHALL NOT pass through a state in which both do. The unowned state costs a cold room for a few minutes and is self-correcting; the contested state causes mechanical wear and leaves both controllers with wrong beliefs about the zone.

#### Scenario: Legacy stops before KlimaControl starts

- **WHEN** a zone is migrated
- **THEN** the legacy controller SHALL stop writing the channel before the KlimaControl device is enabled for it

#### Scenario: Channel is placed in a known state

- **WHEN** the channel becomes unowned
- **THEN** it SHALL be explicitly commanded off, rather than leaving whatever state the previous writer left behind

### Requirement: The failsafe is configured while the channel is unowned

The relay's local auto-off lease and its power-on state SHALL be configured during the unowned window, not before it. Enabling a lease while the legacy controller still owns the channel would apply that lease to the legacy controller, closing the valve mid-demand if that controller writes only on change — and no lease duration is both short enough to be useful and long enough to be safe, because a zone may legitimately be called for hours.

Enabling the lease SHALL NOT be relied upon to close a relay that is already on, because the countdown conventionally starts when the output turns on.

#### Scenario: Lease configured in the gap

- **WHEN** the channel is unowned
- **THEN** `auto_off` SHALL be enabled with a delay of several times the renewal interval
- **AND** the power-on state SHALL be set to off rather than restore-last

#### Scenario: Lease not enabled under the legacy controller

- **WHEN** the legacy controller still owns a channel
- **THEN** the auto-off lease SHALL NOT be enabled on that channel

### Requirement: Control is refused against an unverified actuator

Before a KlimaControl device commands a channel, the actuator's configuration SHALL be read back and checked: the auto-off lease enabled with an acceptable delay, the power-on state set to off, and the input mode detached so a physical input cannot override the controller. The device SHALL refuse to enable temperature control when the check fails.

This is the MQTT-era counterpart of the wiring contract a directly-driven relay would have needed. Unlike a soldered wire, the actuator can be interrogated over the same channel used to command it, so a misconfigured relay SHALL surface as an error rather than remaining a latent hazard.

#### Scenario: Missing lease refuses control

- **WHEN** the target channel reports auto-off disabled
- **THEN** the device SHALL refuse to enable temperature control and SHALL report why

#### Scenario: Restore-last refuses control

- **WHEN** the target channel reports a power-on state that restores its previous output
- **THEN** the device SHALL refuse to enable temperature control

#### Scenario: A conforming actuator is accepted

- **WHEN** the target channel reports auto-off enabled, power-on state off, and input detached
- **THEN** control MAY be enabled

### Requirement: Energisation is confirmed by power draw

A commanded-on channel SHALL be confirmed by the actuator's measured power draw, not by the relay's contact state alone. A closed contact proves the relay switched; only current proves a thermal actuator is present and working. A failed or disconnected wax head is otherwise undetectable — the room simply never warms.

#### Scenario: Actuator confirmed

- **WHEN** a channel is commanded on and reports a power draw consistent with a thermal actuator
- **THEN** the zone SHALL be treated as actuated

#### Scenario: Actuator missing

- **WHEN** a channel is commanded on, reports its contact closed, and draws approximately no power
- **THEN** the condition SHALL be surfaced as a fault rather than treated as successful actuation

### Requirement: The lease is proven by an induced failure

A zone SHALL NOT be considered migrated until the lease has been demonstrated by removing the controller mid-demand. Every other check exercises the command path; only this one exercises the property the design actually depends on.

#### Scenario: Controller removed mid-demand

- **WHEN** power is removed from the KlimaControl device while its zone is commanded on
- **THEN** the relay SHALL turn off within approximately the configured lease delay
- **AND** the relay SHALL attribute the change to its own timer rather than to a command

### Requirement: Channel-to-room mapping is physically verified

The mapping between a manifold channel and the room it heats SHALL be confirmed physically once per zone. Channel names are labels entered by a person, and per-channel metering proves only that a channel energised, not that it is plumbed to the room named.

#### Scenario: Mapping confirmed

- **WHEN** a single zone is commanded on in isolation and given time to take effect
- **THEN** the floor of the room that channel is believed to serve SHALL be confirmed to warm

### Requirement: Every step is reversible

A zone SHALL be returnable to legacy control at any point in the procedure, by disabling control on the KlimaControl device and permitting the legacy controller to write the channel again. No step SHALL leave the system in a state that cannot be undone.

#### Scenario: Rollback mid-cutover

- **WHEN** a cutover is abandoned at any step
- **THEN** the legacy controller SHALL be able to resume writing the channel
- **AND** the zone SHALL return to its pre-cutover behaviour
