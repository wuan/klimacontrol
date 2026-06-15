## Context

The Klima-Control firmware runs on an ESP32-S2 with ~320 KB of SRAM, no
PSRAM, and a long expected uptime (the device is meant to live on a
shelf for months between reboots). Heap fragmentation is currently an
implicit, unmeasured risk. The proposal names three concrete
fragmentation sources (recreated web server / MQTT client, per-request
`JsonDocument`, growing `SensorController` vectors) and the failure
mode they cause (`largest_free_block` shrinks until an allocation
fails mid-OTA, mid-MQTT publish, or mid-HTTP response).

This document captures the technical decisions for the refactor: which
classes get their lifetime changed, what the new route-swap API looks
like, and how the new `largest_free_block` metric is wired into the
existing status JSON. It is the bridge between the proposal (why) and
the tasks (what to type).

## Goals / Non-Goals

**Goals**

- Make heap fragmentation a measurable, testable property of the
  firmware, not a regression detected only at the OTA / MQTT / HTTP
  failure boundary.
- Eliminate all `std::make_unique<...>` calls in the long-lived path
  that run more than once per boot (web server, MQTT client).
- Eliminate per-request heap allocation in HTTP route handlers.
- Reserve the sensor vector capacity at boot so the only post-boot
  allocations are the `Measurement` payloads themselves.
- Land the change with a small, reviewable diff. No new dependencies,
  no JSON library swap, no allocator change.

**Non-Goals**

- Replacing `std::vector` with a custom arena / pool. The capacity
  reserve is enough for the sensor list; if `currentMeasurements`
  churn becomes the next bottleneck it can be a follow-up.
- Changing the JSON serialization library. `StaticJsonDocument` is
  already in the dependency tree.
- Touching the per-measurement `Measurement` struct layout.
- Reworking the FreeRTOS task structure (covered by
  `system-architecture`).
- Changing the OTA partition layout, rollback policy, or download
  streaming (covered by `ota-updates`).

## Decisions

### D1. Collapse the two `WebServerManager` subclasses into the base

**Decision.** Make `WebServerManager` itself route-agnostic. Replace
`setupConfigRoutes()` / `setupAPIRoutes()` with a single
`setMode(WebServerMode mode)` method that internally calls the
appropriate `setupXxxRoutes()` and exposes `clearRoutes()` to drop
the prior registration. Remove `ConfigWebServerManager` and
`OperationalWebServerManager`.

**Rationale.** The current design forces `unique_ptr<WebServerManager>`
to be destroyed and reallocated when the mode flips because the two
subclasses have different types. The proposed `Network::switchToConfigMode()`
/ `switchToOperationalMode()` methods on the *same* instance are only
possible if the base class owns the mode logic. Route handlers
themselves don't need a subclass — they read the same
`ConfigManager` / `SensorController` references regardless of mode.

**Alternatives considered.**

- *Type-erased `unique_ptr<WebServerManager>` with a virtual
  `switchMode()` on each subclass.* Same outcome, more code, two
  classes to maintain. Rejected.
- *Keep both subclasses, store a `unique_ptr<BaseClass>` instead.*
  Same as current. Rejected because it doesn't actually change
  lifetime — the `unique_ptr` still gets re-pointed on every mode
  switch.

### D2. Reuse the same `WebServerManager` instance across AP/STA/STA-fallback cycles

**Decision.** `Network::task()` no longer calls
`std::make_unique<ConfigWebServerManager>(...)` three times. Instead
it owns one `webServer` (default-constructed) and calls
`webServer.setMode(WebServerMode::CONFIG)` /
`setMode(WebServerMode::OPERATIONAL)`. The instance is created
in `Network::begin()` (called from `setup()`).

**Rationale.** Every `make_unique` followed by `reset()` carves a hole
in the heap the size of the largest `WebServerManager` (the route
table is the bulk of it). A `AsyncWebServer` object is non-trivial
on the ESP32 (it owns an `AsyncServer`, a list of handlers, a
request queue). Keeping one alive for the whole boot means a single
allocation, in a single block, that never gets freed.

**Alternatives considered.**

- *A pre-allocated memory pool for the `WebServerManager` and a
  placement-new swap.* Overkill for a single instance. Rejected.
- *Re-create the server on each mode flip and accept the
  fragmentation.* This is the current behaviour and the bug we are
  fixing. Rejected.

### D3. Use `StaticJsonDocument<512>` on the route-handler stack

**Decision.** Each handler in `src/routes/*.cpp` declares a
`StaticJsonDocument<512>` locally, calls `serializeJson(doc, response)`
into a `String`, and passes `response.c_str()` to
`request->send(...)`. The two existing
`std::make_unique<JsonDocument>()` calls in
`WebServerManager::handleWiFiConfig` are replaced with the same
pattern.

**Rationale.** `StaticJsonDocument<N>` is a stack-backed
`JsonDocument` with a fixed-size buffer on the stack. It performs no
heap allocation for the document itself; the 512-byte buffer is
inside the stack frame. The route handlers are all on the Network
task (10 KB stack), so a 512-byte transient is well under budget.
The existing `512` byte ceiling in
`openspec/specs/http-api/spec.md` already constrains the JSON size;
nothing about that changes.

**Alternatives considered.**

- *A process-wide thread-local `JsonDocument` reused across
  requests.* Avoids even the constructor/destructor cost, but adds
  a `thread_local` indirection, requires a `StaticJsonDocument`
  size picked at process level (the per-route size would be a lie),
  and complicates unit testing of the route logic. Rejected for
  clarity.
- *A heap-allocated `DynamicJsonDocument` with a `reserve()`. *
  Still calls `malloc` / `free` per request. Rejected — same
  fragmentation profile as today.
- *Switching to a streaming serializer that writes directly into the
  response.* Best long-term option but requires replacing
  `ArduinoJson` and changing the response model. Out of scope.

### D4. Expose `largest_free_block` via the status endpoint, not a new endpoint

**Decision.** Add a `largest_free_block` field (in bytes) to the
existing `/api/status` JSON. No new route, no new auth model, no new
documentation page.

**Rationale.** The existing web UI already polls `/api/status` (see
`StatusRoutes.cpp`); adding a field is a 2-line change on the
backend and a 5-line change on the frontend. A new endpoint would
force the UI to fetch it separately and the operator to know it
exists.

**Alternatives considered.**

- *A dedicated `/api/heap` endpoint with `free_heap`,
  `min_free_heap`, `largest_free_block`, and `heap_integrity`.*
  Cleaner REST, but the data is co-located with the other status
  fields and the UI already knows the shape. Rejected for now; can
  be added later if more heap metrics are wanted.
- *Logging `largest_free_block` to syslog on a 1-minute cadence
  instead of the API.* Misses the real-time visibility from the
  browser, and the web UI is the operator's primary surface.
  Rejected.

### D5. Reserve `SensorController` vectors in `main.cpp`, not in the controller constructor

**Decision.** Add a public `SensorController::reserveSensorSlots(size_t)`
method. `main.cpp` calls it after the I2C scan loop with
`MAX_KNOWN_SENSORS = 10` (the current upper bound; defined as a
`constexpr` next to the existing `MAX_SENSORS` constant in
`main.cpp`).

**Rationale.** The `SensorController` constructor doesn't know the
upper bound — that's a property of the firmware's sensor roster, not
of the controller. `main.cpp` is the only place that knows the
roster (the I2C scan loop right above it). Keeping the reserve call
there keeps the controller decoupled from the firmware's compile-time
sensor list.

**Alternatives considered.**

- *Hard-code `10` inside `SensorController::begin()`.* Hides the
  constant from the rest of the firmware, makes it harder to bump
  when a new sensor is added. Rejected.
- *Use `sensors.max_size()`.* Returns a platform-dependent large
  number; defeats the purpose. Rejected.

### D6. `largest_free_block` is the primary fragmentation signal

**Decision.** Surface `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`
in the status JSON. Do *not* add a fragmentation ratio or a "block
count" metric.

**Rationale.** `largest_free_block` is the metric that actually
predicts the next allocation failure — a fragmented heap with plenty
of total free space still fails on a `malloc` larger than the
biggest contiguous block. The block count is interesting for
debugging but noisy as a regression signal. A single number is
easier to alert on.

**Alternatives considered.**

- *Both `largest_free_block` and `heap_fragmentation_ratio`
  (`1 - largest_free_block / free_heap`).* More information but the
  ratio is meaningless when `free_heap` is small. Rejected.
- *Track and report fragmentation only via syslog.* Hidden from
  the operator. Rejected.

## Risks / Trade-offs

- **Stack usage of `StaticJsonDocument<512>` per request** → the
  Network task already has a 10 KB stack; 512 bytes per request is
  well under the 1 KB transient budget. The `verify-route-budget`
  native test (if added) should assert the stack frame stays under
  1 KB. Mitigation: the test in
  `test_memory/test_singleton_lifetimes.cpp` runs the request
  handler stub on the host and measures `sizeof` the document.

- **Route handlers that legitimately need a larger buffer** (e.g.
  `/api/sensors` returning 10 sensors × 4 measurements) may bump
  the 512-byte ceiling → if `serializeJson` overflows, it returns
  `0` and the response is empty. Mitigation: keep the existing
  `static_assert` / runtime check in the route that fails-fast to
  serial log instead of sending a half-truncated JSON. The
  `/api/sensors` payload size is already the limiting case in the
  spec; we re-validate it as part of this change.

- **The web server's `AsyncWebServer` may internally allocate per
  request** (handler chains, response objects) → that allocation is
  outside our control. Mitigation: we can audit
  `ESPAsyncWebServer` for any `new` in the request hot path; if
  found, file an upstream issue. The contract in the new
  `memory-management` spec is "our code does not allocate per
  request" — third-party code is out of scope.

- **`reserveSensorSlots(10)` is a guess** → if a future firmware
  adds an 11th sensor, the vector will reallocate. Mitigation: the
  `MAX_KNOWN_SENSORS` constant lives in `main.cpp` next to the
  scan loop, and a unit test in
  `test_memory/test_singleton_lifetimes.cpp` asserts that the
  reserve count matches the number of probe branches in the scan
  loop (a static check).

- **Switching route handlers via `setMode` is not atomic with
  `server.begin()`** → a request arriving during the swap could
  see a partial route table. Mitigation: stop the server
  (`server.end()`) before calling `setMode()` and re-`begin()` it
  after; matches the existing `begin()` / `end()` contract.

- **Subclass removal is a small breaking change for any out-of-tree
  consumer of the headers** → the project does not currently
  publish a stable ABI, and the headers are `class WebServerManager`
  not `extern "C"`. Mitigation: noted in the proposal under
  "Impact" — no migration steps needed inside the repo.

## Migration Plan

The change is a refactor with no externally observable behaviour
change (other than the new `largest_free_block` field in
`/api/status`). It deploys and rolls back like any firmware
upgrade:

1. Land as a single PR. The CI matrix (`pio run -e
   adafruit_qtpy_esp32s2` + `pio test -e native`) must stay green.
2. OTA-deploy to a single test device. Confirm `largest_free_block`
   in `/api/status` is reported, the web UI still loads, and the
   heap stability log line (added as a debug print, canary only) is
   flat over a 24-hour soak.
3. Roll back by OTA-restoring the previous release. No NVS schema
   change, so rollback is safe.

## Open Questions

- Should the `largest_free_block` field appear in the public web UI
  by default, or behind a `?debug=1` query param? Defaulting to
  visible is simpler, but a 4-byte integer in a polling UI may
  confuse non-technical users. **Decision deferred to the implementer**
  — proposal says "no behaviour change visible to users other than
  the new `largest_free_block` field", which implies visible. The
  tasks step for the frontend change can decide the exact placement.
- Should `MqttClient` re-init (`Network::task` recreating the
  client on every STA reconnect) be folded into the same
  "long-lived singleton" rule, or left as-is because it is
  idempotent and small? **To be confirmed in the
  `MqttClient::begin()` audit** (task T1.3 below).
- The `JsonDocument<512>` ceiling is currently the
  `http-api` spec's hard limit. `/api/sensors` at full saturation
  is borderline. Should the ceiling be raised as part of this
  change, or is that a separate concern? **Deferred** — the
  current tasks step re-validates the existing 512-byte ceiling
  and flags any overflow during the audit. If overflow is
  observed, raising the ceiling is a follow-up change.
