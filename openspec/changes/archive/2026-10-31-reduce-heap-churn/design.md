# Design

## Context

Single-core ESP32-S2, ~166 KB internal heap at runtime. The
constrained resource is internal SRAM, not flash (app partition has ~420 KB
free) and not PSRAM (2 MB, but not usable for lwIP/WiFi/DMA/task-stack
allocations). Two churn sources matter: per-request heap traffic on the
AsyncTCP event task, and temporary `String` allocations on the Network task.

## Decisions

### 1. Stream JSON responses with `AsyncResponseStream`, sized by `measureJson`

`AsyncResponseStream` (ESPAsyncWebServer 3.x) implements `Print` and owns a
`cbuf` ring buffer; `request->send(response)` transfers ownership and the
library frees it when the send completes. ArduinoJson's
`serializeJson(doc, Print&)` writes into it directly.

A shared helper in `routes/RouteHelpers.h`:

```cpp
inline void sendJson(AsyncWebServerRequest* request, JsonDocument& doc, int code = 200) {
    const size_t len = measureJson(doc);
    // cbuf keeps one byte to distinguish full from empty: capacity must be len + 1.
    auto* response = new AsyncResponseStream(CONTENT_TYPE_JSON, len + 1);
    response->setCode(code);
    serializeJson(doc, *response);
    request->send(response);
}
```

The `JsonDocument` itself stays on the handler's stack frame (the existing
memory-management requirement is unchanged); only the serialized bytes go to
the single right-sized buffer. Versus the old path (ArduinoJson growing a
`String`, then `AsyncBasicResponse` copying that `String`), this is one
allocation instead of several, and peak heap during a request drops from
~2x payload to ~1x payload. `measureJson` is a pure walk, no allocation.

Non-200 JSON responses (`202`/`409` on OTA check, `500` on OTA confirm)
keep their status codes via the public `AsyncWebServerResponse::setCode()`.

### 2. Cache the device ID instead of returning `String`

`DeviceId::getDeviceId()` derives a 6-hex-char ID from `ESP.getEfuseMac()`,
which never changes at runtime. It now formats into a function-local
`static char[7]` on first call and returns `const char*`. All call sites
updated (`MqttClient`, `MqttPublisher`, `MdnsAdvertiser`, `ApProvisioning`,
`WebServerManager`). The first call happens during `setup()` (MqttClient
construction), before any task exists, so the lazy init has no race in
practice; even if two tasks raced, they write identical bytes.

### 3. Format IPs into stack buffers

A small `support/IpFormat.h` helper renders an `IPAddress` with
`snprintf(buf, len, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3])` — the same
pattern `ApProvisioning` already used inline. Used in request logging
(`AccessLogger`), WiFi connect logs, captive-portal log, and the
`ip_address`/`ap_ip` fields of `/api/status` and `/api/about`. Output format
is byte-identical to `IPAddress::toString()`.

### 4. HA discovery payload buffer: static, not stack

`publishDiscovery()` runs only on the Network task, so a file-static
`char[MQTT_BUFFER_SIZE]` is single-threaded by construction. This removes a
2048 B spike from the 8 KiB Network task stack (measured peak 3544 B).
`MQTT_BUFFER_SIZE` stays 2048: the worst-case discovery payload (63-char
prefix, 31-char name, 20-char version) measures 1787 B, 1828 B framed, so
2048 is the smallest round size above the pinned >=128 B headroom.

## Alternatives considered

- Shrinking `MQTT_BUFFER_SIZE` to 1024: rejected — the worst-case discovery
  payload alone is 1787 B; it would truncate the retained message.
- Temporarily enlarging the PubSubClient buffer only for the discovery
  publish: rejected — `setBufferSize()` reallocates on every call, adding
  heap churn on every connect for a 1 KB steady-state saving.
- Chunked responses (`beginChunkedResponse`): rejected — the chunk callback
  can run after the handler returns, but the `JsonDocument` must live on the
  handler stack (spec), so it cannot outlive the handler.
