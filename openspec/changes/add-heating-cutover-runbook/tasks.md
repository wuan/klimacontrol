# Runbook: migrating a heating zone to KlimaControl

Executed once per manifold, then once per zone. **Blocked** until firmware can
command a Shelly channel over MQTT — that work is not proposed yet.

## Inventory, as found 2026-09-02

```
Heizverteiler1  shellypro4pm-ece334f80800  192.168.110.152  shellies/heating1
  ch0 Bad        ch1 Wohnzimmer   ch2 Charlotte   ch3 Gästebad
Heizverteiler2  shellypro4pm-841fe897b5a4  192.168.110.168  shellies/heating2
  ch0 Jungs      ch1 Schlafzimmer ch2 Küche       ch3 Eingangsbereich

both: fw 2.0.0, mqtt enable=true, server=mqtt.home.wuerl.net, status_ntf=true,
      enable_rpc=FALSE, enable_control=FALSE
all 8 channels: auto_off=FALSE, auto_off_delay=60, apower=0.0 W at ~234 V
7 of 8:         initial_state=restore_last, in_mode=detached, source=HTTP_in
Eingangsbereich: initial_state=match_input, in_mode=follow, source=init
                 (driven by a physical input; candidate hydraulic bypass)

KlimaControl devices: Klima Wohnzimmer, Klima Jungs, Klima Kueche,
                      Klima Test (F32EB0, bench unit, 192.168.110.243)
```

## Prerequisites

- [ ] Firmware can publish `Switch.Set` to a Shelly channel over MQTT
- [ ] Firmware reads back `Switch.GetConfig` and refuses control on a
      non-conforming channel
- [ ] Firmware reports commanded vs observed state, including power draw
- [ ] `toggle_after` semantics confirmed on fw 2.0.0 — see the open question in
      `design.md`; verify on a channel with no actuator attached, or accept a
      brief actuation

## Per manifold — additive, nothing starts commanding

- [ ] Read `Mqtt.GetConfig` and check the auth posture. `enable_rpc: true` means
      anything reaching the broker can command house heating. Local network, so
      a decision to take consciously rather than an alarm
- [ ] Confirm the KlimaControl device can actually reach
      `mqtt.home.wuerl.net`. Its MQTT has never been enabled in service, and
      that client carries a history — dangling pointer, stale socket, IP vs
      hostname. Prove the transport before trusting it with a valve
- [ ] `Mqtt.SetConfig` → `enable_rpc: true`. Device-wide, affects all four
      channels; additive, HTTP RPC keeps working, legacy controller unaffected

## Per zone

Ordering matters. Steps 1–4 are the unowned window: enter it deliberately,
configure the failsafe inside it, and leave it as quickly as the checks allow.

- [ ] **1.** Legacy controller stops writing this channel. It may keep reading
- [ ] **2.** Command the channel off explicitly. Do not rely on the lease to
      clean up a relay that is already on — the countdown starts when the
      output turns on
- [ ] **3.** `Switch.SetConfig` → `auto_off: true`, `auto_off_delay: 180`
- [ ] **4.** `Switch.SetConfig` → `initial_state: "off"`
      (and `in_mode: "detached"` if not already)
- [ ] **5.** Point a KlimaControl device at `shellies/heatingN` + channel id
- [ ] **6.** Enable control on that device

## Verification gates — a zone is not migrated until all pass

Each proves something the one before it could not.

- [ ] **G1 command path** — commanded on → channel reports `output: true`
- [ ] **G2 actuator present** — `apower` ≈ 2–3 W. A closed contact only proves
      the relay switched; current proves a wax head is there and working. A
      dead actuator is otherwise invisible
- [ ] **G3 renewal** — relay stays on across several renewal intervals and
      `source` continues to reflect the command, not `"timer"`
- [ ] **G4 the lease** — pull power from the KlimaControl device mid-demand.
      The relay turns off after ~180 s with `source: "timer"`.
      **This is the acceptance test for the whole design.** Everything else is
      plumbing; this is the property being bought
- [ ] **G5 plumbing** — command this zone alone, wait, and confirm the floor of
      the room named on that channel actually warms. Channel names are labels
      somebody typed; metering cannot verify pipework
- [ ] **G6 rollback rehearsed** — disable control, confirm the legacy
      controller can resume writing the channel

## Zone order

- [ ] **Gästebad** first — no sensor competes for it, a guest bathroom nobody
      will notice, and the bench device is free to point at it. Rehearse the
      entire sequence including G4 here before touching a room with a real
      sensor
- [ ] Wohnzimmer — `Klima Wohnzimmer` exists
- [ ] Jungs — `Klima Jungs` exists
- [ ] Küche — `Klima Kueche` exists
- [ ] Bad, Charlotte, Schlafzimmer — deferred, no sensor
- [ ] Eingangsbereich — physically driven; likely stays legacy. Confirm whether
      it is the hydraulic bypass before considering it at all

## Timing

- [ ] The sequence, including G4, is rehearsable **now** — three minutes of heat
      into one floor in September is harmless
- [ ] Control quality cannot be judged until the house is losing heat. Separate
      milestone; do not let it block the cutover

## Open questions to resolve during the first heating season

- [ ] Does the legacy controller re-assert periodically, or write only on
      change? Decides whether `auto_off` may stay enabled after a rollback
- [ ] Where does the legacy controller record which channels it may write? If
      that is a config entry, per-channel ownership is easy to make explicit; if
      it is implicit in a script, step 1 is the fragile part of this procedure
- [ ] Does the manifold have a differential-pressure bypass, or does something
      guarantee minimum flow when all zones close? Eight independent per-room
      controllers cannot maintain that invariant between them
