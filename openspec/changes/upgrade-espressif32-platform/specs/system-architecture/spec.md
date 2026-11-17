# Spec Delta

## MODIFIED Requirements

### Requirement: Memory budget

The firmware SHALL keep the flash image within the 1856 KB app partition and SHALL maintain at least 64 KB of free heap to allow OTA updates. The system SHALL restart cleanly if free heap drops below 16 KB during runtime outside of an OTA flow. The firmware SHALL additionally publish `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` (in bytes) via the `/api/status` endpoint under the field `largest_free_block` so that heap *shape* regressions — fragmentation with healthy total free heap — are observable without serial-log access.

OTA memory accounting SHALL use `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` rather than `ESP.getFreeHeap()` / `esp_get_free_heap_size()`. The two calls report different things on this board: the latter sums PSRAM and internal SRAM (`CONFIG_SPIRAM_USE_MALLOC=y`, `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=0`), and would pass an OTA memory gate unconditionally when PSRAM is healthy. The allocations that fail under pressure (task stacks, lwIP / WiFi structures, DMA buffers, the mbedTLS working set) are internal-only, which is what the gate is actually protecting against.

#### Scenario: Build size check

- **WHEN** the build completes
- **THEN** the image size SHALL NOT exceed 1856 KB (the `app0`/`app1` partition size in `partitions.csv`) and the build SHALL fail otherwise

#### Scenario: Low heap during runtime

- **WHEN** `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` returns less than 16384 bytes and no OTA update is in progress
- **THEN** the firmware SHALL log a CRITICAL line and call `ESP.restart()` within 1 second

#### Scenario: Heap shape is exposed via the status API

- **WHEN** a client GETs `/api/status` while the firmware is running normally (free heap > 64 KB)
- **THEN** the response JSON contains a `largest_free_block` key whose value is the size in bytes of the largest contiguous free block in the 8-bit-capable heap, so an operator can detect fragmentation regressions from the web UI

#### Scenario: Heap thresholds are revalidated after a toolchain generation change

- **WHEN** the Arduino core or ESP-IDF generation changes — for example the move from core 2.0.17 / IDF 4.4.7 to core 3.3.12 / IDF 5.5.5
- **THEN** the idle internal free heap SHALL be measured on the target board and the 16 KB restart threshold and the 64 KB OTA headroom SHALL be confirmed to hold against that measurement
- **AND** the measured values SHALL be recorded in the change that performs the bump, so a later reader can tell a verified threshold from an inherited one
