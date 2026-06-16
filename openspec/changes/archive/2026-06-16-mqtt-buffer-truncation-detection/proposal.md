## Why

`MqttClient::begin()` calls `mqttClient.setBufferSize(MQTT_BUFFER_SIZE)`
with a requested 1024-byte buffer (`src/MqttClient.cpp:52`). PubSubClient
allocates this buffer from the heap; on a fragmented heap (which is
exactly the situation the firmware runs into on a no-PSRAM ESP32-S2
after uptime), `setBufferSize()` returns `false` and the only signal
is a `ESP_LOGW` line that the user will never see. From that point
on, every publish goes through the compile-time 256-byte default buffer
(`PubSubClient.h: MQTT_MAX_PACKET_SIZE = 256`). When a measurement
payload approaches that ceiling, PubSubClient returns `false` from
`publish()` — the firmware logs a generic "N failed" line and increments
`getFailedCount()`. The broker never sees those measurements. The user
sees an MQTT pipeline that *appears* to work end-to-end but is silently
dropping data on the floor, with no signal in the API that distinguishes
"buffer too small" from "broker rejected" or "transient network issue".

## What Changes

- Track the *actual* buffer size PubSubClient is operating with
  (captured via `getBufferSize()` after `setBufferSize()`), plus a
  `bufferDegraded` flag that is set when the actual size is below the
  requested 1024 bytes.
- In `MqttClient::publish()`, when the underlying `PubSubClient::publish()`
  returns `false` AND `bufferDegraded` is set, emit a one-shot
  `ESP_LOGE` per cycle that explicitly identifies the publish as a
  likely buffer-truncation (the user can see in the serial log
  *why* the publish failed, not just *that* it failed).
- Add a new `truncatedPublishes` counter on `MqttClient` that
  increments only for publishes that fail while the buffer is
  degraded — a distinct signal that can be exposed via the API.
- Surface the buffer size, the `bufferDegraded` flag, and the
  truncation counter in the `/api/mqtt` JSON response (alongside
  the existing `stats` block), so an operator can see in the web
  UI that the MQTT path is running on a smaller buffer than intended
  and which publishes have been dropped as a result.
- Periodically retry `setBufferSize(MQTT_BUFFER_SIZE)` from the
  `loop()` reconnect path (i.e. on every successful connect, while
  the heap may be slightly less fragmented): if the retry succeeds,
  clear `bufferDegraded` and emit an `ESP_LOGI` recovery line. This
  is a small self-heal: a transient allocation failure at boot
  doesn't have to be permanent.
- No new dependencies, no API breakage, no schema change. The
  `/api/mqtt` response gains three new top-level fields; existing
  consumers ignore unknown fields.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `mqtt-integration`: the "Connection management" requirement gets a
  new scenario that asserts "buffer state is observable from the
  API" (degraded flag, current size, truncation counter). The
  underlying behaviour (set buffer, warn on failure) is preserved —
  this is a new observation, not a behavioural change.
- `http-api`: the "MQTT endpoints" requirement's scenario for
  `GET /api/mqtt` is widened to require the three new fields
  (`buffer_size`, `buffer_degraded`, `truncated_publishes`) in
  the response. Existing fields (host, port, username, prefix,
  interval, enabled, stats) are unchanged.

## Impact

- **Source files (refactor)**:
  - `src/MqttClient.h` — add `truncatedPublishes` counter,
    `getBufferSize()` and `isBufferDegraded()` accessors,
    `bufferSize` and `bufferDegraded` private fields.
  - `src/MqttClient.cpp` — capture `getBufferSize()` after
    `setBufferSize()`; set `bufferDegraded`; log explicit
    truncation line in `publish()`; increment
    `truncatedPublishes`; retry `setBufferSize()` in `loop()`.
  - `src/Network.cpp` — pass the new fields to `/api/mqtt` via
    the existing stats block (or a new sibling block).
  - `src/routes/MqttRoutes.cpp` — add the three new fields to
    the `GET /api/mqtt` JSON response.
- **Spec files (text update)**:
  - `openspec/specs/mqtt-integration/spec.md` — add a
    buffer-state-observable scenario under the "Connection
    management" requirement (or a new requirement if the
    existing structure doesn't fit cleanly).
  - `openspec/specs/http-api/spec.md` — widen the
    `GET /api/mqtt` scenario to require the three new fields.
- **Tests** (additions, not changes):
  - `test/test_mqtt_client/test_mqtt_client.cpp` — add unit
    tests for `getBufferSize()` / `isBufferDegraded()` defaults,
    and a stubbed test that simulates a degraded buffer (the
    native test build doesn't link PubSubClient, so we test
    the fields directly).
- **No dependency changes** — `bblanchon/PubSubClient@^2.8` is
  unchanged; we use the existing `getBufferSize()` method.
- **No JSON wire format breakage** — `/api/mqtt` only adds
  fields. Existing consumers (the web UI, scripts) ignore
  unknown fields per the JSON contract.
- **Out of scope**: changing the requested 1024-byte buffer
  size, switching MQTT libraries, handling OTA-induced
  fragmentation, or moving the buffer from heap to a custom
  allocator.
