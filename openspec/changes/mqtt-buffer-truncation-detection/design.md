## Context

`MqttClient::begin()` asks PubSubClient for a 1024-byte TX/RX
buffer via `setBufferSize(MQTT_BUFFER_SIZE)`
(`src/MqttClient.cpp:39-55`). PubSubClient heap-allocates this
buffer; the ESP32-S2 has no PSRAM and the heap is shared with
every other long-lived object (the `WebServerManager`, the
`SensorController`, the `Measurements` vector, the JSON
documents on the request hot path, the `OTAUpdater` GitHub
payload filter, …).

On a fragmented heap — exactly the situation the firmware
runs into on a long-uptime single-core ESP32-S2 — the
allocation fails. The current code logs a warning and
proceeds; from that point on, every publish goes through
the compile-time 256-byte default buffer
(`PubSubClient.h: MQTT_MAX_PACKET_SIZE = 256`). When a
measurement payload approaches that ceiling
(topic ~150 + JSON ~120 + MQTT framing overhead), PubSubClient
returns `false` from `publish()` without ever touching the
network. `MqttClient::publish()` propagates the `false`;
`Network::publishMeasurements()` increments `failed++` and
emits a generic "N failed" log line; the broker never sees
those measurements. From the outside, the system *looks*
working: the WiFi is up, the broker is connected, the
publish cadence is steady — but payloads are quietly
disappearing.

Three things are missing today:

1. **Detection** — there is no field anywhere on `MqttClient`
   that records "we tried to allocate 1024 bytes, we got N",
   and no flag that says "the buffer is smaller than we
   asked for, treat publish failures as likely-truncation".
2. **Diagnosis** — the `ESP_LOGW` line in `begin()` is the
   only signal, and it fires once at boot; the only ongoing
   signal is the `failed++` counter, which is mixed in
   with transient broker-rejected / network errors.
3. **Recovery** — there is no retry of `setBufferSize()`,
   so a transient allocation failure at boot is permanent
   for the lifetime of the device.

## Goals / Non-Goals

**Goals:**

- Make the buffer state observable: capture the actual
  buffer size PubSubClient ends up with, expose it via a
  getter, and surface it in `/api/mqtt`.
- Distinguish buffer-truncation failures from other
  publish failures: a dedicated `truncatedPublishes` counter
  increments only when `publish()` returns `false` AND
  `bufferDegraded` is set.
- Log a clear, identifiable error in the serial log when a
  publish is suspected to have been truncated, so the user
  can correlate `/api/mqtt` truncation counts against the
  serial timeline.
- Self-heal on the reconnect path: retry `setBufferSize()`
  when the heap may be less fragmented (e.g. after a
  successful broker (re)connect), and clear the degraded
  flag on success.
- No JSON wire-format breakage (additive only). No
  dependency changes. No API method removals.

**Non-Goals:**

- Changing the requested 1024-byte buffer size. The size
  was chosen to fit `topic (~150) + JSON payload (~256) +
  MQTT framing`; bumping or shrinking it is a separate
  concern.
- Switching MQTT libraries or rewriting the publish path.
- Implementing a custom stack allocator for the MQTT
  buffer (consistent with the project's
  ArduinoJson 7 decision from the previous change: prefer
  the library default over plumbing a custom allocator).
- Changing how `/api/mqtt` reports the broker connection
  state (already covered by `connected` in the existing
  stats block).
- Detecting truncation on the receive path. PubSubClient's
  RX buffer is the same allocation; we treat the TX side
  as the leading indicator. A subscribe-based consumer
  would need its own change.

## Decisions

### D1. Capture `getBufferSize()` once, right after `setBufferSize()`

In `MqttClient::begin()`, after the `setBufferSize()` call,
read the actual size back via `mqttClient.getBufferSize()`.
Store it in a private `uint16_t bufferSize = 0` field.
Set `bool bufferDegraded = (bufferSize < MQTT_BUFFER_SIZE)`.
This is the source of truth for "what is PubSubClient
actually working with right now".

**Rationale.** PubSubClient's `getBufferSize()` is the
documented accessor; it returns whatever was last set by
`setBufferSize()` (or the compile-time default of 256 if no
call has been made). Reading it after `setBufferSize()`
gives the runtime value, not the requested value.

**Alternatives considered**

- *Call `getBufferSize()` on every publish.* Wasteful —
  the value only changes when `setBufferSize()` is called.
  One capture at `begin()` plus one on every successful
  reconnect is enough.
- *Track the requested size, not the actual size.*
  Rejected — the whole point is to surface that the
  *actual* size diverges from the requested size.

### D2. New `truncatedPublishes` counter, separate from `failedCount`

Add `uint32_t truncatedPublishes = 0;` to `MqttClient`,
along with a `getTruncatedPublishes()` getter. In
`MqttClient::publish()`, increment it when
`mqttClient.publish(...)` returns `false` AND
`bufferDegraded` is `true`. Do NOT increment `failedCount`
for the same event — `Network::publishMeasurements()`
already increments `failedCount`, and counting the same
event in two places would double-count.

**Rationale.** `failedCount` is the catch-all for
"something went wrong with this publish". `truncatedPublishes`
is the specific subset "this publish was likely lost to a
buffer-too-small condition". The two are disjoint by
construction, and surfacing both lets the operator tell
"broker rejected" / "network blip" (counted in
`failedCount` only) apart from "buffer truncated"
(counted in both `truncatedPublishes` and `failedCount`).

### D3. Surface three new fields in `GET /api/mqtt`

Add three top-level fields to the JSON response in
`src/routes/MqttRoutes.cpp`:
- `buffer_size` (uint16) — `mqtt->getBufferSize()`
- `buffer_degraded` (bool) — `mqtt->isBufferDegraded()`
- `truncated_publishes` (uint32) — `mqtt->getTruncatedPublishes()`

Place them as siblings of the existing `stats` block, not
inside it: `stats` is per-cycle / per-publish accounting,
while these three are device-state / lifetime counters
that don't fit the cycle model.

**Rationale.** The web UI already reads `connected` and
`stats.*`; adding three more top-level fields is a small
JSON contract change that any consumer can ignore.
Keeping them outside `stats` avoids the cycle / lifetime
naming confusion.

### D4. Periodic `setBufferSize()` retry in `loop()`

In `MqttClient::loop()`, immediately after a successful
MQTT `connect()` (i.e. on the path that logs "Connected
to …"), attempt `mqttClient.setBufferSize(MQTT_BUFFER_SIZE)`.
Read `getBufferSize()` back; if it now matches the
requested size, clear `bufferDegraded` and emit
`ESP_LOGI(TAG, "MQTT buffer recovered: %u bytes",
bufferSize)`. If it still doesn't match, leave
`bufferDegraded` set and emit nothing (to avoid log spam).

**Rationale.** The retry only runs on a successful
(re)connect, which is a moment when the heap is likely
slightly less fragmented (the prior failed-socket cleanup
in `loop()` may have freed a WiFiClient buffer, and the
TCP connect itself consumes and releases a small amount
of stack). Bounding the retry to "on connect" (not on a
timer) keeps the cost off the hot path and means the
recovery happens at moments the operator already cares
about (broker state changes).

**Alternatives considered**

- *Retry on every `loop()` call.* Too noisy. The buffer
  won't free up unless something actually fragments /
  defragments the heap, and the most likely time for
  that is the (re)connect path itself.
- *Retry on a timer (e.g. every 60 s).* Rejected — the
  cost (an allocation attempt) is small but non-zero,
  and the recovery window is opportunistic, not periodic.
  Per-connect is enough.
- *Defensive: never retry, just surface the state.*
  Rejected — the user explicitly asked for "device appears
  to work" to be replaced with a recoverable failure mode.

### D5. Native-test gap

PubSubClient is not linked in the `native` PlatformIO
environment, so the buffer-size code path is only
exercised on-device. The new fields (`bufferSize`,
`bufferDegraded`, `truncatedPublishes`) are plain C++
fields on `MqttClient`; unit tests can:
- Assert the default values (`bufferSize = 0`,
  `bufferDegraded = false`, `truncatedPublishes = 0`).
- Assert `getBufferSize()` and `isBufferDegraded()`
  return what was set.
- Skip the actual `setBufferSize()` / `getBufferSize()`
  round-trip (it requires `PubSubClient`, which is
  `ARDUINO`-gated).

This keeps the existing test suite green while adding
coverage for the new fields. A device-side smoke test
that forces fragmentation (e.g. by allocating and freeing
in a loop) is out of scope for the change but would be a
natural follow-up.

## Risks / Trade-offs

- **Risk:** The retry on connect might succeed and
  reallocate a buffer that the broker is mid-write into.
  **→** Mitigation: `setBufferSize()` in PubSubClient
  only reallocates if the requested size differs from the
  current size. If we're recovering from 256 → 1024, the
  new buffer is bigger and PubSubClient handles the
  transition internally. If the connect just succeeded,
  there is no in-flight publish. The risk is negligible.
- **Risk:** `truncatedPublishes` could be misread as
  "broker dropped N publishes" rather than "firmware
  refused N publishes". **→** Mitigation: the field name
  is `truncated_publishes`, not `failed_publishes`; the
  doc string and the spec scenario will call out the
  distinction explicitly.
- **Risk:** Operators who relied on the previous generic
  "N failed" log line might be surprised by the new
  `ESP_LOGE` for buffer-truncation. **→** Acceptable
  trade-off — the surprise is the point; it surfaces
  silent failures. The `ESP_LOGE` rate is bounded by the
  publish cadence, not by anything else.
- **Risk:** `setBufferSize()` retry on connect adds a
  tiny allocation attempt to the hot reconnect path.
  **→** Mitigation: the allocation is one `malloc(1024)`
  per successful (re)connect — once per broker cycle, not
  per request. Cost is negligible.

## Migration Plan

This is an additive refactor; no data migration and no
runtime deploy step beyond rebuilding and re-flashing.

1. Apply the field/accessor changes in `MqttClient.h`
   and the implementation changes in `MqttClient.cpp`.
2. Wire the three new fields into `GET /api/mqtt` in
   `src/routes/MqttRoutes.cpp`.
3. Add unit tests in `test/test_mqtt_client/test_mqtt_client.cpp`
   for the new accessors (default values; field updates
   via the public `recordPublishResult`-style API).
4. Build for ESP32: `pio run -e adafruit_qtpy_esp32s2`
   (expect zero new warnings).
5. Run native tests: `pio test -e native` (expect
   233 + N new tests, all green).
6. Update `openspec/specs/mqtt-integration/spec.md` and
   `openspec/specs/http-api/spec.md` deltas.
7. Archive the change (`/opsx:archive`).

**Rollback.** Revert the source + spec commits. The
`/api/mqtt` response is additive; consumers ignore the
removed fields. The `MqttClient` accessors are new and
have no callers outside the new code. No data
migration; NVS schema is untouched.

## Open Questions

None. The detection logic, the counter split, the API
shape, and the retry placement all follow directly from
the existing `MqttClient` API surface and the existing
`/api/mqtt` JSON contract.
