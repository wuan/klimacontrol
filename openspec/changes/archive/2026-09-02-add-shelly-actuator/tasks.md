# Tasks: Shelly actuator

Staged; each stage leaves the device coherent. If scope has to be cut, cut from
the end.

## Stage 0 — settle before starting

- [x] Confirm the manifold address form — **resolved: a configurable host
      string.** Both manifolds answer to mDNS
      (`shellypro4pm-ece334f80800.local`, `shellypro4pm-841fe897b5a4.local`), so
      the field accepts either a name or an IP and the DHCP question is a
      deployment choice rather than a code one
- [x] `toggle_after` — **deferred, not a dependency.** Cannot be tested without
      actuating a live appliance, and `auto_off` already provides the lease.
      Remains a follow-up optimisation
- [x] Bench device channel — **a deployment choice, not a code one.** The
      runbook already nominates Gästebad for the rehearsal

## Stage 1 — client and configuration verification

- [x] `src/actuator/HeatingActuator.{h,cpp}` — `Switch.Set`, `Switch.GetStatus`,
      `Switch.GetConfig` over HTTP RPC, 3 s timeouts, failure counter. Split
      from the pure `ShellyChannel` parser/predicate so the safety-critical half
      is natively testable
- [x] Config fields: manifold address, channel id, cycle period, travel time,
      safety limit, hysteresis. NVS keys **≤ 15 characters**, `static_assert`ed
      as `Config::nvsKeyFits` already does
- [x] No default channel assignment — an unassigned device refuses control
- [x] Conformance check against the runbook contract: `auto_off` enabled with a
      delay ≥ 2× renewal, `initial_state: "off"`, `in_mode: "detached"`
- [x] Refuse to enable control when the check fails; report which field failed
- [x] Re-check periodically; disable control and close the valve if conformance
      is lost
- [x] Tests: conformance predicate over the real JSON shapes captured from
      Heizverteiler1, including each individual violation

## Stage 2 — time-proportional output

- [x] `src/control/TimeProportionalOutput.{h,cpp}` — Arduino-free, injected
      clock, following `PidController` and `RefreshPolicy`
- [x] Latch duty at cycle start; never re-evaluate mid-cycle
- [x] Bank unachievable duties as credit and spend them as full-length pulses,
      rather than snapping them away — see the dead-zone note in `design.md`
- [x] Validate cycle period ≥ 4× travel time
- [x] Handle the `millis()` rollover in the cycle arithmetic
- [x] Tests: duty→open-time, latching, both rails, rollover, 0/1 produce no
      switching, and cycle skipping averages correctly at 5 %, 10 % and 95 %
      with no partial strokes (21 cases)

## Stage 3 — wire it up

- [x] Actuator work runs off the control loop; demand crosses as an atomic
- [x] Actuator tick evaluates TPO and issues commands on its own cadence
- [x] Renewal while open, explicit off at the end of the open phase
- [x] **Over-temperature shutoff** — before the PID computation, with
      hysteresis, and engaging when no valid reading exists
- [x] Valve commanded closed on every path that stops control: disabled,
      sensor invalid, shutoff, conformance lost, autotune ended
- [x] Tests: unreachable manifold does not stall the sensor task or the watchdog

## Stage 3b — configuration UI

Missing from the original proposal; see the note there. Without it the cutover
runbook's per-zone assignment step requires a terminal.

- [x] Settings section: manifold host, channel, cycle time, travel time,
      safety limit
- [x] Live conformance verdict with the actionable detail message, so a refused
      channel can be diagnosed and fixed without leaving the page
- [x] A re-check control, so the verdict can be refreshed after changing
      something in the Shelly's own UI rather than waiting out the 5 minute
      periodic re-check
- [x] `POST /api/actuator/timing` for cycle / travel / safety limit, validated as
      a pair and rejecting rather than clamping (named `/api/actuator/timing`
      rather than under `/api/settings/`, to sit with the other actuator routes)
- [x] Warn that these settings drive a real valve
- [x] Regenerate `src/generated/settings_gz.h`

## Stage 4 — report the truth

- [x] Track commanded / observed / power separately
- [x] `isControlActive()` becomes confirmed actuation
- [x] Surface the four interesting states: agreed-open, agreed-closed,
      **commanded-on-but-no-power** (dead actuator), and unreachable —
      as `Actuator::ReportedState` {disabled, idle, heating, unknown, fault},
      mapped by a pure function with 10 native tests
- [x] `/api/control` and `/api/status` gain `state`; the dashboard symbol gains a
      fourth amber form for "cannot say", the e-paper gains
      `ControlState::UNCERTAIN` (ring with a dot), and the demand bar is
      suppressed while uncertain because a duty means nothing when the actuator
      is not answering
- [x] Regenerate web assets

## Verification on hardware

- [x] Conformance refusal: point at a channel with `auto_off: false` and
      confirm control cannot be enabled — verified against Gästebad
      (heating1 ch3) as found: `auto_off_disabled`, `conforming=false`,
      `permitted=false`, `commanded_open=false`, and no command ever issued
- [ ] A 0.30 duty produces a ~4.5 min open phase in a 15 min cycle
- [x] Power draw confirms actuation — **1.9 W observed** on heating1 ch0
      ("Bad") while commanded open, against a 0.0 W baseline at ~234 V. Above
      the 0.5 W threshold, so `agreement` reads `heating` rather than
      `no_actuator`
- [x] Pull the manifold off the network mid-demand: sensing continues, state
      goes unknown, and the relay's own lease closes the valve — **verified by
      the maintainer**. This is the acceptance test for the whole design: it is
      the only check that demonstrates the lease actually fires, rather than
      being a property the configuration merely claims
- [x] Over-temperature shutoff closes the valve and holds it through hysteresis
      — **verified by the maintainer**
- [ ] Then run `add-heating-cutover-runbook` against Gästebad

## Follow-ups deliberately not here

- Phase-staggering TPO across zones (`hash(device_id) mod cycle`) so eight
  zones do not pulse in unison
- Persisting PID gains, and autotune applying a result durably
- Closing out `add-heating-actuator-and-autotune`, whose Stage 1 this supersedes

## Verified on hardware (2026-09-02)

Against the real Heizverteiler1, read-only — no zone was ever commanded.

```
assign -> 192.168.110.152 ch3 (Gästebad)
  conformance      auto_off_disabled
  detail           "Set auto_off=true on this channel: without it nothing
                    closes the valve if this controller stops"
  conforming       false
  commanded_open   false      <- never commanded, which is the point
  failed_requests  0          <- all three RPCs succeeded
  observed         output=false power=0.0W  agreement=closed
  defaults         cycle 1200s, travel 180s, safety 35C

assign -> 192.168.110.9 (unrouted, every call times out)
  sensor_timestamp deltas over 40 s:
    4999 5010 5000 5000 6000 4999 5004   <- control loop untouched
  conformance      not_read
  observed_valid   false -> agreement "unknown", not a stale belief
  commanded_open   false
```

### Route shadowing, found and fixed

`server.on("/api/actuator", ...)` builds a `Type::BackwardCompatible` matcher,
which matches `_value == path || path.startsWith(_value + "/")`
(`WebServer.cpp:335`). Registered before its own sub-paths, it swallowed both of
them, and produced two distinct failures:

- `POST /api/actuator/recheck` (no body) reached the assignment handler's empty
  `onRequest` and returned **501**
- `POST /api/actuator/timing` (with a body) was parsed as an assignment with no
  `host`, **silently clearing the channel** and answering 200

Fixed with `AsyncURIMatcher::exact` rather than by registering the specific
paths first. Ordering would also work — `SensorRoutes` happens to rely on it for
`/api/sensors` — but it is invisible at the call site and a later reordering
would quietly reintroduce this.

The conformance gate refusing a real channel, and refusing it for the right
reason with an actionable message, is the single most important behaviour in
this change: it is what stops the firmware driving a valve that nothing would
close if the firmware died.
