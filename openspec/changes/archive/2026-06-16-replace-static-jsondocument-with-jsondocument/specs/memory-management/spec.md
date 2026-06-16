## REMOVED Requirements

### Requirement: HTTP route handlers do not allocate from the heap
**Reason**: Renamed and rewritten to reflect the actual allocation behaviour in ArduinoJson 7 — the `StaticJsonDocument<N>` type was reduced to a deprecated shim that heap-allocates on overflow, so the "no heap allocation" claim was always aspirational.
**Migration**: See the new requirement "HTTP route handlers place the JsonDocument on the stack" for the new contract.

## ADDED Requirements

### Requirement: HTTP route handlers place the JsonDocument on the stack

The firmware SHALL serialize every HTTP route handler's response under `src/routes/*.cpp` and `WebServerManager::handleWiFiConfig` using a `JsonDocument` placed on the handler's stack frame. The `JsonDocument` object itself MUST be stack-allocated; variable-length data the document holds is allocated via the ArduinoJson default allocator (`heap_caps_malloc` / `free` on ESP32, `malloc` / `free` on the host) and is freed when the document is destroyed at handler return. The firmware MUST NOT call `std::make_unique<JsonDocument>()`, `new JsonDocument`, `DynamicJsonDocument`, or any other heap allocator for the document object itself in a request handler.

#### Scenario: Status handler serves `/api/status`

- **WHEN** a client GETs `/api/status`
- **THEN** the handler builds the response with a stack-allocated `JsonDocument`; the `JsonDocument` object is on the handler's stack frame and any data it holds is allocated via the default allocator and freed at handler return

#### Scenario: WiFi config handler accepts a POST

- **WHEN** a client POSTs to `/api/wifi` with a valid CSRF header
- **THEN** the handler parses the body and serializes the response using stack-allocated `JsonDocument` instances; the `JsonDocument` objects are on the handler's stack frame and any data they hold is allocated via the default allocator and freed at handler return
