# Heating manifold cutover runbook

## Why

The heating actuator is a Shelly relay commanded over HTTP RPC, one channel per
room. The hardware already exists and is already in service:

```
  Heizverteiler1  shellypro4pm  192.168.110.152
    ch0 Bad          ch1 Wohnzimmer     ch2 Charlotte   ch3 Gästebad
  Heizverteiler2  shellypro4pm  192.168.110.168
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

This change writes the procedure down before the first zone moves.

## The firmware now exists, over HTTP rather than MQTT

This proposal was first written while the output path was still undecided and
assumed a boolean actuator commanded over the shared MQTT broker.
`add-shelly-actuator` has since shipped and been archived, and it chose **direct
HTTP RPC** instead: `http://<manifold>/rpc/Switch.Set?id=<ch>&on=<bool>`, each
device addressing its own manifold. Request/response makes a lost command an
error rather than a silent drop, and no broker outage can remove control from
all eight zones at once.

That closes the blocker this change was written against, and it simplifies the
procedure in one specific way: **`Mqtt.enable_rpc` stays `false`**. The
per-manifold preparation step that would have opened every channel on a manifold
to anything reaching the broker is not needed at all. HTTP RPC is the interface
the legacy controller already uses (`source: "HTTP_in"`), so the cutover changes
who calls it, not what is exposed.

It also moves one prerequisite from the broker to the manifold: the firmware
builds a plain `http://` URL with no credentials, so **HTTP authentication must
remain disabled** on both manifolds. Enabling `Shelly.SetAuth` would lock the
firmware out with no way to configure a password.

The firmware requirements this procedure implies are no longer speculative —
they are shipped and specified in the `heating-actuator` capability: conformance
refusal before enabling control, periodic re-checking, explicit close rather than
lease expiry, and agreement between commanded and observed state including power
draw. This change therefore specifies only what remains procedure.

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
- **Spec requirements** that are genuinely procedure rather than firmware:
  single-writer ownership per channel, the ordering constraint that routes a
  handover through unowned rather than contested, when the relay's failsafe is
  configured, physical verification of channel-to-room mapping, proving the
  lease by induced failure, and reversibility at every step.
- The device inventory above, recorded so it is not rediscovered.

## Non-goals

- **The actuator firmware.** Shipped and archived as `add-shelly-actuator`. The
  requirements on it live in the `heating-actuator` capability and are not
  restated here.
- **Control tuning.** The cutover is rehearsable now; evaluating control quality
  needs a house that is actually losing heat. Separate milestones. Note that
  `Kp`/`Ki`/`Kd` are still compile-time constants, so a converged autotune run
  has nowhere to write its answer — tracked separately.
- **Migrating all eight zones.** Five have no sensor. They stay legacy.
- Anything about the legacy controller's internals beyond "it must stop writing
  a given channel".

## Capabilities

### New Capabilities

- `heating-cutover`: ownership rules for a manifold channel, the ordering
  constraints on a handover, and the verification gates that qualify a zone as
  migrated.

## Impact

- **Documentation**: a runbook, executed once per manifold and once per zone.
- **Firmware**: none. Everything the procedure needs is shipped — the
  conformance gate, the periodic re-check, agreement reporting, and per-device
  assignment via `actuator_host` + `actuator_channel` (`POST /api/actuator`).
- **Devices outside this project**: per channel, `auto_off: true` with
  `auto_off_delay` ≥ 120 s, `initial_state: "off"` and `in_mode: "detached"`.
  Device-wide, HTTP authentication must stay disabled. Those are changes to
  shared home infrastructure, not to this repository. `Mqtt.enable_rpc` stays
  `false` — no longer required by this design.
- **Not blocked.** The first zone can be migrated now.
