# Heating manifold cutover runbook

## Why

The heating actuator will be a Shelly relay commanded over MQTT, one channel per
room. The hardware already exists and is already in service:

```
  Heizverteiler1  shellypro4pm  192.168.110.152  shellies/heating1
    ch0 Bad          ch1 Wohnzimmer     ch2 Charlotte   ch3 Gästebad
  Heizverteiler2  shellypro4pm  192.168.110.168  shellies/heating2
    ch0 Jungs        ch1 Schlafzimmer   ch2 Küche       ch3 Eingangsbereich
```

Eight zones. Three rooms currently have a KlimaControl device (Wohnzimmer,
Jungs, Küche); the rest do not. All eight channels are presently driven by an
existing controller over HTTP RPC — `source: "HTTP_in"` on seven of them — and
that controller becomes read-only for a channel as KlimaControl takes it over.

So the migration is **per zone and gradual**, not a single switchover, and for a
long period the same physical relay will have some channels written by
KlimaControl and some by the legacy controller. Getting that handover wrong in
either direction has consequences: a contested channel wears a wax actuator that
takes minutes per stroke, and an unowned-but-open channel heats a floor until
somebody notices.

This change writes the procedure down before the first zone moves, and captures
the firmware requirements the procedure implies.

## The finding that makes this urgent

`auto_off` — the local countdown that closes a relay if nobody re-asserts it —
is enabled on the bathroom extractor fan and on **none of the eight heating
channels**. `initial_state` is `restore_last` on seven of eight.

Today that is survivable because the legacy controller is always running and
will close a stray valve as a side effect of its own loop. Once KlimaControl is
the sole writer for a channel, that accidental safety net is gone:

```
  before   controller misbehaves ──► legacy loop closes it eventually
  after    controller misbehaves ──► nothing closes it
```

The lease stops being good practice and becomes the only thing standing between
a hung ESP32 and a continuously heated floor. The current relay configuration
would not survive that transition.

## What Changes

- A **runbook**: per-manifold preparation, a per-zone cutover sequence, and the
  verification gates that must pass before a zone is considered migrated.
- **Spec requirements** the runbook depends on, so the firmware is built to
  support it rather than retrofitted: single-writer ownership, verifying the
  actuator's failsafe configuration before commanding it, and confirming
  energisation by power draw rather than by the relay's own contact state.
- The device inventory above, recorded so it is not rediscovered.

## Non-goals

- **The MQTT actuator itself.** This change is the deployment design; the
  firmware that publishes `Switch.Set` does not exist yet and is not proposed
  here. This is deliberately written first, because the procedure constrains
  the firmware and not the other way round.
- **Control tuning.** The cutover is rehearsable now; evaluating control quality
  needs a house that is actually losing heat. Separate milestones.
- **Migrating all eight zones.** Five have no sensor. They stay legacy.
- Anything about the legacy controller's internals beyond "it must stop writing
  a given channel".

## Capabilities

### New Capabilities

- `heating-cutover`: ownership rules for a manifold channel, the ordering
  constraints on a handover, and the verification gates that qualify a zone as
  migrated.

## Impact

- **Documentation**: a runbook, executed once per zone.
- **Firmware** (future, gated on this): configuration verification before
  enabling control, power-draw confirmation, and per-device zone assignment
  (manifold topic prefix + channel id).
- **Devices outside this project**: `enable_rpc` must be turned on per manifold,
  and `auto_off` / `initial_state` set per channel. Those are changes to shared
  home infrastructure, not to this repository.
- **Blocked on**: the MQTT actuator firmware. This change can be written and
  reviewed now; it cannot be executed until something can command a relay.
