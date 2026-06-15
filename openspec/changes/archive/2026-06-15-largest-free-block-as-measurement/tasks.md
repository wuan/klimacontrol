## 1. Add `LargestFreeBlock` to the measurement type system

- [x] 1.1 In `src/sensor/Sensor.h`, add `LargestFreeBlock` to
  the `MeasurementType` enum, add the corresponding case to
  `measurementTypeLabel` returning `"largest_free_block"`, and
  add the corresponding case to `measurementTypeUnit` returning
  `"kB"`.
- [x] 1.2 In `src/sensor/DeviceSensor.h`, extend
  `providesMeasurements()` so its returned `TypeSpan` includes
  `MeasurementType::LargestFreeBlock` and the count is bumped
  from 5 to 6.
- [x] 1.3 In `src/sensor/DeviceSensor.cpp`, push a
  `LargestFreeBlock` measurement in `read()` (under
  `#ifdef ARDUINO`, alongside `FreeHeap` and `Uptime`) with the
  value `static_cast<float>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024.0f)`.
  Add an `#include <esp_heap_caps.h>` guarded by `#ifdef ARDUINO`.

## 2. Surface the field on `/api/about`

- [x] 2.1 In `src/routes/StatusRoutes.cpp`, inside the
  `/api/about` handler, add
  `doc["largest_free_block"] = static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));`
  guarded by `#ifdef ARDUINO` (so native tests still compile).
  Place it next to the existing `free_heap` /
  `min_free_heap` block.

## 3. Move the dashboard tile to the Device Info page

- [x] 3.1 In `data/control.html`, remove the
  `<div class="control-item" title="Largest contiguous free heap block — fragmentation indicator">`
  block (and its child elements) from the `.control-bar` div.
- [x] 3.2 In `data/control.html`, remove the
  `// Update largest free block` block from the `updateStatus()`
  function (the comment plus the four lines that touch
  `currentStatus.largest_free_block`).
- [x] 3.3 In `data/about.html`, add a "Largest Free Block" row
  to the `memoryInfo` section, after the existing
  "Min Free Heap" row, using `formatBytes(data.largest_free_block)`.

## 4. Refresh the compressed web assets

- [x] 4.1 Run `python scripts/compress_web.py` from the repo
  root to regenerate `src/generated/*_gz.h` for the modified
  `control.html` and `about.html`. Confirm the diff is limited
  to those two compressed blobs.

## 5. Native tests

- [x] 5.1 Create `test/test_device_sensor_measurements/`
  with a `test_largest_free_block.cpp` native test that
  asserts:
  - `measurementTypeLabel(MeasurementType::LargestFreeBlock)`
    returns `"largest_free_block"`.
  - `measurementTypeUnit(MeasurementType::LargestFreeBlock)`
    returns `"kB"`.
  - `DeviceSensor::providesMeasurements()` includes
    `LargestFreeBlock` in its list.
  - `DeviceSensor::providesMeasurements().count` is `6`.
  - `DeviceSensor::read()` on the native build returns an
    empty `SensorReading.measurements` (consistent with the
    existing pattern — `#ifdef ARDUINO` guard skips the
    push).
- [x] 5.2 In `test/test_memory_status_payload/test_status_payload.cpp`,
  extend `test_status_routes_emits_largest_free_block_field`
  so it also asserts the substring
  `doc[\"largest_free_block\"]` appears at least twice in
  `src/routes/StatusRoutes.cpp` (one for the `/api/status`
  handler, one for the `/api/about` handler). Bump the
  comment block above the test to mention the dual
  emission.
- [x] 5.3 Add `+<test/test_device_sensor_measurements/>` to
  the `build_src_filter` in `[env:native]` of
  `platformio.ini`.

## 6. Spec validation and build

- [x] 6.1 Run `openspec validate --change
  largest-free-block-as-measurement --strict` from the repo
  root. Fix any reported errors.
- [x] 6.2 Build for ESP32:
  `pio run -e adafruit_qtpy_esp32s2`. Fix any compilation
  errors. The pre-build hook re-runs `scripts/compress_web.py`
  so the regenerated headers are picked up automatically.
- [x] 6.3 Run the full native test suite:
  `pio test -e native`. The two new `test_memory_*` tests,
  the new `test_device_sensor_measurements` test, and the
  existing tests SHALL all pass.

## 7. Manual device verification (deferred)

- [ ] 7.1 *(Deferred — requires real device.)* Flash the
  firmware to a test device, open `/about`, confirm the
  "Largest Free Block" row is present in the Memory section
  and updates alongside the existing `Free Heap` / `Min Free
  Heap` rows.
- [ ] 7.2 *(Deferred — requires real device.)* Open the main
  dashboard, confirm the "Heap LFB" tile is gone and the
  control bar shows only Target / Control.
- [ ] 7.3 *(Deferred — requires real device.)* Subscribe to
  the configured MQTT prefix and confirm a `largest_free_block`
  topic appears with a JSON payload matching the spec
  scenario (kB value, `sensor: "ESP32"`, `calculated: false`).
