# Tasks

## 1. OpenSpec change artifacts

- [x] 1.1 Create this change (proposal, design, delta for `memory-management`, tasks). Validate with `openspec validate --all --strict` from the repo root.

## 2. Reclaim the static discovery buffer (item 1)

- [x] 2.1 Remove the file-static `discoveryPayload[MQTT_BUFFER_SIZE]` from `src/network/MqttPublisher.cpp` and format the payload into a stack buffer inside `publishDiscovery()`.
- [x] 2.2 Confirm `MqttClient::MQTT_BUFFER_SIZE` stays 2048 and the worst-case MQTT payload test (`test/test_mqtt_payload`) is unchanged.

## 3. Exact response-stream sizing (item 2)

- [x] 3.1 In `sendJson()` (`src/routes/RouteHelpers.h`) pass `len` (not `len + 1`) to `AsyncResponseStream` and correct the comment.

## 4. Verification and archive

- [x] 4.1 `pio test -e native` passes (722/722).
- [x] 4.2 `pio run -e adafruit_qtpy_esp32s2` succeeds; RAM 84068 -> 82020 B (2048 B reclaimed, `_heap_start` 0x3ffd87d0 -> 0x3ffd7fd0).
- [x] 4.3 `scripts/validate-openspec.sh` (openspec 1.14.0) passes.
- [x] 4.4 Archive the change and sync the delta into `openspec/specs/memory-management/spec.md`.
