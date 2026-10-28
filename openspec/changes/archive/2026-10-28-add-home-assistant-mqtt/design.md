# Design

## Context

See `proposal.md` for motivation and the spec deltas for requirements.

Current MQTT is publish-only: `MqttPublisher` (Network task, 1 s loop)
publishes sensor `Measurement`s as JSON on `{prefix}/{label}` via
`MqttClient::publish()` (PubSubClient, QoS 0, no retain). There is no
`subscribe`, no `setCallback`, and no `setWill`, and the setpoint /
control-enabled state is never published. `TemperatureController` and
`HeatingActuator` both live on the Network task, so the state being
published is already local to the task that owns the MQTT loop — except
the command path, which crosses back into `TemperatureController`.

Constraints that shape the approach:

- Single-core ESP32-S2; `ConfigManager` serialises its writes behind a
  device-config lock, so a second writer on the Network task is safe.
- `#ifdef ARDUINO` guards network code; only pure helpers are
  unit-testable in the `native` environment.
- The 1024-byte TX buffer is a known fault line (`buffer_degraded`,
  `truncated_publishes`); the project sizes things against it deliberately.

## Goals / Non-Goals

**Goals:**
- Publish setpoint, control-enabled, PID output and heating state.
- Accept setpoint and enable/disable commands over MQTT.
- Represent the device in Home Assistant as a thermostat plus sensors,
  via one opt-in retained discovery message.
- Make availability real (LWT) rather than advertised-but-unimplemented.

**Non-Goals:**
- A `climate` `action_topic` or a standalone temperature `sensor`
  (the climate entity already carries current temperature).
- Subscribing to Home Assistant's birth topic `homeassistant/status`
  (retained discovery makes it unnecessary).
- TLS, QoS > 0, or per-entity discovery.
- Exposing diagnostics (RSSI, heap) to Home Assistant.

## Decisions

**1. One device-discovery message, not five per-entity messages.**
HA device discovery (`homeassistant/device/<device_id>/config`, one
`cmps` map) sends the `device` block once and is HA's documented
preference for multi-entity devices. *Alternative:* single-component
discovery keeps each message small but repeats the device block five
times. Measured cost of the chosen path (base topic declared per
component — see decision 10):

```
  device-discovery packet, one retained message
  typical (prefix "sensors", default name)   ~1.42 kB
  worst   (prefix 63 chars, name 31 chars)    ~1.75 kB
```

**2. Raise `MQTT_BUFFER_SIZE` 1024 -> 2048.** The packet above exceeds
1024 by ~450 bytes, so 1024 would truncate. 2048 leaves ~577 B of
headroom worst-case. *Alternative:* stay at 1024 and use per-entity
discovery; rejected because the device payload is the cleaner contract
and the user chose it, with the size pinned by a test.

**3. Control-state topics are plain scalars; discovery does the mode
mapping.** `{prefix}/target_temperature` (`21.5`), `{prefix}/control_enabled`
(`ON`/`OFF`), `{prefix}/control_output` (`0..100`), `{prefix}/heating`
(`ON`/`OFF`). The climate entity maps `ON`->`heat`, `OFF`->`off` in its
`mode_stat_tpl`/`mode_cmd_tpl`, so the device keeps an honest boolean and
a neutral topic others can consume. *Alternative:* publish `heat`/`off`
directly — no template, but the state topic stops being a reusable
boolean.

**4. Availability via a real LWT.** Will topic `{prefix}/status`, payload
`offline`, **retained**; retained `online` on connect, retained `offline`
on clean disconnect. Discovery declares `avty_t: {prefix}/status`. This
also implements a `mqtt-integration` requirement the code never honoured,
and corrects the inverted sentence ("publish `online` on clean
disconnect"). *Alternative:* omit `avty_t`; rejected because a dead
device would show a live-looking retained setpoint on the thermostat card.

**5. Commands reject out-of-range rather than clamp.** The MQTT callback
uses the same range rule as `POST /api/temperature/target`. Extract the
check into a pure, native-testable predicate (and reuse it in the route)
so the two paths cannot drift. *Alternative:* rely on
`setTargetTemperature()`'s clamp; rejected because the HTTP layer
deliberately rejects, and a silently substituted setpoint is
indistinguishable from an accepted one.

**6. Poll control state in `MqttPublisher::tick()`; publish on change.**
Read `TemperatureController`/`HeatingActuator` each second, compare to the
last published values, publish the changed ones; republish all on connect.
*Alternative:* callbacks pushed from the controller — rejected because it
adds a second writer/coupling into the control path and the state is
already readable on this task.

**7. Commands are handled in the PubSubClient callback on the Network
task.** `setCallback()` takes a C function pointer, so `MqttClient` keeps
a static instance pointer and trampolines to the instance. `setCallback()`
and `subscribe()` run once after a successful connect. Subscriptions are
independent of `ha_discovery` (commands are useful with discovery off).

**8. Payload builders are pure functions.** `formatHaDiscoveryPayload()`
goes next to `formatMeasurementPayload()` in `network/MqttPayload.*`,
taking device id/name/prefix/firmware version, and is covered by the
`test_mqtt_payload` native test — including a worst-case size assertion
against the buffer. `MqttClient::publish()` gains a `retain` overload.

**9. Lifecycle tied to the toggle.** Enable/connect -> publish retained
discovery. Disable -> publish an empty retained payload to the discovery
topic to remove the device. Republish after every (re)connect, outside
the 60 s measurement settle. Discovery topic keys on `device_id` (not
prefix), so a prefix change updates HA in place without orphans.

**10. The `~` base topic is declared per component, not once at the
payload root.** Home Assistant substitutes `~` in component configs only:
its `SHARED_OPTIONS` list (device-level options merged into each component)
omits the base topic, and `_replace_topic_base()` runs per component and
requires the `~` key to be present in that component's config. A
root-level `~` is therefore ignored and every topic stays literally
`~/…`, leaving the entities permanently unavailable. Declaring `~` in each
of the five components costs ~280 B worst-case; writing every topic out in
full instead would cost ~50 B *more than the buffer allows* at the longest
prefix, and the size test in `test_mqtt_payload` pins that. *Alternative:*
root-level `~` — rejected because it does not work.

## Risks / Trade-offs

- **Discovery silently missing in HA when the buffer is degraded.** One
  oversized message is the only thing that can exceed a fallback buffer.
  -> Keep the `truncated_publishes` counter and add an explicit
  "discovery truncated" log; the size test guards future growth.
- **`~` base topic in device discovery.** Resolved during implementation
  from Home Assistant's own source: a device-level `~` is *not* propagated
  to components, so it must be declared in each component (decision 10).
  The fallback of writing every topic out in full does **not** fit 2048 at
  the longest prefix, so it is not usable; per-component `~` costs ~280 B
  worst-case and leaves ~220 B headroom, pinned by the size test.
- **+1 kB long-lived buffer lands in internal SRAM.** Confirmed on
  hardware: `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` sends the 2048-byte
  PubSubClient buffer to internal SRAM, the pool OTA and the low-heap guard
  gate on, not to PSRAM. Measured on the running device: 39.6 kB internal
  free now, 29.0 kB minimum since boot — ~12.6 kB above the low-heap
  restart guard (16 kB) and ~16.7 kB above the OTA floor (12 kB), and
  `buffer_degraded` is false with `truncated_publishes` at 0. The extra kB
  is absorbed; re-check the min-free figure if more long-lived internal
  allocations are added.
- **A retained command message is re-applied on every connect.**
  PubSubClient cannot tell a retained delivery from a fresh one. -> HA
  commands are non-retained by default (climate `retain` defaults false);
  document that retained command topics are unsupported.
- **Retained discovery outlives a firmware downgrade.** -> Disable
  discovery before downgrading, or clear the topic manually.

## Migration Plan

- Firmware update is additive; existing measurement topics and payloads
  are unchanged, so current consumers keep working with MQTT untouched.
- Enable discovery on the settings page (or `POST /api/mqtt`
  `{"ha_discovery": true}`); the thermostat and sensors appear in HA.
- Rollback: disable discovery (removes the device from HA) and/or reflash.
  Downgrading without disabling leaves a stale retained discovery payload
  that must be cleared by hand.

## Open Questions

_None outstanding._ The `~` base-topic question was settled from Home
Assistant's source (decision 10, and confirmed end-to-end on the device) and
the buffer's memory region was confirmed on hardware (see the internal-SRAM
risk above).
