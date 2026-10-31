# Proposal

## Why

Change `2026-10-31-reduce-heap-churn` moved the Home Assistant discovery
payload buffer in `MqttPublisher::publishDiscovery()` from the Network task's
stack to a file-static `char[2048]`. On the ESP32-S2 the heap starts exactly at
the end of BSS (`_heap_start` == `_bss_end`, confirmed in `firmware.map`), so
that buffer permanently removes 2048 B from
`heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` — the exact pool the change set
out to conserve.

The move was justified by a stack-spike fear that the measurements do not
support: the Network task runs an 8 KiB stack with a measured ~3.5 KiB peak
(≈4.6 KiB free), and `publishDiscovery()` runs only on the STA + MQTT connect
path — never on the AP/captive-portal path — so the 2048 B buffer is transient
and comfortably fits. Net effect of the change was therefore negative on
steady-state internal heap (−2 KB) with no observed stack benefit.

## What Changes

- `MqttPublisher::publishDiscovery()` formats the discovery payload into a
  transient stack buffer again, removing the 2048 B static allocation and
  reclaiming ~2 KB of internal heap. `MqttClient::MQTT_BUFFER_SIZE` stays 2048.
- `sendJson()` sizes the `AsyncResponseStream` to the exact `measureJson()`
  length: `cbuf` already adds its own full/empty sentinel internally, so the
  previous `len + 1` over-allocated by one byte per JSON response.

## Capabilities

### Modified Capabilities

- `memory-management` — the largest one-shot MQTT payload is transient, not
  permanent static storage; JSON responses are sized to the exact measured
  length.

### New Capabilities

- None.
