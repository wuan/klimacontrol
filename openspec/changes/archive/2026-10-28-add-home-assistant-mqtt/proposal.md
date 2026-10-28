# Proposal

## Why

Klima-Control is a thermostat, but it can only be monitored and controlled
from its own web UI. MQTT is currently publish-only: it streams sensor
measurements out and accepts nothing back, and it never publishes the
setpoint or the control-enabled state. A Home Assistant user gets no
entities, cannot see the target temperature or whether control is on, and
cannot change either without opening the device's page. Home Assistant's
MQTT integration is the standard way to close that gap, and this device
maps onto it almost exactly — a thermostat with a current temperature, a
setpoint, and an on/off state.

## What Changes

- Add a `ha_discovery` boolean to `MqttConfig`, persisted in NVS, exposed
  through `GET/POST /api/mqtt`, and toggled from the MQTT section of the
  settings page.
- Publish four new values from the controller/actuator:
  `{prefix}/target_temperature` and `{prefix}/control_enabled` (both
  **retained**), `{prefix}/control_output` (PID output as a percentage),
  and `{prefix}/heating` (confirmed heating, `ON`/`OFF`).
- Subscribe to `{prefix}/target_temperature/set` and
  `{prefix}/control_enabled/set` and apply them. The setpoint command
  **rejects** out-of-range values rather than clamping, sharing the
  validation the HTTP route already performs.
- When `ha_discovery` is enabled, publish a single **retained**
  Home Assistant device-discovery payload describing the device and its
  entities (one `climate`, two `sensor`s for humidity/dew point, one
  `sensor` for PID output, one `binary_sensor` for heating), republished
  after every (re)connect. Turning it off publishes an empty retained
  payload that removes the device.
- Wire availability: register the MQTT Last Will as a retained `offline`
  on `{prefix}/status`, publish a retained `online` on connect and
  `offline` on clean disconnect, and correct the existing spec sentence
  that says the opposite. This makes UI-advertised availability real
  rather than a stale `online` that outlives the device.
- Raise `MQTT_BUFFER_SIZE` from 1024 to 2048 bytes: the device-discovery
  payload is ~1.4 kB and cannot fit the current buffer. A native test
  pins the worst-case size against the buffer.
- Scope the networking requirement that gates MQTT publishing to
  *measurement* publishes, so the event-driven discovery and control-state
  publishes are permitted without loosening the measurement cadence.

## Capabilities

### New Capabilities
- `home-assistant-integration`: the device's Home Assistant interop — the
  retained MQTT discovery payload, the entity mapping, availability
  wiring, and the republish/removal lifecycle tied to the config toggle.

### Modified Capabilities
- `mqtt-integration`: new published values (setpoint, control state, PID
  output, heating), inbound command topics with validation, the
  implemented Last Will / availability behaviour, and the larger TX
  buffer.
- `configuration`: `MqttConfig` gains the `ha_discovery` field.
- `networking`: the publish-gating requirement is narrowed to measurement
  publishes so event-driven publishes are allowed.

## Impact

- **Code**: `src/MqttClient.{h,cpp}`, `src/network/MqttPublisher.{h,cpp}`,
  `src/network/MqttPayload.{h,cpp}`, `src/Config.{h,cpp}`,
  `src/routes/MqttRoutes.cpp`, `src/Network.{h,cpp}`, `data/settings.html`
  (and its regenerated `src/generated/*_gz.h`), plus new native tests.
- **Memory**: the long-lived PubSubClient buffer grows by 1 kB (may be
  served from PSRAM; to be confirmed against the boot heap log).
- **Pre-existing gap resolved**: the `mqtt-integration` spec already
  required a Last Will that the firmware never implemented; this change
  makes the code match the spec and fixes one inverted sentence.
- **Compatibility**: additive. Existing topics and payloads are unchanged;
  no consumer has to change.
