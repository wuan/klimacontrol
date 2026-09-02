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

The lease delay SHALL be set to at least the minimum the firmware will accept, which is several times its renewal interval. A channel left at its factory delay SHALL be treated as unconfigured even once the lease flag is enabled, because a delay shorter than that minimum is refused.

#### Scenario: Lease configured in the gap

- **WHEN** the channel is unowned
- **THEN** `auto_off` SHALL be enabled with a delay of several times the renewal interval
- **AND** the power-on state SHALL be set to off rather than restore-last

#### Scenario: Lease not enabled under the legacy controller

- **WHEN** the legacy controller still owns a channel
- **THEN** the auto-off lease SHALL NOT be enabled on that channel

#### Scenario: Factory delay is not sufficient

- **WHEN** the lease flag is enabled but the delay is left at its factory value below the firmware's accepted minimum
- **THEN** the channel SHALL NOT be considered configured, and control SHALL remain refused

### Requirement: The actuator interface stays reachable without credentials

The manifold's HTTP RPC interface SHALL remain unauthenticated for as long as any zone on it is owned by a KlimaControl device, because the firmware issues plain unauthenticated requests and has no configuration surface for credentials. Enabling authentication on a manifold SHALL be treated as a change that removes control from every zone migrated to it, with no path back short of reconfiguring the manifold.

This is a standing constraint on shared infrastructure, not a step in the procedure, and SHALL be recorded where whoever administers the manifolds will encounter it. The trust assumption is the local network — the same one the legacy controller already relies on, since it commands the same interface.

Commanding over the shared MQTT broker SHALL NOT be enabled as part of a cutover. It is unnecessary once commands go directly over HTTP, and it is device-wide rather than per-channel, so enabling it would expose every zone on a manifold to anything able to reach the broker.

#### Scenario: Authentication enabled on a migrated manifold

- **WHEN** HTTP authentication is enabled on a manifold with migrated zones
- **THEN** those zones SHALL lose actuator control, and the condition SHALL be recognised as a configuration change rather than a firmware fault

#### Scenario: Broker commanding is not enabled

- **WHEN** a zone is migrated
- **THEN** the manifold's MQTT RPC control SHALL be left disabled

### Requirement: The lease is proven by an induced failure

A zone SHALL NOT be considered migrated until the lease has been demonstrated by removing the controller mid-demand. Every other check exercises the command path; only this one exercises the property the design actually depends on.

The firmware's revocation of a channel that stops conforming SHALL likewise be demonstrated rather than assumed, by reconfiguring the channel from the relay's own interface while control is enabled. The one-time gate at assignment and the periodic re-check are different mechanisms, and only the second one protects a channel that a person reconfigures months later.

Neither demonstration SHALL be performed first on a zone with a live sensor and an occupant. A zone with no sensor competing for it SHALL be used to rehearse the full sequence.

#### Scenario: Controller removed mid-demand

- **WHEN** power is removed from the KlimaControl device while its zone is commanded on
- **THEN** the relay SHALL turn off within approximately the configured lease delay
- **AND** the relay SHALL attribute the change to its own timer rather than to a command

#### Scenario: Failsafe removed from under a running controller

- **WHEN** the auto-off lease is disabled on a channel while a KlimaControl device has control enabled for it
- **THEN** the device SHALL disable control and command the valve closed within its re-check interval

#### Scenario: Rehearsal precedes a live zone

- **WHEN** the procedure is executed for the first time
- **THEN** it SHALL be executed against a zone with no sensor assigned, and both induced failures SHALL pass there before a sensored zone is migrated

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
