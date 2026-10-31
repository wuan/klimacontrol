# Spec Delta

## ADDED Requirements

### Requirement: JSON responses are streamed without an intermediate String

The firmware SHALL NOT serialize a route handler's `JsonDocument` into an intermediate `String` (or any other growable heap string) before sending. The serialized bytes SHALL go directly into the `AsyncResponseStream` owned by the response object, sized to the exact `measureJson()` length (+1 for the ring buffer's full/empty sentinel), so one HTTP request costs one right-sized heap allocation for the response body instead of String growth plus a response copy. The response bytes on the wire SHALL be identical to the previous `String` path.

#### Scenario: Concurrent status polls do not fragment the heap

- **WHEN** several clients poll `GET /api/status` concurrently
- **THEN** each request allocates exactly one response buffer sized to its measured JSON length, freed when the send completes, and no per-request `String` allocation occurs

### Requirement: The device ID is a cached buffer, not a String per call

`DeviceId::getDeviceId()` and `DeviceId::getMacAddress()` SHALL return a `const char*` into storage that lives for the device's lifetime (cached on first call), not a `String` constructed per call. Callers SHALL use the returned pointer directly and SHALL NOT copy it into a `String` unless a `String` is genuinely required by the callee's interface.

#### Scenario: Discovery publish does not allocate for the device ID

- **WHEN** `MqttPublisher::publishDiscovery()` builds the Home Assistant discovery message
- **THEN** the device ID it embeds comes from the cached buffer, with no `String` temporary allocated for it

### Requirement: The largest one-shot MQTT payload uses static storage, not the task stack

The Home Assistant discovery payload in `MqttPublisher::publishDiscovery()` — the one publish that can reach `MqttClient::MQTT_BUFFER_SIZE` (2048 B) — SHALL be formatted into static storage, not a buffer on the Network task's stack, because the Network task stack is 8 KiB of internal SRAM with a measured ~3.5 KiB peak and a 2048 B stack buffer would roughly double that peak. The MQTT TX buffer size itself SHALL remain 2048 B: the worst-case discovery payload (63-char prefix, 31-char device name, 20-char version) measures 1787 B, 1828 B framed, so a smaller buffer would truncate the retained discovery message.

#### Scenario: Discovery publish on connect

- **WHEN** an MQTT (re)connect triggers `publishDiscovery()`
- **THEN** the payload is formatted into the static buffer and the Network task stack high-water mark does not include the payload buffer
