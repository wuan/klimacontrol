# Spec Delta

## MODIFIED Requirements

### Requirement: Request handler allocation discipline

The firmware SHALL build and serialize every HTTP request handler's JSON response using a `JsonDocument` placed on the handler's stack frame. The `JsonDocument` object itself MUST be stack-allocated; variable-length data the document holds is allocated via the ArduinoJson default allocator (`heap_caps_malloc` / `free` on ESP32, `malloc` / `free` on the host) and is freed when the document is destroyed at handler return. The firmware SHALL NOT call `std::make_unique<JsonDocument>()`, `new JsonDocument`, `DynamicJsonDocument`, or any other heap allocator for the document object itself in a request handler. Body parsing MUST use the same stack pattern. The serialized response SHALL be written directly into the response object's stream (`AsyncResponseStream` sized to the `measureJson()` length) — the firmware SHALL NOT serialize into an intermediate `String` and pass that `String` to `send()`, which would allocate and copy the payload a second time per request.

#### Scenario: GET handler builds a status response

- **WHEN** a client GETs `/api/status`
- **THEN** the handler constructs a stack-allocated `JsonDocument`, populates the keys, and serializes it directly into the response stream via the shared `sendJson()` helper; the `JsonDocument` object is on the handler's stack frame and any data it holds is allocated via the default allocator and freed at handler return

#### Scenario: POST handler parses a JSON body

- **WHEN** a client POSTs to `/api/wifi` (or any other state-changing endpoint) with a valid CSRF header
- **THEN** the body handler uses a stack-allocated `JsonDocument` to deserialize the body and a separate stack-allocated `JsonDocument` to build the response; the `JsonDocument` objects are on the handler's stack frame and any data they hold is allocated via the default allocator and freed at handler return; the response is sent via the shared `sendJson()` helper without an intermediate `String`

#### Scenario: Non-200 JSON responses keep their status code

- **WHEN** a handler responds with a JSON body and a status other than 200 (e.g. 202/409 on `POST /api/ota/check`, 500 on `POST /api/ota/confirm`)
- **THEN** the response SHALL carry that status code with the same streamed-JSON body, via `AsyncWebServerResponse::setCode()` on the response stream
