# Spec Delta

## MODIFIED Requirements

### Requirement: JSON responses are streamed without an intermediate String

The firmware SHALL NOT serialize a route handler's `JsonDocument` into an intermediate `String` (or any other growable heap string) before sending. The serialized bytes SHALL go directly into the `AsyncResponseStream` owned by the response object, sized to the exact `measureJson()` length — the `cbuf` ring buffer that backs the stream already reserves its own full/empty sentinel outside that length. No per-request `String` is allocated for the response body, and the response bytes on the wire SHALL be identical to the previous `String` path.

#### Scenario: Concurrent status polls do not fragment the heap

- **WHEN** several clients poll `GET /api/status` concurrently
- **THEN** each request allocates exactly one response buffer sized to its measured JSON length (no `+1` slack), freed when the send completes, and no per-request `String` allocation occurs

## REMOVED Requirements

### Requirement: The largest one-shot MQTT payload uses static storage, not the task stack
**Reason**: On the ESP32-S2 the heap begins at the end of BSS, so the 2048 B static buffer permanently removed 2048 B from `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` while the Network task's 8 KiB stack had ample headroom — a net regression on the resource the requirement aimed to protect.
**Migration**: See the new requirement "The largest one-shot MQTT payload is transient, not static storage".

## ADDED Requirements

### Requirement: The largest one-shot MQTT payload is transient, not static storage

The Home Assistant discovery payload in `MqttPublisher::publishDiscovery()` — the one publish that can reach `MqttClient::MQTT_BUFFER_SIZE` (2048 B) — SHALL be formatted into transient storage (the Network task's stack), not a permanent static/`.bss` buffer. `MqttClient::MQTT_BUFFER_SIZE` SHALL remain 2048 B.

#### Scenario: Discovery publish on connect

- **WHEN** an MQTT (re)connect triggers `publishDiscovery()`
- **THEN** the payload is formatted into a transient stack buffer that is released when `publishDiscovery()` returns, and no permanent static buffer is reserved for it
