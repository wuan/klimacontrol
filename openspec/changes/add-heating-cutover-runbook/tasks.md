# Runbook: migrating a heating zone to KlimaControl

Executed once per manifold, then once per zone. **Not blocked** — the actuator
firmware shipped as `add-shelly-actuator`, over direct HTTP RPC rather than the
MQTT transport this runbook was originally drafted against.

## Firmware capabilities this procedure relies on

All shipped; listed so a gate failure can be attributed to the right side.

- Commands the channel with `GET /rpc/Switch.Set?id=<ch>&on=<bool>` direct to
  the manifold. A failed request is an error, counted in `failed_requests`
- Refuses to enable control unless `Switch.GetConfig` shows `auto_off` enabled
  with a delay ≥ 120 s, `initial_state: "off"` and `in_mode: "detached"`;
  reports which one failed as `conformance` / `conformance_detail`
- Re-checks conformance every 300 s and disables control if a channel stops
  conforming
- Renews an open command every 30 s and closes the valve **explicitly** at the
  end of an open phase — the lease is never the normal close mechanism
- Observes `Switch.GetStatus` every 30 s and reports `agreement` as one of
  `closed` / `heating` / `no_actuator` / `relay_refused` / `unknown`;
  observations older than 120 s become `unknown` rather than sticking
- Actuation counts as confirmed only above 0.5 W, so a disconnected wax head
  reports `no_actuator` rather than success

## Per manifold — additive, nothing starts commanding

- [ ] Confirm HTTP RPC answers from the network the KlimaControl device is on:
      `curl http://<manifold>/rpc/Switch.GetStatus?id=<ch>`. This is the
      interface the legacy controller already uses, so it should already work
- [ ] Confirm HTTP **authentication is disabled** (`Shelly.GetDeviceInfo` →
      `auth_en: false`). The firmware builds a bare `http://host/rpc/...` URL
      with no credentials; enabling auth would lock out every migrated zone with
      no configuration path back. Record this as a standing constraint for
      whoever administers the Shellys
- [ ] Leave `Mqtt.enable_rpc` alone. The previous draft of this runbook turned
      it on; HTTP RPC makes that unnecessary, and `false` is the safer posture
- [ ] Note the manifold's address. Prefer the IP over a hostname unless DNS for
      it is known-good — the firmware resolves the host on each request

## Per zone

Ordering matters. Steps 1–4 are the unowned window: enter it deliberately,
configure the failsafe inside it, and leave it as quickly as the checks allow.

- [ ] **1.** Legacy controller stops writing this channel. It may keep reading
- [ ] **2.** Command the channel off explicitly:
      `curl "http://<manifold>/rpc/Switch.Set?id=<ch>&on=false"`. Do not rely on
      the lease to clean up a relay that is already on — the countdown starts
      when the output turns on
- [ ] **3.** `Switch.SetConfig` → `auto_off: true`, `auto_off_delay: 180`.
      Must be ≥ 120 s or the firmware refuses the channel; 180 s means three
      consecutive failed renewals still leave the valve where it is
- [ ] **4.** `Switch.SetConfig` → `initial_state: "off"`
      (and `in_mode: "detached"` if not already)
- [ ] **5.** Assign the KlimaControl device to the channel — settings page, or
      `POST /api/actuator {"host": "<manifold>", "channel": <ch>}`
- [ ] **6.** Check `GET /api/actuator` reports `conformance: "Ok"` and
      `conforming: true` **before** enabling control. If it does not, fix the
      channel config and `POST /api/actuator/recheck` rather than waiting out
      the 300 s re-check
- [ ] **7.** Confirm `cycle_s` and `travel_s` match the fitted actuator
      (`POST /api/actuator/timing`); cycle must be ≥ 4× travel
- [ ] **8.** Enable control on that device

## Verification gates — a zone is not migrated until all pass

Each proves something the one before it could not. `GET /api/actuator` on the
KlimaControl device reports everything G1–G3 need.

- [ ] **G1 command path** — commanded open → `commanded_open: true` and
      `observed_output: true`
- [ ] **G2 actuator present** — `observed_power_w` ≈ 2–3 W and
      `agreement: "heating"`. A closed contact only proves the relay switched;
      current proves a wax head is there and working. A dead actuator otherwise
      reports `no_actuator` and is invisible on the relay's own interface
- [ ] **G3 renewal** — the relay stays open across several 30 s renewals,
      `failed_requests` does not climb, and the relay's `source` continues to
      reflect the command rather than `"timer"`. If it ever reads `"timer"`
      during normal operation the explicit-close contract is broken
- [ ] **G4 the lease** — pull power from the KlimaControl device mid-demand.
      The relay turns off after ~180 s with `source: "timer"`.
      **This is the acceptance test for the whole design.** Everything else is
      plumbing; this is the property being bought
- [ ] **G5 conformance revocation** — with control enabled, set
      `auto_off: false` on the channel from the Shelly's own interface. Within
      300 s the device must disable control and command the valve closed.
      Proves the periodic re-check, not just the one-time gate
- [ ] **G6 plumbing** — command this zone alone, wait, and confirm the floor of
      the room named on that channel actually warms. Channel names are labels
      somebody typed; metering cannot verify pipework
- [ ] **G7 rollback rehearsed** — disable control, confirm the legacy
      controller can resume writing the channel

## Zone order

Work through devices step by step

## Timing

- [ ] The sequence, including G4, is rehearsable **now** — three minutes of heat
      into one floor in September is harmless
- [ ] Control quality cannot be judged until the house is losing heat. Separate
      milestone; do not let it block the cutover
- [ ] Note that `Kp`/`Ki`/`Kd` are still compile-time constants. Autotune runs
      end to end but its derived gains cannot be persisted, so a rehearsal can
      validate the cutover without being able to act on a tuning result

## Open questions to resolve during the first heating season

- [ ] Does the legacy controller re-assert periodically, or write only on
      change? Decides whether `auto_off` may stay enabled after a rollback
- [ ] Where does the legacy controller record which channels it may write? If
      that is a config entry, per-channel ownership is easy to make explicit; if
      it is implicit in a script, step 1 is the fragile part of this procedure
- [ ] Does the manifold have a differential-pressure bypass, or does something
      guarantee minimum flow when all zones close? Eight independent per-room
      controllers cannot maintain that invariant between them
- [ ] How does a migrated zone behave across a manifold firmware update, which
      reboots the relay mid-cycle? `initial_state: "off"` covers the output; the
      open question is whether `Switch.SetConfig` values survive the update