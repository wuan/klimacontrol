# http-api Specification

## Purpose
TBD - created by archiving change baseline-capabilities. Update Purpose after archive.
## Requirements
### Requirement: HTTP framework and response format

The firmware SHALL serve HTTP using `ESPAsyncWebServer`. All `/api/*` endpoints SHALL respond with `Content-Type: application/json` and JSON bodies produced via `ArduinoJson`.

#### Scenario: Content-Type header

- **WHEN** any `/api/*` endpoint returns a response
- **THEN** the `Content-Type` header SHALL be `application/json`

### Requirement: Sensor configuration endpoints

The firmware SHALL expose: `GET /api/sensors` (list with status), `GET /api/sensors/config` (current assignment string), `POST /api/sensors/config` (update assignments), `GET /api/sensors/registry` (known driver types).

#### Scenario: Updating sensor configuration

- **WHEN** `POST /api/sensors/config` is sent with a new assignment payload
- **THEN** the configuration SHALL be persisted to NVS and the device SHALL schedule a restart so the new sensors are picked up on next boot

### Requirement: Temperature control endpoints

The firmware SHALL expose: `POST /api/temperature/target` (set setpoint), `POST /api/control/enable`, `POST /api/control/disable`.

#### Scenario: Setting target temperature

- **WHEN** `POST /api/temperature/target` is sent with body `{"value": 23.5}`
- **THEN** `sensorController.setTargetTemperature(23.5)` SHALL be invoked and the new value SHALL be persisted to `DeviceConfig.target_temperature`

#### Scenario: Out-of-range setpoint

- **WHEN** the request body contains a value outside the validated range (`10.0` … `30.0` °C)
- **THEN** the endpoint SHALL respond with HTTP 4xx and the controller setpoint SHALL remain unchanged

### Requirement: Settings endpoints

The firmware SHALL expose: `POST /api/settings/wifi`, `POST /api/settings/device-name`, `POST /api/settings/elevation`, `POST /api/settings/reboot`, `POST /api/settings/factory-reset`, `POST /api/restart`.

#### Scenario: Updating WiFi credentials

- **WHEN** `POST /api/settings/wifi` is sent with valid SSID/password
- **THEN** the credentials SHALL be persisted, the connection-failure counter SHALL be reset, and a restart SHALL be scheduled

#### Scenario: Factory reset

- **WHEN** `POST /api/settings/factory-reset` is sent
- **THEN** all NVS data in the `"klima"` namespace SHALL be cleared and the device SHALL restart

### Requirement: OTA endpoints

The firmware SHALL expose: `POST /api/ota/check`, `GET /api/ota/check`, `POST /api/ota/update`, `GET /api/ota/update`, `GET /api/ota/status`, `POST /api/ota/confirm`.

`POST /api/ota/update` SHALL NOT accept a download URL: the device installs only the asset identified by its own check of the compiled-in owner/repo, so a client cannot point it at an arbitrary binary. For the same reason `GET /api/ota/check` SHALL NOT expose the download URL.

#### Scenario: Checking for updates

- **WHEN** `GET /api/ota/check` is polled after `POST /api/ota/check` and a strictly newer GitHub release exists
- **THEN** the response SHALL include `update_available: true`, `latest_version`, `release_name`, and `size_bytes`, and SHALL NOT include a download URL

#### Scenario: Polling a running update

- **WHEN** `GET /api/ota/update` is requested while a download is in progress
- **THEN** the response SHALL include `status: "downloading"` with `percent` and `bytes`

#### Scenario: Polling a failed update

- **WHEN** `GET /api/ota/update` is requested after a download failed
- **THEN** the response SHALL include `status: "error"` and an `error` message

### Requirement: MQTT endpoints

The firmware SHALL expose: `GET /api/mqtt`, `POST /api/mqtt`, `POST /api/mqtt/enable`, `POST /api/mqtt/disable`.

#### Scenario: Reading MQTT configuration

- **WHEN** `GET /api/mqtt` is requested
- **THEN** the response SHALL include `host`, `port`, `username`, `prefix`, `interval`, `enabled` (passwords SHALL NOT be echoed back), `buffer_size`, `buffer_degraded`, and `truncated_publishes` (the buffer fields are documented in the mqtt-integration spec, "MQTT TX buffer state is observable" requirement)

### Requirement: Page routes

The firmware SHALL serve a dashboard at `GET /` and a settings page at `GET /settings`. Both SHALL be served as embedded HTML, not from a filesystem.

#### Scenario: Dashboard load

- **WHEN** a browser requests `/` on a device in STA mode
- **THEN** the response SHALL be an HTML page that uses the JSON status endpoints to populate values

### Requirement: Request handler allocation discipline

The firmware SHALL build and serialize every HTTP request handler's JSON response using a `JsonDocument` placed on the handler's stack frame. The `JsonDocument` object itself MUST be stack-allocated; variable-length data the document holds is allocated via the ArduinoJson default allocator (`heap_caps_malloc` / `free` on ESP32, `malloc` / `free` on the host) and is freed when the document is destroyed at handler return. The firmware SHALL NOT call `std::make_unique<JsonDocument>()`, `new JsonDocument`, `DynamicJsonDocument`, or any other heap allocator for the document object itself in a request handler. Body parsing MUST use the same stack pattern.

#### Scenario: GET handler builds a status response

- **WHEN** a client GETs `/api/status`
- **THEN** the handler constructs a stack-allocated `JsonDocument`, populates the keys, and calls `serializeJson` into a `String`; the `JsonDocument` object is on the handler's stack frame and any data it holds is allocated via the default allocator and freed at handler return

#### Scenario: POST handler parses a JSON body

- **WHEN** a client POSTs to `/api/wifi` (or any other state-changing endpoint) with a valid CSRF header
- **THEN** the body handler uses a stack-allocated `JsonDocument` to deserialize the body and a separate stack-allocated `JsonDocument` to build the response; the `JsonDocument` objects are on the handler's stack frame and any data they hold is allocated via the default allocator and freed at handler return

### Requirement: `/api/status` schema includes `largest_free_block`

The `/api/status` JSON response SHALL include a `largest_free_block` field (in bytes) sourced from `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`, alongside the existing `free_heap` field.

#### Scenario: Status payload contains the field

- **WHEN** a client GETs `/api/status`
- **THEN** the parsed JSON contains an integer-valued `largest_free_block` key whose value is greater than or equal to zero and less than or equal to `free_heap`

#### Scenario: Field is omitted in unit tests that stub heap APIs

- **WHEN** the handler is invoked from a native test environment that does not link the ESP-IDF `heap_caps_*` symbols
- **THEN** the field is reported as zero (or the response is documented to omit the key in the test build) so the contract remains testable without an ESP32 target

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

### Requirement: Display endpoints

The firmware SHALL expose `GET /api/display` and `POST /api/display` for reading
and updating the e-paper display configuration, implemented in
`src/routes/DisplayRoutes.cpp` following the structure of the existing
`SyslogRoutes.cpp`.

`GET /api/display` SHALL return the user-facing fields only:

```json
{"enabled": false, "rotation": 0, "interval": 60}
```

`POST /api/display` SHALL require the CSRF header
`X-Requested-With: KlimaControl` via `verifyCsrfHeader()`, SHALL validate the
body through `Config::validateDisplayConfig()`, SHALL persist the result, and
SHALL then call `config.requestRestart(1000)` — matching the save-then-restart
convention of the other settings routes.

When the request disables a display that was previously enabled, the handler
SHALL blank the panel synchronously before saving and scheduling the restart.
This blocks the callback for the duration of a full refresh (~2.6 s), and up to
a further refresh if the Network task is mid-repaint.

#### Scenario: Reading the display configuration

- **WHEN** a client GETs `/api/display` on a device that has never configured the display
- **THEN** the response SHALL be `200` with `enabled: false`, `rotation: 0`, and `interval: 60`

#### Scenario: Enabling the display

- **WHEN** a client POSTs `/api/display` with `{"enabled": true, "rotation": 2, "interval": 120}` and the CSRF header
- **THEN** the configuration SHALL be persisted and a restart SHALL be scheduled

#### Scenario: Disabling the display blanks the panel inline

- **WHEN** a client POSTs `/api/display` with `{"enabled": false}` while the display was previously enabled
- **THEN** the panel SHALL be blanked before the response is sent, the persisted configuration SHALL have `enabled = false`, and a restart SHALL be scheduled

#### Scenario: Missing CSRF header

- **WHEN** a client POSTs `/api/display` without the `X-Requested-With: KlimaControl` header
- **THEN** the request SHALL be rejected and no configuration SHALL be written

#### Scenario: Out-of-range values are clamped, not rejected

- **WHEN** a client POSTs `/api/display` with `{"enabled": true, "rotation": 9, "interval": 1}`
- **THEN** the persisted configuration SHALL contain a `rotation` in 0..3 and an `interval` in 10..3600

#### Scenario: Enabling or reconfiguring does not drive the panel

- **WHEN** a `POST /api/display` request enables the display or changes only rotation or interval
- **THEN** the handler SHALL return without driving the panel; only the disable transition performs a blanking refresh

### Requirement: Timezone endpoints

The firmware SHALL expose `GET /api/settings/timezone` returning the configured
POSIX TZ string together with the current local time, and
`POST /api/settings/timezone` to update it.

```json
{"timezone": "CET-1CEST,M3.5.0,M10.5.0/3", "local_time": "14:32", "synced": true}
```

`local_time` SHALL be an empty string when NTP has not yet synced, and `synced`
SHALL report that state, so the UI can distinguish "no clock yet" from midnight.

`POST` SHALL require the CSRF header `X-Requested-With: KlimaControl`, SHALL
reject an implausible value with 400, SHALL persist the value, and SHALL apply it
immediately via `Support::applyTimezone()`.

Unlike the other mutating settings routes, this endpoint SHALL NOT schedule a
restart: `tzset()` fully applies the change, and no component holds derived
state that a restart would reconcile.

#### Scenario: Reading the timezone before NTP sync

- **WHEN** a client GETs `/api/settings/timezone` on a device that has not yet synced NTP
- **THEN** the response SHALL contain the configured `timezone`, an empty `local_time`, and `synced: false`

#### Scenario: Updating the timezone takes effect without a restart

- **WHEN** a client POSTs `{"timezone": "CET-1CEST,M3.5.0,M10.5.0/3"}` with the CSRF header
- **THEN** the value SHALL be persisted and applied immediately, no restart SHALL be scheduled, and a subsequent GET SHALL report a `local_time` in the new zone

#### Scenario: Rejecting an implausible value

- **WHEN** a client POSTs an empty or non-printable timezone string
- **THEN** the request SHALL be rejected with 400 and the stored value SHALL be unchanged

#### Scenario: Missing CSRF header

- **WHEN** a client POSTs `/api/settings/timezone` without the `X-Requested-With: KlimaControl` header
- **THEN** the request SHALL be rejected and no configuration SHALL be written

