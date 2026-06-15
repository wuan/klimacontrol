## Context

The `reduce-heap-fragmentation` change just landed `largest_free_block`
in `/api/status` and put a "Heap LFB" tile in the main dashboard's
control bar (`data/control.html`). The field is sourced from
`heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` and is documented
in the new `memory-management` spec.

Two follow-up decisions remain:

1. **Where the field is rendered.** The main dashboard is for
   environmental readings; the Device Info page (`data/about.html`,
   served from `GET /about`) is the existing home for diagnostic
   fields like `free_heap`, `min_free_heap`, and `flash_size`.
   Moving the display keeps the dashboard uncluttered and groups
   the heap diagnostics together.

2. **Whether the value is exposed as a generic measurement.** The
   codebase already has a `DeviceSensor` that reports device-internal
   metrics (RSSI, chip temp, free heap, uptime). The MQTT publish
   loop iterates over `SensorController::getMeasurements()` and
   publishes each entry under `{prefix}/{measurement_type_label}`
   (`src/Network.cpp:767`). Adding the new measurement type to
   `DeviceSensor` is a one-line addition to `providesMeasurements()`
   and a four-line `read()` extension — the publish loop picks it
   up with no other code changes.

## Goals / Non-Goals

**Goals**

- Render `largest_free_block` on the Device Info page in the
  Memory section, sourced from `GET /api/about`.
- Add `LargestFreeBlock` as a `Sensor::MeasurementType`, produced
  by `DeviceSensor::read()` on the same cadence as `FreeHeap`,
  using the same kB unit for consistency.
- Cover both changes with native tests that compile under
  the `native` env (no `ARDUINO`).
- Keep the existing `/api/status` field — it is the contract for
  the `memory-management` spec and the source of the
  `largest_free_block` data inside `DeviceSensor::read()`.

**Non-Goals**

- Switching the device-internal measurement units from kB to
  bytes. The new measurement matches the existing `FreeHeap`
  convention; finer-grained fragmentation tracking can be a
  follow-up.
- Changing the MQTT publish cadence or payload shape. The new
  measurement rides the existing loop.
- Removing the `largest_free_block` field from `/api/status`.
  The `memory-management` spec mandates it; downstream tooling
  (e.g. an external metrics scraper) may already depend on it.
- Adding a new control to disable publishing device-internal
  measurements. All `DeviceSensor` measurements are published by
  the existing loop; adding a per-type opt-out is out of scope.

## Decisions

### D1. New measurement type is `Sensor::MeasurementType::LargestFreeBlock`, value in kB

**Decision.** Add the enum value, label (`"largest_free_block"`),
and unit (`"kB"`), and store the value as
`static_cast<float>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024.0f)`.

**Rationale.** Matches the existing `FreeHeap` pattern exactly
(`src/sensor/DeviceSensor.cpp:38`):
`{MeasurementType::FreeHeap, static_cast<float>(ESP.getFreeHeap()/1024.0), getType(), false}`.
A user looking at the JSON table or the MQTT payload sees the new
field formatted the same way as the existing one. The label is
the snake-case string the MQTT publish loop uses for the topic
suffix (`src/Network.cpp:779`), so no extra wiring is needed.

**Alternatives considered.**

- *Bytes instead of kB (a `int32_t` value with unit `"B"`).*
  More precise for fragmentation tracking, but inconsistent with
  `FreeHeap` and the existing UI display. The kB unit matches
  the ~320 KB heap granularity well — a 100-byte resolution
  is below the noise floor of normal allocations.
- *Re-use `FreeHeap` and add a sibling `FreeHeapShape` field.*
  Two measurement types covering the same metric would duplicate
  work and confuse the MQTT topic tree.

### D2. `/api/about` is the canonical home for the field in the web UI

**Decision.** Add `largest_free_block` to the `/api/about` JSON
document (alongside the existing `free_heap`, `min_free_heap`,
and `heap_size` fields) and render it in the Memory section of
`data/about.html`. Remove the "Heap LFB" tile and its update JS
from `data/control.html`.

**Rationale.** The Device Info page is the existing home for
diagnostic fields. The main dashboard is for at-a-glance
environmental readings; a fragmentation metric adds visual
clutter without informing the typical user.

**Alternatives considered.**

- *Keep the field in both `/api/status` and `/api/about`.*
  This is what the change actually does. The redundant
  `/api/status` field is mandated by the `memory-management`
  spec; the `/api/about` field is what the UI binds to.
- *Move the field only on the UI, not in the API.* Surprising
  for any other consumer of `/api/status` that already handles
  the key. Both should be present so the JSON shape is stable.
- *Drop the field from `/api/status` to avoid the duplicate.*
  Breaks the `memory-management` spec, which is now in
  `openspec/specs/`.

### D3. The API field and the MQTT measurement are independent

**Decision.** The `/api/status` and `/api/about` HTTP handlers
each call `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`
directly and emit the value in bytes (matching the
`memory-management` spec, which documents the field in bytes).
The new `Sensor::MeasurementType::LargestFreeBlock` produced
by `DeviceSensor::read()` is a *separate* call, scheduled by
the sensor-monitor task at the same cadence as the other
device metrics. The MQTT publish loop picks up the measurement
in kB (matching the existing `FreeHeap` unit convention) and
publishes it under `{prefix}/largest_free_block`.

**Rationale.** The HTTP API returns bytes so the value is
directly comparable with the `largest_free_block` field
already documented in the `memory-management` spec. The MQTT
topic reports kB to match the existing device-internal
measurement convention. Keeping the two paths independent
avoids the unit mismatch and missing-data edge cases that
would arise if `/api/about` read the measurement back from
`SensorController` (kB on the sensor side, bytes on the API
side; the API could be polled before the first sensor cycle
ran, leaving the field absent).

**Alternatives considered.**

- *Have `/api/about` read the `LargestFreeBlock` measurement
  from `SensorController`.* Avoids the second
  `heap_caps_*` call but introduces a unit mismatch (the
  measurement is stored in kB; the spec documents the API
  field in bytes) and a missing-data window before the
  first sensor cycle.
- *Reuse the `FreeHeap` measurement and add a `heap_shape`
  field on it.* Two measurements covering the same metric
  would duplicate work and confuse the MQTT topic tree.

### D4. Web-UI tile is the only thing removed from the main dashboard

**Decision.** Strip the `<div class="control-item" id="largestFreeBlock">`
tile and the matching JS update block from `data/control.html`.
No other layout, polling, or data binding changes.

**Rationale.** Minimal diff, preserves the existing 10-second
polling cadence and the `setInterval` / `fetch` plumbing. The
tile removal brings the control bar back to three items
(target, control state), restoring the original layout that
pre-dates the heap-stability refactor.

**Alternatives considered.**

- *Move the tile to a collapsible "Diagnostics" section on the
  main page.* Increases JS complexity and shifts focus from the
  environmental reading. The Device Info page is the
  conventional home for diagnostics.

## Risks / Trade-offs

- **Topic stability** → the MQTT topic suffix is the
  `measurementTypeLabel()` string, so a typo on
  `"largest_free_block"` would surface as
  `home/sensors/largest_free_block` on the broker. The static
  source test in the new native test pins the label string.
- **Per-cycle heap-cap call from `DeviceSensor`** → the new
  measurement is produced every sensor cycle (1 Hz by default).
  `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` is
  implemented as a single walk over the free-block list and is
  safe to call at 1 Hz; it is already called at 10-second
  cadence from the `/api/status` handler. The doubled call rate
  is within budget on the ESP32-S2.
- **kB resolution hides byte-level fragmentation** → a 100-byte
  granularity is below the noise floor of normal allocations on
  a 320 KB heap, but a 24-hour soak test may show a slow trend
  that bytes would expose. Mitigation: `ESP.getFreeHeap()` is
  already published in kB; the new field matches. A byte-level
  follow-up can extend the device-internal unit convention if
  it becomes a useful signal.
- **Dashboard tile removal is a small user-visible change** →
  the main dashboard goes from four control-bar items back to
  three. Any operator who had internalised the tile loses a
  glanceable fragmentation metric. Mitigation: the Device Info
  page is one click away via the nav footer link, and the value
  is also published to MQTT for operators with a dashboard.
- **`/api/about` is one of the largest response documents** →
  the existing 1024-byte `StaticJsonDocument` budget is already
  documented as "the about payload is fixed-size but large"
  (see `StatusRoutes.cpp:79`). Adding a 4-byte integer field
  is well under that budget.

## Migration Plan

The change is fully backward-compatible at the API and MQTT
level:

- The MQTT topic `{prefix}/largest_free_block` is *new*. Any
  consumer that subscribed to the prefix will receive the new
  topic on the next publish cycle. Existing topics are
  unchanged.
- The `/api/about` payload gains a field. Existing fields keep
  their types and order; consumers that ignore unknown keys
  (the standard JSON practice) are unaffected.
- The `/api/status` payload is unchanged.
- The main dashboard HTML is missing a DOM element that only
  the matching JavaScript was using. Removing both in the same
  commit keeps the page self-consistent; no "left over" state.

Deployment is a normal firmware flash. Roll back the same way
(by re-flashing the previous release); no NVS schema change.

## Open Questions

(none — the user has made the placement decision; the unit
follows the existing `FreeHeap` convention.)
