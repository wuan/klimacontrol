## MODIFIED Requirements

### Requirement: Request handler allocation discipline

The firmware SHALL build and serialize every HTTP request handler's JSON response using a `JsonDocument` placed on the handler's stack frame. The `JsonDocument` object itself MUST be stack-allocated; variable-length data the document holds is allocated via the ArduinoJson default allocator (`heap_caps_malloc` / `free` on ESP32, `malloc` / `free` on the host) and is freed when the document is destroyed at handler return. The firmware SHALL NOT call `std::make_unique<JsonDocument>()`, `new JsonDocument`, `DynamicJsonDocument`, or any other heap allocator for the document object itself in a request handler. Body parsing MUST use the same stack pattern.

#### Scenario: GET handler builds a status response

- **WHEN** a client GETs `/api/status`
- **THEN** the handler constructs a stack-allocated `JsonDocument`, populates the keys, and calls `serializeJson` into a `String`; the `JsonDocument` object is on the handler's stack frame and any data it holds is allocated via the default allocator and freed at handler return

#### Scenario: POST handler parses a JSON body

- **WHEN** a client POSTs to `/api/wifi` (or any other state-changing endpoint) with a valid CSRF header
- **THEN** the body handler uses a stack-allocated `JsonDocument` to deserialize the body and a separate stack-allocated `JsonDocument` to build the response; the `JsonDocument` objects are on the handler's stack frame and any data they hold is allocated via the default allocator and freed at handler return
