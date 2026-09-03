# heating-actuator Specification

## Purpose
TBD - created by archiving change add-shelly-actuator. Update Purpose after archive.
## Requirements
### Requirement: Commanding a Shelly channel over HTTP RPC

The firmware SHALL command its assigned relay channel using Shelly RPC over HTTP directly to the device, rather than RPC over a shared MQTT broker. HTTP RPC is request/response, so a command that fails to arrive SHALL be observable as a failure rather than silently dropped; and each device addressing its own manifold SHALL avoid a shared broker outage removing control from every zone at once.

The firmware SHALL support `Switch.Set`, `Switch.GetStatus` and `Switch.GetConfig` on a configured channel.

#### Scenario: Commanding the channel

- **WHEN** the actuator commands its channel on
- **THEN** a `Switch.Set` request SHALL be issued to the configured manifold and channel

#### Scenario: A failed command is observable

- **WHEN** a command request fails or times out
- **THEN** the failure SHALL be recorded and surfaced, and SHALL NOT be treated as a successful command

### Requirement: Actuator I/O never blocks the control loop

Actuator requests SHALL NOT be issued from the task that runs the sensor read and control loop. That task feeds a watchdog on a one-second cadence, and an unreachable manifold can take seconds to time out. Demand SHALL be handed to the actuator through shared state with a single writer on each side.

#### Scenario: Unreachable manifold does not stall sensing

- **WHEN** the manifold is unreachable and requests are timing out
- **THEN** sensor reads, the control loop and the watchdog SHALL continue on their normal cadence

### Requirement: Time-proportional output

The controller output in the range `[0.0, 1.0]` SHALL be converted to valve open time over a configurable cycle. The duty SHALL be latched at the start of each cycle and SHALL NOT be re-evaluated mid-cycle, so that the fraction of the cycle spent open corresponds to the commanded duty.

The cycle period SHALL be at least four times the configured actuator travel time. Duties that would command an open or closed interval shorter than the travel time SHALL NOT be delivered as a partial stroke, because a valve asked to perform a stroke it cannot complete delivers heat unrelated to the commanded duty.

Such a shortfall SHALL be accumulated rather than discarded, and delivered as a single full-length pulse once it is worth one. Discarding it would make every duty below one stroke per cycle unreachable — on an underfloor plant that covers most of the heating season, and the loop would hunt between nothing and the minimum instead of settling. Rounding up at the opposite rail SHALL likewise be carried as a negative balance and worked off. The average delivered over several cycles SHALL track the demand even though no individual cycle can represent it.

Accumulated credit SHALL be discarded when demand reaches zero, so that heat requested before the reason for it disappeared is not delivered afterwards.

Time-proportional output SHALL be evaluated on the actuator's cadence rather than the control loop's, so the two need not share a period.

#### Scenario: Duty becomes open time

- **WHEN** the demand is `0.30` and the cycle period is 15 minutes
- **THEN** the valve SHALL be commanded open for approximately 4.5 minutes and closed for the remainder

#### Scenario: Duty latched for the cycle

- **WHEN** the demand changes part-way through a cycle
- **THEN** the current cycle SHALL complete on the latched value and the next SHALL use the new one

#### Scenario: Unachievable duty is deferred, not dropped

- **WHEN** the duty would command an open interval shorter than the actuator travel time
- **THEN** that cycle SHALL be fully closed
- **AND** the shortfall SHALL be retained and delivered in a later cycle

#### Scenario: Small demand averages correctly

- **WHEN** a demand below one stroke per cycle is held for many cycles
- **THEN** the total open time SHALL approximate the demanded fraction
- **AND** every individual pulse SHALL be at least one full stroke

#### Scenario: High demand averages correctly

- **WHEN** a demand whose closed interval would be shorter than a stroke is held for many cycles
- **THEN** those cycles SHALL be fully open, and the overdelivery SHALL be worked off so the average still tracks the demand

#### Scenario: Mid-range duty is unaffected

- **WHEN** the demanded open and closed intervals both exceed the travel time
- **THEN** every cycle SHALL deliver its duty directly, with no cycle skipped

#### Scenario: Credit is discarded when demand ceases

- **WHEN** demand falls to zero while a shortfall is outstanding
- **THEN** the outstanding amount SHALL be discarded rather than delivered later

### Requirement: The valve is closed explicitly, not by lease expiry

The end of an open phase SHALL be commanded explicitly. The relay's auto-off lease SHALL NOT be used as the normal mechanism for closing the valve, because expiry would append up to the full lease duration of unrequested heat to every cycle. The lease is a failsafe for the case where the controller stops acting at all.

While the valve is meant to be open, the on-command SHALL be re-issued often enough that the lease cannot expire, and the lease SHALL be wide enough that a single failed request does not move the valve.

#### Scenario: Explicit close

- **WHEN** the open phase of a cycle ends
- **THEN** an off command SHALL be issued rather than allowing the lease to expire

#### Scenario: A single failed renewal does not move the valve

- **WHEN** one renewal request fails while the valve should stay open
- **THEN** the valve SHALL remain open, and a subsequent renewal SHALL restore the lease

### Requirement: Control is refused against a non-conforming channel

The firmware SHALL read the target channel's configuration and SHALL refuse to enable temperature control unless the relay's own failsafe is in place: the auto-off lease enabled with a delay of at least twice the renewal interval, the power-on state set to off rather than restoring its previous output, and the input mode detached so a physical input cannot override the controller.

The check SHALL be repeated periodically, because the relay can be reconfigured from its own interface at any time. A channel that stops conforming SHALL cause control to be disabled and the condition reported.

#### Scenario: Missing lease refuses control

- **WHEN** the channel reports auto-off disabled
- **THEN** control SHALL NOT be enabled, and the reason SHALL be reported

#### Scenario: Restore-last refuses control

- **WHEN** the channel reports a power-on state that restores its previous output
- **THEN** control SHALL NOT be enabled

#### Scenario: Conformance lost while running

- **WHEN** a periodic re-check finds the channel no longer conforming
- **THEN** control SHALL be disabled and the valve commanded closed

### Requirement: Actuator state is confirmed, not assumed

The firmware SHALL distinguish what it commanded from what the relay reports, and SHALL treat a zone as actuated only when both the relay's contact state and its measured power draw agree with the command. A closed contact proves the relay switched; only current proves a thermal actuator is present and drawing power.

`isControlActive()` SHALL report confirmed actuation rather than non-zero demand, and any display derived from it SHALL follow confirmed state. A stale or failed observation SHALL be shown as unknown rather than as the last believed value.

#### Scenario: Confirmed heating

- **WHEN** the channel is commanded on, reports its contact closed, and draws power consistent with a thermal actuator
- **THEN** the zone SHALL be reported as actively heating

#### Scenario: Missing actuator detected

- **WHEN** the channel is commanded on and reports its contact closed but approximately no power draw
- **THEN** a fault SHALL be reported, because the actuator is absent, disconnected or failed

#### Scenario: Manifold unreachable

- **WHEN** observations have failed for longer than the observation timeout
- **THEN** the actuator state SHALL be reported as unknown rather than as the last observed value

#### Scenario: Relay disagrees with the command

- **WHEN** the channel is commanded on but reports its contact open
- **THEN** the disagreement SHALL be reported

### Requirement: Actuator host is re-validated at the storage boundary

`Config::ConfigManager::updateActuatorAssignment()` SHALL re-validate the
`actuatorHost` argument with the same character-class predicate the
`POST /api/actuator` route applies (`^[A-Za-z0-9._-]{1,253}$`, empty
permitted) before writing to NVS or to the in-memory `deviceConfig` cache.
A non-empty host that does not match the predicate SHALL be treated as if
no host had been supplied: the stored `deviceConfig.actuator_host` SHALL
be `""` and `deviceConfig.actuator_channel` SHALL be
`ACTUATOR_CHANNEL_UNASSIGNED`. No partial write SHALL occur.

This rule exists so a future caller that bypasses the route handler (a CLI,
a future MQTT control channel, a unit test, etc.) cannot store a host that,
once concatenated into `snprintf("http://%s%s", host, path)` inside
`HeatingActuator::httpGet()`, would redirect the device's actuator probe
away from its configured manifold.

#### Scenario: Storage rejects SSRF payload from a non-HTTP caller

- **WHEN** `ConfigManager::updateActuatorAssignment("192.168.1.42/admin", 0)`
  is called directly (without going through the HTTP route)
- **THEN** the stored `deviceConfig.actuator_host` SHALL be `""` and
  `deviceConfig.actuator_channel` SHALL be `-1`, exactly as if an empty
  host had been passed

#### Scenario: Storage accepts a valid IPv4 literal

- **WHEN** `ConfigManager::updateActuatorAssignment("192.168.1.1", 2)` is
  called
- **THEN** `deviceConfig.actuator_host` SHALL be `"192.168.1.1"` and
  `deviceConfig.actuator_channel` SHALL be `2`

#### Scenario: Storage accepts an mDNS-style hostname

- **WHEN** `ConfigManager::updateActuatorAssignment("shellypro4pm-aabbccddeeff.local", 0)`
  is called
- **THEN** `deviceConfig.actuator_host` SHALL be
  `"shellypro4pm-aabbccddeeff.local"` and `deviceConfig.actuator_channel`
  SHALL be `0`

#### Scenario: Storage rejects whitespace and control bytes

- **WHEN** `ConfigManager::updateActuatorAssignment()` is called with a
  non-empty host containing a space, a tab, a newline, or any byte outside
  `^[A-Za-z0-9._-]$`
- **THEN** the assignment SHALL be cleared and no NVS write SHALL occur for
  the host field

#### Scenario: Round-trip from NVS does not reintroduce a bad host

- **WHEN** `loadDeviceConfig()` reads a stored `actuator_host` that was
  written by a pre-fix firmware and contains characters outside the
  permitted set
- **THEN** `validateDeviceConfig()` (or an equivalent load-time check
  applied to the host field) SHALL clear the host to `""` and the channel
  to `ACTUATOR_CHANNEL_UNASSIGNED`, so the device cannot drive an actuator
  whose URL is malformed

#### Scenario: Empty host from any caller clears the assignment

- **WHEN** `ConfigManager::updateActuatorAssignment("", 2)` or
  `ConfigManager::updateActuatorAssignment(nullptr, 2)` is called
- **THEN** `deviceConfig.actuator_host` SHALL be `""` and
  `deviceConfig.actuator_channel` SHALL be `-1` — the existing "host with
  no channel clears the assignment" behaviour is preserved for empty
  input as well

