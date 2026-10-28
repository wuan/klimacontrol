# Proposal

## Why

Today the thermostat publishes its control and actuator state (`target_temperature`,
`control_enabled`, `control_output`, `heating`) only when a value changes, plus once
per MQTT (re)connect. A subscriber therefore cannot tell which setpoint and demand
belong to a given control-loop computation: a setpoint that stays constant is silent,
and the demand between changes is only inferable from the last change. Publishing the
setpoint with every control-loop run makes each computation observable, ties the
setpoint to the demand it produced, and gives consumers a heartbeat that proves the
loop is still computing.

## What Changes

- Publish the full control-state set (`target_temperature`, `control_enabled`,
  `control_output`, `heating`) to MQTT on **every control-loop computation** — once per
  configured control interval (default 60 s, i.e. each PID computation) — in addition to
  the existing publish-on-change behaviour and the forced publish on connect.
- The control loop gains a monotonic count of PID computations that the publisher reads
  to detect a fresh run; it is not gated by the measurement interval or the post-boot
  settle period.
- No change to the topic names, payload formats, or retain flags.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `mqtt-integration`: the "Control and actuator state is published" requirement is
  extended so the control-state set is republished on every control-loop computation,
  not only when a value changes.

## Impact

- `src/control/TemperatureController.{h,cpp}`: expose a monotonic computation counter,
  incremented on each PID computation.
- `src/network/MqttPublisher.{h,cpp}`: read the counter each tick and force the
  control-state publish when it advances.
- MQTT traffic: the control-state topics now republish each control interval even when
  unchanged (up to one set per interval), including the retained setpoint and
  control-enabled messages.
- Tests: a native test for the counter advancing once per control interval; existing
  payload-format tests are unaffected.
