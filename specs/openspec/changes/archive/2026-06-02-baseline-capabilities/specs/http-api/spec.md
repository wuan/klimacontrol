## ADDED Requirements

### Requirement: HTTP framework and response format

The firmware SHALL serve HTTP using `ESPAsyncWebServer`. All `/api/*` endpoints SHALL respond with `Content-Type: application/json` and JSON bodies produced via `ArduinoJson`.

#### Scenario: Content-Type header

- **WHEN** any `/api/*` endpoint returns a response
- **THEN** the `Content-Type` header SHALL be `application/json`

### Requirement: Status endpoints

The firmware SHALL expose the following GET endpoints for device status: `GET /api/status` (overview including device_id, device_name, firmware_version, sensor_connected, sensor_valid, temperature, relative_humidity, dew_point, sensor_timestamp, target_temperature, control_enabled, wifi_connected, ip_address, wifi_ssid), `GET /api/about` (extended device info), `GET /api/measurements` (per-measurement table).

#### Scenario: Operational device status

- **WHEN** `GET /api/status` is requested on a device with WiFi connected and at least one online sensor
- **THEN** the JSON response SHALL include numeric `temperature`, `relative_humidity`, and `dew_point`, `wifi_connected: true`, and the current `ip_address`

#### Scenario: Measurement table

- **WHEN** `GET /api/measurements` is requested
- **THEN** the response body SHALL include a `measurements` array with one entry per available measurement type (with `type`, `value`, `unit`, `sensor`, `calculated` fields) and SHALL also include the top-level `temperature` and `relative_humidity` fields for backward compatibility

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
