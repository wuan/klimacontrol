# Design

## Context

See `proposal.md` — Why. Two facts shape the approach:

- The control loop runs in the **Sensor Monitor task** (`Control::TemperatureController::update()`),
  while all MQTT publishing runs in the **Network task** (`Net::MqttPublisher::tick()`,
  once per second). The PID is decimated to the configured control interval
  (default 60 s); `update()` is still called on every ~1 s sensor cycle but only
  *computes* on the interval.
- `MqttPublisher::publishControlState(bool force)` already formats the four
  control-state payloads, compares each against the last-sent string, and
  publishes only the changed ones unless `force` is set. `force` is currently used
  once per (re)connect.

Publishing cannot move into the control loop: `PubSubClient` is not thread-safe
and belongs to the Network task, and the Sensor Monitor task must not block on
network I/O. The signal has to be a cross-task value the publisher polls.

## Goals / Non-Goals

**Goals:**

- Republish the full control-state set on each control-loop computation (once per
  configured control interval), in addition to publish-on-change and connect-time
  force.
- Keep the signalling mechanism consistent with the existing cross-task
  conventions in `TemperatureController` (single-writer `std::atomic` scalars).

**Non-Goals:**

- Changing topic names, payload formats, retain flags, or the publish-on-change
  behaviour.
- Flashing the status LED on a control-state publish (only measurement publishes
  flash today; unchanged).
- Advancing the heartbeat on every autotune update, sensor tick, or shutoff/
  disabled run.

## Decisions

### Decision: a monotonic computation counter on the controller, polled by the publisher

`TemperatureController` gains a `std::atomic<uint32_t> controlComputeCount`,
incremented once per PID computation, exposed via
`uint32_t getControlComputeCount() const`. `MqttPublisher` keeps
`lastControlComputeCount` and, on each connected tick, republishes when the
counter differs.

- **Why a counter over a boolean "dirty" flag**: a flag consumed with
  `exchange(false)` is equivalent in effect but loses how many runs passed and
  makes "did a run happen since connect?" harder to reason about; a counter also
  lets the connect path seed the baseline cleanly so a connect does not
  immediately double-publish.
- **Why poll rather than publish from the loop**: keeps network I/O on the
  Network task, and lets a computation that lands between two ticks collapse into
  a single publish if the interval is ever configured shorter than the tick.

### Decision: only a completed PID computation advances the counter

The increment sits at the PID compute point, past the decimation gate (the branch
that calls `pid.update()`).

- The disabled, no-valid-data, NaN and over-temperature-shutoff paths return
  early and set the output to `0.0`; those are not computations, so they do not
  advance the counter. Their output change is still published by the existing
  publish-on-change path.
- The autotuner owns the output during a run and updates on **every sensor tick**,
  not the control interval. Counting those would turn the heartbeat into a
  ~1 Hz publish during a run and violate the "once per control interval" cadence
  in the spec. Autotune output changes are still published on change, and the
  retained setpoint already describes the run.

### Decision: republish the full set (force), not just the setpoint

When the counter advances, the publisher calls the existing `force` path, so all
four topics (`target_temperature`, `control_enabled`, `control_output`,
`heating`) go out. This matches the request to send the setpoint with every run
and keeps the set coherent for the consumer; the messages are tiny (< 16 bytes
each) so the extra traffic is negligible next to the 15 s measurement cadence.

### Decision: seed the baseline on connect

The connect path already force-publishes everything; it also sets
`lastControlComputeCount` to the current value, so the same tick does not publish
twice.

## Risks / Trade-offs

- [More retained publishes] The retained `target_temperature` and
  `control_enabled` messages are now re-sent each control interval → harmless for
  a broker and for Home Assistant, which treats a repeated retained value as
  unchanged; the benefit (an observable setpoint per run) outweighs it.
- [Counter wrap] `uint32_t` wraps after ~4 billion computations (~8000 years at a
  60 s interval); the `!=` comparison only misfires if exactly 2^32 runs elapse
  between two ticks, which cannot happen. → No mitigation needed.
- [Autotune heartbeat goes quiet] During a run the counter does not advance, so a
  subscriber relying on the heartbeat sees a gap → acceptable: a run is not a PID
  computation, and the autotune-active state is already observable through the
  control output changing as the relay toggles.

## Migration Plan

No config, NVS, or wire-format change; deploying the firmware is the whole
migration. Rollback is reverting the firmware.
