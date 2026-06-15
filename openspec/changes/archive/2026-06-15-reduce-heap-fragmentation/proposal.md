## Why

The ESP32-S2 has ~320 KB of contiguous SRAM with no PSRAM, and the firmware
currently allocates and frees several long-lived objects repeatedly during
operation:

- `WebServerManager` is destroyed and reconstructed via `std::make_unique`
  three times in `Network::task()` during AP-mode entry, AP-fallback, and
  STA-mode boot (see `src/Network.cpp:305,375,442`). The two subclasses
  (`ConfigWebServerManager`, `OperationalWebServerManager`) are different
  types, so the existing `unique_ptr<WebServerManager>` cannot just be
  re-pointed.
- `MqttClient` is likewise reconstructed on every successful STA connect
  (`src/Network.cpp:277`).
- `std::make_unique<JsonDocument>()` runs on every HTTP request body
  parse and every response build (`src/WebServerManager.cpp:115,156` and
  the route files under `src/routes/`).
- `SensorController::sensors` is a `std::vector` that grows as sensors
  are discovered at boot (`src/main.cpp:79-97`), and `currentMeasurements`
  grows as readings arrive.

Each allocate/free cycle is small but, on a single-core MCU with no
PSRAM and no defragmenting allocator, leaves holes in the heap. After
weeks of WiFi reconnect cycles, MQTT reconnects, and hundreds of
thousands of `JsonDocument` requests, the largest free block (`largest
free block` reported by `heap_caps_get_largest_free_block`) shrinks
even though `ESP.getFreeHeap()` looks healthy. The eventual symptom is
an allocation failure inside the OTA stream, the MQTT ring buffer, or
the next `AsyncWebServerRequest` reply — a long-running crash with no
obvious cause.

The existing `system-architecture` spec only guards against the
end-stage failure (free heap < 16 KB triggers a restart). It does not
constrain the *shape* of the heap, the lifetime of long-lived objects,
or the per-request allocation pattern.

## What Changes

- **Long-lived singletons stay long-lived.** Construct `WebServerManager`
  (and the route table), `MqttClient`, and the sensor list once at
  boot, in `setup()` after configuration load. Switch the active
  server by calling `setupConfigRoutes()` / `setupAPIRoutes()` on the
  same instance, instead of `make_unique` + `reset()`. Use a single
  polymorphic `WebServerManager` base that can hold both route sets.
- **Mode transitions stop creating `JsonDocument` on the hot path.**
  Build the response by reusing a thread-local `StaticJsonDocument`
  (or stack-allocated `StaticJsonDocument<512>`) inside each route
  handler instead of `std::make_unique<JsonDocument>()`. No
  per-request heap allocation.
- **Sensor list capacity is reserved up front.** `SensorController`
  exposes `reserveSensorSlots(n)` called from `main.cpp` with the
  known upper bound (currently 10) so the `std::vector` never
  reallocates after boot.
- **Heap stability is observable.** The system-architecture spec gains
  a new requirement: the firmware SHALL publish `largest_free_block`
  in the `/api/status` JSON (alongside `free_heap`) so a regression
  in fragmentation is visible from the existing web UI without a
  serial log scrape.
- **A new spec `memory-management`** captures the long-lived
  singleton, no-per-request-allocation, and `largest_free_block`
  obligations as a first-class capability.

## Capabilities

### New Capabilities

- `memory-management`: Defines the heap-stability contract for the
  firmware — long-lived objects are constructed once, HTTP request
  handlers do not allocate from the heap, vector capacities are
  reserved at boot, and `largest_free_block` is exposed for
  observability. The capability owns the cross-cutting concern that
  currently lives implicitly in `system-architecture`.

### Modified Capabilities

- `system-architecture`: Add a "Heap fragmentation observability"
  requirement requiring `largest_free_block` to be reported in
  `/api/status`. The existing "Memory budget" requirement already
  covers the upper/lower bound; the new requirement covers the
  *shape* of the free heap so fragmentation regressions are caught
  before they cause a restart.
- `http-api`: Add a "Request handler allocation discipline"
  requirement that route handlers MUST NOT call
  `std::make_unique<JsonDocument>()` (or any other non-pooled heap
  allocator) per request. Adds `largest_free_block` to the documented
  `/api/status` schema.

## Impact

**Code**

- `src/Network.h` / `src/Network.cpp` — remove the three
  `make_unique<...WebServerManager>(...)` reassignments; instantiate
  one server in `setup()` and call a `switchToConfigMode()` /
  `switchToOperationalMode()` method on it.
- `src/WebServerManager.h` / `src/WebServerManager.cpp` — collapse
  `ConfigWebServerManager` and `OperationalWebServerManager` into the
  base (or make `switchToConfigMode` / `switchToOperationalMode`
  virtual overrides on the base) and expose a `clearRoutes()` helper
  so route swaps don't accumulate handlers across mode flips.
- `src/MqttClient.h` / `src/MqttClient.cpp` — confirm
  re-initialization is safe; if not, move construction to `setup()`.
- `src/SensorController.h` / `src/SensorController.cpp` — add
  `reserveSensorSlots(size_t)` that calls
  `sensors.reserve(n)` and `currentMeasurements.reserve(n)`.
- `src/main.cpp` — call `reserveSensorSlots(MAX_KNOWN_SENSORS)` after
  the I2C scan loop, before the first `readSensors()`.
- `src/routes/*.cpp` — replace per-request
  `std::make_unique<JsonDocument>()` with stack-allocated
  `StaticJsonDocument<512>` (or a thread-local pool) inside each
  handler. JSON output is then built into a `String` and passed to
  `request->send`.
- `src/routes/StatusRoutes.cpp` — add `largest_free_block` to the
  status JSON.

**Specs**

- New `openspec/specs/memory-management/spec.md`.
- Delta under `openspec/changes/reduce-heap-fragmentation/specs/`
  for `system-architecture` and `http-api`.

**Tests**

- New native test `test_memory/test_singleton_lifetimes.cpp`
  asserting that the sensor vector does not reallocate after
  `reserveSensorSlots(N)` once `N` sensors are added.
- New native test `test_memory/test_status_payload.cpp` asserting
  that `largest_free_block` is present in the status JSON
  (compile-gated to `ARDUINO`, exercised via a stub).
- No new integration tests; the contract is observable in
  `/api/status` and the existing native tests cover the
  `SensorController` vector math.

**Risk / Non-goals**

- Not addressing the underlying `std::vector<std::unique_ptr>`
  reallocation cost in `SensorController::currentMeasurements`
  (rebuilt wholesale per cycle today) — the reserve call covers
  capacity, not churn.
- Not switching the JSON library or moving to a memory pool. The
  goal is to stop *adding* to the fragmentation budget, not to
  rewrite serialization.
- No behaviour change visible to users other than the new
  `largest_free_block` field in `/api/status`.
