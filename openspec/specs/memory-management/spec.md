# memory-management Specification
## Purpose
The memory-management capability defines the firmware's contract for
keeping the heap stable over long uptimes on a single-core ESP32-S2
with no PSRAM. It captures the rules every capability must follow
when constructing long-lived objects, handling HTTP requests, and
growing in-process data structures, plus the observability hooks
that let an operator see fragmentation before it becomes a
crash.
## Requirements
### Requirement: Long-lived singletons are constructed once

The firmware SHALL construct each long-lived singleton — the
`WebServerManager`, the `MqttClient`, and the `SensorController`
— exactly once during boot, inside `setup()` or the equivalent
initialization phase called from `setup()`. After construction, the
firmware SHALL NOT destroy and reconstruct any singleton in response
to runtime events such as WiFi mode flips, AP-fallback, MQTT
reconnect, or sensor re-scan.

#### Scenario: AP-mode entry after STA failure

- **WHEN** the network task transitions from STA mode to AP mode
  (either at first boot, on AP-fallback, or after a configuration
  change)
- **THEN** the same `WebServerManager` instance is reused and
  switched to configuration routes via a method call; no
  `make_unique` / `reset` of the web server pointer occurs

#### Scenario: Operational server bring-up after STA connect

- **WHEN** the network task transitions from AP mode to STA
  operational mode
- **THEN** the same `WebServerManager` instance is reused and
  switched to operational routes via a method call; no
  `make_unique` / `reset` of the web server pointer occurs

#### Scenario: MQTT reconnect after broker drop

- **WHEN** the MQTT client disconnects and reconnects to the broker
- **THEN** the `MqttClient` instance is reused; only the
  reconnection handshake runs, not a re-construction

### Requirement: Vector capacities are reserved at boot

The firmware SHALL call `SensorController::reserveSensorSlots(n)`
from `main.cpp` (or the equivalent boot sequence) before the first
sensor is added, with `n` equal to the number of distinct sensor
probe branches in the I2C scan loop. After that call, adding up to
`n` sensors SHALL NOT cause the `sensors` vector to reallocate.

#### Scenario: I2C scan discovers fewer than `n` sensors

- **WHEN** the I2C scan finds `k < n` sensors and
  `reserveSensorSlots(n)` was called before the scan
- **THEN** the sensor vector holds exactly `k` sensors and reports
  `capacity() >= n`; no reallocation occurred during the add loop

#### Scenario: Reserve count matches scan branches

- **WHEN** the firmware is built
- **THEN** the value passed to `reserveSensorSlots` equals the
  number of `if (i2cBus.probe(addr))` branches in the I2C scan
  loop in `main.cpp` (asserted by a native unit test)

### Requirement: Heap fragmentation is observable

The firmware SHALL publish the value of
`heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` (in bytes) in
the `/api/status` JSON response under the field
`largest_free_block`, alongside the existing `free_heap` field.

#### Scenario: Status payload includes the field

- **WHEN** a client GETs `/api/status`
- **THEN** the response JSON contains a `largest_free_block` key
  whose value is a non-negative integer in bytes

#### Scenario: Field reflects current heap shape

- **WHEN** the firmware has been running for at least one hour and
  has handled at least one hundred HTTP requests
- **THEN** `largest_free_block` is greater than zero and is at most
  equal to `free_heap` (it is the size of the largest *contiguous*
  free block, never larger than the total free heap)

### Requirement: Heap regressions are detectable from the API

The `/api/status` endpoint SHALL be sufficient to detect a
fragmentation regression — meaning, the data exposed via that
endpoint (in particular `largest_free_block`) SHALL make it
possible to observe the heap's shape without serial-log access.

#### Scenario: Operator checks heap health remotely

- **WHEN** an operator opens the device's web UI from a browser
  while the device is on the home network
- **THEN** the displayed status includes the current value of
  `largest_free_block` so the operator can see whether the
  largest contiguous free block has shrunk unexpectedly

### Requirement: HTTP route handlers place the JsonDocument on the stack

The firmware SHALL serialize every HTTP route handler's response under `src/routes/*.cpp` and `WebServerManager::handleWiFiConfig` using a `JsonDocument` placed on the handler's stack frame. The `JsonDocument` object itself MUST be stack-allocated; variable-length data the document holds is allocated via the ArduinoJson default allocator (`heap_caps_malloc` / `free` on ESP32, `malloc` / `free` on the host) and is freed when the document is destroyed at handler return. The firmware MUST NOT call `std::make_unique<JsonDocument>()`, `new JsonDocument`, `DynamicJsonDocument`, or any other heap allocator for the document object itself in a request handler.

#### Scenario: Status handler serves `/api/status`

- **WHEN** a client GETs `/api/status`
- **THEN** the handler builds the response with a stack-allocated `JsonDocument`; the `JsonDocument` object is on the handler's stack frame and any data it holds is allocated via the default allocator and freed at handler return

#### Scenario: WiFi config handler accepts a POST

- **WHEN** a client POSTs to `/api/wifi` with a valid CSRF header
- **THEN** the handler parses the body and serializes the response using stack-allocated `JsonDocument` instances; the `JsonDocument` objects are on the handler's stack frame and any data they hold is allocated via the default allocator and freed at handler return

