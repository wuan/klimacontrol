# Heating actuator via direct Shelly RPC

## Why

The control loop has been complete and observable for a while and still drives
nothing. `add-heating-actuator-and-autotune` is on hold because its GPIO/relay
premise was replaced by a Shelly, and `add-heating-cutover-runbook` documents a
deployment that cannot be executed until firmware can command a channel.

This is that firmware. It closes the loop.

## The transport decision

The two candidates were RPC over the shared MQTT broker, and RPC direct to the
device over HTTP. Direct HTTP wins on three counts that matter for heating:

| | MQTT RPC | **direct HTTP RPC** |
|---|---|---|
| Command delivery | QoS 0, at most once, silently dropped | **request/response — a lost command is an error, not a mystery** |
| Shared failure | broker down ⇒ **all eight zones** lose control | each device talks to its own manifold; no shared dependency |
| Observed state | push via `status_ntf` | polled |
| Proven in place | `enable_rpc: false` on both manifolds | `source: "HTTP_in"` — the legacy controller already does exactly this |

The first row is the important one. The at-most-once problem that shaped the
whole lease discussion applies to *commands*; over HTTP a failed command comes
back as a failure and can be retried or reported. The lease is still required —
it covers a dead or hung controller, which no transport can — but it stops
having to cover ordinary message loss as well.

The second row matters because a broker outage taking out heating in every room
simultaneously is a worse failure than any per-zone problem it might prevent.

Cost: status becomes polled rather than pushed, and each device needs its
manifold's address. Both are cheap. And `enable_rpc` never has to be turned on,
so the manifolds are not exposed to broker-wide commanding — the per-manifold
step in the cutover runbook gets simpler, not harder.

## What Changes

Four stages, ordered so work can stop after any of them and leave the device
coherent.

### Stage 1 — Shelly client and configuration verification

An HTTP RPC client for `Switch.Set`, `Switch.GetStatus` and `Switch.GetConfig`,
plus the per-device configuration naming a manifold and a channel. Before
control may be enabled, the channel's configuration is read back and checked
against the contract the runbook defines: auto-off enabled, power-on state off,
input detached. A non-conforming channel refuses control instead of becoming a
latent hazard.

### Stage 2 — Time-proportional output

The PID's `0.0…1.0` becomes valve open time over a cycle. Duty is latched at
cycle start; duties too small to complete an actuator stroke snap to fully
closed or fully open. This is where the existing `PidController` /
`RefreshPolicy` pattern applies again: Arduino-free, injected clock, tested
natively.

### Stage 3 — Wire it up, with the safety shutoff

The actuator work moves off the control loop so a slow HTTP call cannot stall
it. The over-temperature shutoff — an existing, never-implemented
`temperature-control` requirement — lands here, because this is the first change
where it can actually stop something.

### Stage 3b — A way to configure the target

Added after Stage 3, because it was missing from this proposal entirely. The
cutover runbook's per-zone step 5 is "point a KlimaControl device at a manifold
and channel", and until there is a form that means a terminal — eight times,
in a house, which is exactly where a typo assigns the wrong room's valve.

The section carries the assignment and the timings, and shows the live
conformance verdict beside them: the useful loop is seeing *why* a channel was
refused, fixing it in the Shelly's own UI, and re-checking without leaving the
page.

### Stage 4 — Tell the truth about what is happening

`isControlActive()` currently means "demand is non-zero". With a remote relay it
must become "the actuator is confirmed on", reconciled against observed state
and power draw. A dead wax head, or a manifold that has stopped answering, is
surfaced rather than displayed as a confident green dot.

## Non-goals

- **Multi-zone coordination.** One device, one room, one channel. The fleet is
  multi-zone; the firmware is not. Phase-staggering TPO cycles across zones is
  worth doing later and is not needed to close the loop.
- **Persisting PID gains, and autotune applying them.** Still the separate
  "PID parameter configurability" requirement.
- **Executing the cutover.** That is the runbook, and it is a deployment
  activity, not a code change.
- **MQTT for actuation.** Considered and rejected above. The existing MQTT
  client keeps doing telemetry.

## Capabilities

### New Capabilities

- `heating-actuator`: commanding a Shelly channel, verifying its failsafe
  configuration, time-proportional output, and reconciling commanded against
  observed state.

### Modified Capabilities

- `configuration`: manifold address, channel id, cycle time, actuator travel
  time, safety limit.
- `temperature-control`: the over-temperature shutoff becomes real, and
  `isControlActive()` is redefined in terms of confirmed actuator state.

## Impact

- **Source**: new `src/control/TimeProportionalOutput.{h,cpp}`,
  `src/actuator/ShellyChannel.{h,cpp}` and `src/actuator/HeatingActuator.{h,cpp}`;
  `SensorController`, `Config`,
  `PrefsKeys` (mind the 15-character NVS limit), routes and UI.
- **Supersedes**: Stage 1 of `add-heating-actuator-and-autotune`, which should
  be archived or closed once this lands — its GPIO content is void.
- **Unblocks**: `add-heating-cutover-runbook`, whose prerequisites are exactly
  stages 1, 3 and 4 here.
- **Risk**: this is the first change that makes the device actuate anything. A
  firmware fault now has a physical consequence, bounded by the relay's lease
  rather than by anything in this repository.
