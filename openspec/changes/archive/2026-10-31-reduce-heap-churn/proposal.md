# Proposal

## Why

The ESP32-S2 has ~166 KB of internal heap at runtime (the rest of the 320 KB
DRAM is static data/BSS and WiFi/lwIP reservations; the 2 MB PSRAM pool is
separate and not usable for the allocations that fail under pressure — task
stacks, lwIP/WiFi structures, DMA buffers). Two patterns churn that pool on
every HTTP request and on common network paths:

1. Every JSON route handler serializes its response into an intermediate
   `String` (`serializeJson(doc, response)`) and then hands that `String` to
   `request->send(...)`, which copies it again into the response object.
   That is several heap allocations/frees per request (String growth plus
   the copy), and under concurrent requests it is exactly the transient dip
   the `LowHeapGuard` watches for.
2. `DeviceId::getDeviceId()` returns a `String` by value (one heap allocation
   per call) and log lines call `WiFi.localIP().toString()` (another
   temporary per line). Both are called on the Network task's hot paths
   (MQTT discovery publish, mDNS, request logging).

Also, `MqttPublisher::publishDiscovery()` formats the retained Home
Assistant discovery payload (up to `MqttClient::MQTT_BUFFER_SIZE` = 2048 B)
in a buffer on the Network task's 8 KiB stack, roughly doubling that task's
measured peak stack use for the duration of one connect.

## What Changes

- Route handlers serialize the stack `JsonDocument` directly into an
  `AsyncResponseStream` sized to the measured JSON length (`measureJson`),
  via a shared `sendJson()` helper in `routes/RouteHelpers.h`. One
  right-sized heap buffer per response, owned by the response object and
  freed when the send completes — no intermediate `String`, no second copy.
  Response bytes and status codes are unchanged.
- `DeviceId::getDeviceId()` / `getMacAddress()` return a `const char*` into a
  cached static buffer instead of a `String` by value; call sites updated.
- IP addresses in log lines and API payloads are formatted with `snprintf`
  into stack buffers instead of `IPAddress::toString()` temporaries.
- The HA discovery payload buffer in `MqttPublisher::publishDiscovery()`
  moves from the Network task stack to static storage. The MQTT TX buffer
  size itself stays at 2048 B: the worst-case discovery payload measures
  1787 B (1828 B framed), so 2048 is the smallest round size that keeps the
  pinned >=128 B headroom, and shrinking it would truncate the retained
  discovery message.

## Capabilities

### Modified Capabilities

- `http-api` — request handler allocation discipline: responses are
  serialized straight into the response stream, not into an intermediate
  `String`.
- `memory-management` — new requirement: no intermediate `String` between
  `JsonDocument` and the wire; device ID is a cached buffer; the largest
  one-shot MQTT payload uses static, not task-stack, storage.

### New Capabilities

- None.
