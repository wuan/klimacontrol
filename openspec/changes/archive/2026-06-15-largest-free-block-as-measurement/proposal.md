## Why

The `reduce-heap-fragmentation` change just made `largest_free_block`
observable on the device — it added the field to `/api/status` and put a
"Heap LFB" tile in the main dashboard's control bar. Two follow-up
issues remain:

- The main dashboard is for at-a-glance environmental readings
  (temperature, humidity, dew point, target, control state). Heap
  fragmentation is a diagnostic, not an environmental metric; it
  belongs on the Device Info page alongside `free_heap`, `min_free_heap`,
  and `flash_size`.
- The MQTT publication path publishes every `Sensor::MeasurementType`
  produced by `SensorController` under `{prefix}/{measurement_type}`.
  `largest_free_block` is currently only available through the HTTP
  API, so a Home Assistant / Node-RED integration cannot alert on
  fragmentation regressions without scraping the JSON endpoint.

## What Changes

- **Add `LargestFreeBlock` to the device-measurement surface.** Add a
  new `Sensor::MeasurementType::LargestFreeBlock` (in kB, matching the
  existing `FreeHeap` unit), produce it from `DeviceSensor::read()` on
  the same cadence as the other device-internal metrics, and let the
  existing MQTT publish loop pick it up automatically. The value
  continues to come from
  `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`.
- **Move the dashboard display to the Device Info page.** Remove the
  "Heap LFB" tile from `data/control.html` (and the JS that updates
  it). Add a "Largest Free Block" row to the Memory section of
  `data/about.html`, sourced from a new `largest_free_block` field on
  `GET /api/about`. The field on `/api/status` stays (the
  `memory-management` spec already requires it; the `web-interface`
  spec is the one being tightened).
- **Native test for the new measurement type.** Assert that
  `measurementTypeLabel(LargestFreeBlock) == "largest_free_block"` and
  `measurementTypeUnit(LargestFreeBlock) == "kB"`, and that
  `DeviceSensor::providesMeasurements()` includes the new type. The
  existing static-source test in `test/test_memory_status_payload/`
  is extended to assert the `/api/about` handler also emits the field.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `sensor-management`: extend the "Measurement model" requirement so
  the documented set of measurement types includes
  `LargestFreeBlock` (in kB).
- `http-api`: add a "Status endpoints" scenario for the
  `largest_free_block` field on `GET /api/about`.
- `web-interface`: add a requirement that the Device Info page
  (served from `data/about.html`) shows the current
  `largest_free_block` value alongside the other memory fields, and
  clarify that the main dashboard does *not* render it.
- `mqtt-integration`: add a scenario to the "Publish topic format and
  payload" requirement confirming that a measurement produced by
  `DeviceSensor` (a device-internal metric) is published under
  `{prefix}/<measurement_type>` with the same payload shape as
  environmental measurements.

## Impact

**Code**

- `src/sensor/Sensor.h` — add `MeasurementType::LargestFreeBlock`,
  its label (`"largest_free_block"`), and its unit (`"kB"`).
- `src/sensor/DeviceSensor.h` — extend `providesMeasurements()` to
  list the new type.
- `src/sensor/DeviceSensor.cpp` — push a new
  `{MeasurementType::LargestFreeBlock, …}` measurement in `read()`
  using the same kB conversion as `FreeHeap`.
- `src/routes/StatusRoutes.cpp` — add `largest_free_block` to the
  `/api/about` handler (the `/api/status` field stays as-is).
- `data/control.html` — remove the "Heap LFB" tile and its update
  logic.
- `data/about.html` — add a "Largest Free Block" row in the Memory
  section.
- `scripts/compress_web.py` is re-run as part of the pre-build hook
  to refresh `src/generated/*_gz.h`.

**Specs**

- Deltas under `openspec/changes/largest-free-block-as-measurement/specs/`
  for `sensor-management`, `http-api`, `web-interface`, and
  `mqtt-integration`.

**Tests**

- New native test
  `test/test_device_sensor_measurements/test_largest_free_block.cpp`
  asserting the enum, label, unit, and `providesMeasurements()` list.
- The existing static-source test
  `test/test_memory_status_payload/test_status_payload.cpp` is
  extended to also assert the `/api/about` handler emits the field.

**Risk / Non-goals**

- The unit choice (kB) matches the existing `FreeHeap` convention. A
  follow-up could switch device-internal measurements to bytes for
  finer-grained fragmentation tracking; not part of this change.
- No new MQTT topic, no QoS change, no schema migration. The new
  measurement rides the existing publish loop.
