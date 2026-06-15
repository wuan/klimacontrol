# http-api Specification (delta)

## Purpose

The http-api spec describes the route surface of the device's
web interface. This delta adds two requirements: one formalizing
the existing implicit rule that route handlers must not allocate
per-request from the heap (previously unstated), and one
documenting the new `largest_free_block` field in `/api/status`.

## ADDED Requirements

### Requirement: Request handler allocation discipline

The firmware SHALL build and serialize every HTTP request
handler's JSON response using a `StaticJsonDocument` placed on
the stack frame. The firmware SHALL NOT call
`std::make_unique<JsonDocument>()`, `new JsonDocument`, or any
other heap allocator in a request handler that is freed before
the handler returns. Body parsing MUST use the same stack
pattern.

#### Scenario: GET handler builds a status response

- **WHEN** a client GETs `/api/status`
- **THEN** the handler constructs a stack-allocated
  `StaticJsonDocument<512>`, populates the keys, and calls
  `serializeJson` into a `String`; no heap allocation for the
  JSON document itself occurs during the handler's execution

#### Scenario: POST handler parses a JSON body

- **WHEN** a client POSTs to `/api/wifi` (or any other state-
  changing endpoint) with a valid CSRF header
- **THEN** the body handler uses a stack-allocated
  `StaticJsonDocument` to deserialize the body and a separate
  stack-allocated `StaticJsonDocument` to build the response;
  no per-request heap allocation for JSON document objects
  occurs

### Requirement: `/api/status` schema includes `largest_free_block`

The `/api/status` JSON response SHALL include a `largest_free_block`
field (in bytes) sourced from
`heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`, alongside the
existing `free_heap` field.

#### Scenario: Status payload contains the field

- **WHEN** a client GETs `/api/status`
- **THEN** the parsed JSON contains an integer-valued
  `largest_free_block` key whose value is greater than or equal
  to zero and less than or equal to `free_heap`

#### Scenario: Field is omitted in unit tests that stub heap APIs

- **WHEN** the handler is invoked from a native test environment
  that does not link the ESP-IDF `heap_caps_*` symbols
- **THEN** the field is reported as zero (or the response is
  documented to omit the key in the test build) so the
  contract remains testable without an ESP32 target
