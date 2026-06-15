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

The firmware SHALL expose: `GET /api/ota/check`, `POST /api/ota/update`, `GET /api/ota/status`, `POST /api/ota/confirm`.

#### Scenario: Checking for updates

- **WHEN** `GET /api/ota/check` is requested and a newer GitHub release exists
- **THEN** the response SHALL include `update_available: true` and the latest `version` and `download_url`

### Requirement: MQTT endpoints

The firmware SHALL expose: `GET /api/mqtt`, `POST /api/mqtt`, `POST /api/mqtt/enable`, `POST /api/mqtt/disable`.

#### Scenario: Reading MQTT configuration

- **WHEN** `GET /api/mqtt` is requested
- **THEN** the response SHALL include `host`, `port`, `username`, `prefix`, `interval`, and `enabled` (passwords SHALL NOT be echoed back)

### Requirement: Page routes

The firmware SHALL serve a dashboard at `GET /` and a settings page at `GET /settings`. Both SHALL be served as embedded HTML, not from a filesystem.

#### Scenario: Dashboard load

- **WHEN** a browser requests `/` on a device in STA mode
- **THEN** the response SHALL be an HTML page that uses the JSON status endpoints to populate values

### Requirement: Request handler allocation discipline

The firmware SHALL build and serialize every HTTP request handler's JSON response using a `StaticJsonDocument` placed on the stack frame. The firmware SHALL NOT call `std::make_unique<JsonDocument>()`, `new JsonDocument`, or any other heap allocator in a request handler that is freed before the handler returns. Body parsing MUST use the same stack pattern.

#### Scenario: GET handler builds a status response

- **WHEN** a client GETs `/api/status`
- **THEN** the handler constructs a stack-allocated `StaticJsonDocument<512>`, populates the keys, and calls `serializeJson` into a `String`; no heap allocation for the JSON document itself occurs during the handler's execution

#### Scenario: POST handler parses a JSON body

- **WHEN** a client POSTs to `/api/wifi` (or any other state-changing endpoint) with a valid CSRF header
- **THEN** the body handler uses a stack-allocated `StaticJsonDocument` to deserialize the body and a separate stack-allocated `StaticJsonDocument` to build the response; no per-request heap allocation for JSON document objects occurs

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

