# Design: the cutover

## The state space

Ownership of one manifold channel:

```
                 legacy writes   KlimaControl writes
   A  today           yes               no          ok
   B                  yes               yes         CONTESTED
   C                  no                no          UNOWNED
   D  target          no                yes         ok
```

There is no atomic path from A to D. One of B or C must be traversed, so the
question is which, and they fail very differently:

```
  B contested   two writers command one relay. The valve chatters, and a
                thermal wax actuator takes 3-5 minutes per stroke — so it never
                reaches an end stop, wears, and delivers heat unrelated to
                either controller's intent. Neither controller's model of the
                zone is correct.

  C unowned     nobody commands it. The valve closes. The room gets cold.
```

**Route through unowned.** A cold room for a few minutes is cheap and
self-correcting; a contested valve is mechanical wear plus two controllers
holding wrong beliefs. Prefer a cold room to a contested valve.

## Why the lease is configured mid-handover

The instinct is to enable `auto_off` first, since it is the failsafe. That is
wrong.

While the legacy controller still owns a channel, `auto_off` applies to *it*. If
that controller writes only on change — common — then a 180 s lease closes the
valve three minutes into every call for heat, and the legacy controller has no
idea why. Nor is there a safely long value: in deep winter a zone can legitimately
be on for hours, so any lease short enough to be useful is short enough to
interfere.

The unowned window resolves it at no cost:

```
  legacy stops writing  ──►  UNOWNED  ──►  KlimaControl starts
                               │
                               └─ no writer to surprise. Command the channel
                                  off for a known state, then set auto_off and
                                  initial_state here.
```

Note the explicit off: enabling `auto_off` on a relay that is *already* on
conventionally does not start a countdown retroactively — the timer starts when
the output turns on. Do not rely on the lease to clean up a state that predates
it.

## Why `auto_off` and not something we build

Every other candidate mechanism shares fate with the network:

| mechanism | survives firmware hang | survives WiFi loss | survives an unreachable manifold |
|---|---|---|---|
| re-assertion from the controller | no | no | no |
| a watchdog elsewhere on the network | yes | no | no |
| **`auto_off` in the relay** | **yes** | **yes** | **yes** |

`auto_off` runs inside the device holding the contactor. It needs no network and
no ESP32. That is the whole reason it is the failsafe and everything else is
advisory — and `source: "timer"` on the bathroom fan is that mechanism observed
firing autonomously in production.

Direct HTTP RPC improves the *advisory* half without touching this: a failed
`Switch.Set` returns an error the firmware records and surfaces
(`failed_requests`, `agreement`), so ordinary command loss becomes visible
instead of silent. It does nothing for a controller that has stopped acting
altogether, which is exactly the case the lease covers. The lease is not
made redundant by the transport choice; it is made the only remaining
dependency.

With `auto_off` and `initial_state: off` set, every failure terminates:

| failure | what closes the valve |
|---|---|
| firmware crash or hang | auto_off, locally |
| device WiFi loss | auto_off, locally |
| manifold unreachable from the device | auto_off, locally |
| relay loses WiFi | auto_off — needs no network |
| relay reboots | `initial_state: off` |
| sensor fails | firmware stops renewing → auto_off |
| mains cut to the manifold | the NC actuator de-energises physically |

### The lease must span several renewals

The firmware renews an open command every 30 s (`RENEW_MS`) and refuses a
channel whose lease is shorter than 120 s (`MIN_LEASE_S`, four renewals). So the
runbook's `auto_off_delay: 180` is not arbitrary: it clears the firmware's
own floor with margin, and it means three consecutive failed renewals still
leave the valve where it is. A wax head takes 3–5 minutes per stroke, so a lease
that trips on one lost packet would move a valve for no reason.

## What the transport costs — and what it no longer costs

The earlier draft of this design turned on `Mqtt.enable_rpc` per manifold, and
noted that this is **device-wide, not per-channel**: it would have exposed all
four zones on a manifold to anything reaching a broker with `user: null`. That
step is gone. `add-shelly-actuator` commands the channel over HTTP RPC, which is
the interface the legacy controller already uses, so the cutover adds no new
exposure surface at all. `enable_rpc` stays `false` on both manifolds.

What HTTP RPC costs instead is one constraint on the manifolds:

```
  HeatingActuator::request()  →  snprintf("http://%s%s", host, path)
                                 no credentials, no digest auth
```

The firmware has no way to authenticate. **HTTP auth must remain disabled on
both manifolds**, and turning it on later would lock every migrated zone out of
its actuator with no configuration path back. Same local-network trust
assumption as before, arrived at by not changing anything rather than by opening
something — but now it is a standing constraint on shared infrastructure, not a
one-time decision. Worth recording where whoever administers the Shellys will
see it.

The other side of the trade: control is now per-device point-to-point, so eight
zones have eight independent failure domains instead of one shared broker. A
device that cannot reach its manifold loses only its own zone, and the lease
closes that zone's valve locally.

## Two things the electrics cannot tell you

**Energisation is not the same as a working actuator.** The relay's `output`
flag says a contact closed. Per-channel power metering says a wax head actually
drew current:

```
  commanded on + apower ~ 0 W   →  actuator disconnected, failed, or absent
```

A dead wax head is otherwise invisible: the room simply never warms and nobody
knows why. All eight channels read 0.0 W at ~234 V with everything off, which is
a clean baseline.

**Channel-to-room mapping is an inherited assumption.** `Wohnzimmer`,
`Charlotte`, `Gästebad` are labels somebody typed. Metering proves *a* channel
energised, not that it is plumbed to the room named. Command one zone alone,
wait, and go and feel the floor. Once per zone, and then it is known.

## Sequencing against the calendar

It is early September. The full sequence — including the lease test — is
rehearsable now, because three minutes of heat into one floor is harmless.
Control quality cannot be evaluated until the house is losing heat.

Those are separate milestones and conflating them would stall the cutover on a
question the cutover does not answer.

Rehearse on **Gästebad**: no sensor competing for it, a guest bathroom nobody
will notice, and the bench device (F32EB0, `Klima Test`) is free to point at it.

## Rollback

At any point: disable control on the KlimaControl device and let the legacy
controller resume writing the channel. Nothing in the sequence is one-way.

Whether `auto_off` can stay enabled after a rollback depends on whether the
legacy controller re-asserts periodically or only writes on change — which is
observable once the heating season starts, and is the single most useful thing
to learn about it.
