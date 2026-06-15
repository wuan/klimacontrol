# http-api Specification (delta)

## Purpose

The http-api spec already documents the status endpoints. The
`/api/status` payload already includes `largest_free_block` (see
`memory-management` → "Heap fragmentation is observable"). This delta
adds the same field to `/api/about`, which is the natural home for
the value in the web UI's Device Info page.

## MODIFIED Requirements

### Requirement: Status endpoints

The firmware SHALL expose the following GET endpoints for device status: `GET /api/status` (overview including device_id, device_name, firmware_version, sensor_connected, sensor_valid, temperature, relative_humidity, dew_point, sensor_timestamp, target_temperature, control_enabled, wifi_connected, ip_address, wifi_ssid, `largest_free_block`), `GET /api/about` (extended device info including `free_heap`, `min_free_heap`, `heap_size`, `largest_free_block`, chip info, flash info, network info, sensor stats, cycle-delay stats), `GET /api/measurements` (per-measurement table).

#### Scenario: Operational device status

- **WHEN** `GET /api/status` is requested on a device with WiFi connected and at least one online sensor
- **THEN** the JSON response SHALL include numeric `temperature`, `relative_humidity`, and `dew_point`, `wifi_connected: true`, and the current `ip_address`

#### Scenario: Measurement table

- **WHEN** `GET /api/measurements` is requested
- **THEN** the response body SHALL include a `measurements` array with one entry per available measurement type (with `type`, `value`, `unit`, `sensor`, `calculated` fields) and SHALL also include the top-level `temperature` and `relative_humidity` fields for backward compatibility

#### Scenario: Device info includes the heap fragmentation metric

- **WHEN** `GET /api/about` is requested
- **THEN** the JSON response SHALL include a `largest_free_block`
  field whose value (in bytes) is the size of the largest
  contiguous free block in the 8-bit-capable heap, alongside the
  existing `free_heap` and `min_free_heap` fields

## ADDED Requirements

### Requirement: Device info heap-shape field is guarded for native builds

`/api/about` SHALL emit `largest_free_block` on the device. The
underlying call to `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`
SHALL be guarded with `#ifdef ARDUINO` so the native test build
still compiles. In the native test build the field is omitted from
the response (the call site is unreachable), matching the existing
convention for `heap_caps_*` calls in the route handlers.

#### Scenario: Field present on device

- **WHEN** the firmware is running on the device target and a
  client GETs `/api/about`
- **THEN** the response JSON contains an integer-valued
  `largest_free_block` key whose value is greater than or equal to
  zero and less than or equal to `free_heap`
