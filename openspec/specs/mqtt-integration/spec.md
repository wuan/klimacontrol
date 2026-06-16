# mqtt-integration Specification

## Purpose
TBD - created by archiving change baseline-capabilities. Update Purpose after archive.
## Requirements
### Requirement: MQTT configuration model

The firmware SHALL persist MQTT configuration in NVS as `MqttConfig` with fields `host`, `port`, `username`, `password`, `prefix`, `interval`, and `enabled`. Default values SHALL be: `port = 1883`, `prefix = "sensors"`, `interval = 15` seconds, `enabled = false`.

#### Scenario: Default config on fresh device

- **WHEN** the firmware boots with no stored MQTT config
- **THEN** `loadMqttConfig()` SHALL return the defaults above and MQTT SHALL remain disabled

### Requirement: PubSubClient as the underlying library

The firmware SHALL use the `PubSubClient` library for MQTT. The client ID SHALL be set to the device's `device_id`. The firmware SHALL only publish at QoS 0 — `PubSubClient::publish()` does not support higher QoS levels, so no QoS parameter SHALL be exposed to callers.

#### Scenario: Client ID

- **WHEN** `MqttClient::connect()` is called
- **THEN** the client identifier passed to the broker SHALL equal the device's `device_id`

### Requirement: Server target handling

When the configured host string is an IP address, the firmware SHALL pass it to PubSubClient via the `setServer(IPAddress, port)` overload (which stores the IP by value). When the host is a DNS name, the firmware SHALL store the hostname string in a long-lived buffer (the `MqttConfig` member) and pass that pointer to `setServer(const char*, port)` — passing a function parameter would create a dangling pointer because `setServer` does not copy the string.

#### Scenario: Numeric host

- **WHEN** the configured host parses as a valid IPv4 address
- **THEN** PubSubClient SHALL receive the address via the IP overload, avoiding a DNS lookup on every connect attempt

#### Scenario: Hostname stored long-lived

- **WHEN** the configured host is a DNS name
- **THEN** the pointer passed to `setServer(const char*, …)` SHALL refer to the `MqttConfig` member buffer, not to a local parameter

### Requirement: Connection management

The firmware SHALL attempt to (re)connect to the broker when enabled and disconnected. Each connect attempt SHALL first call `wifiClient.stop()` to close any stale underlying socket, because PubSubClient does not reliably clean up failed sockets between attempts. Reconnect retries SHALL run in the network task's 1-second loop, not in the LED's 50 ms path — `connect()` can block up to ~2 seconds and SHALL NOT be allowed to stall responsive UI updates.

#### Scenario: Stale socket cleanup

- **WHEN** a previous `mqttClient.connect()` attempt failed
- **THEN** the next attempt SHALL call `wifiClient.stop()` before invoking `connect()`

#### Scenario: Reconnect cadence

- **WHEN** MQTT is enabled and the client is disconnected
- **THEN** reconnect attempts SHALL be made from the 1-second loop, with at most one connect attempt per loop iteration

### Requirement: Publish topic format and payload

Sensor measurements SHALL be published to topics of the form `{prefix}/{measurement_type}`. The payload SHALL be a JSON object containing `time` (epoch seconds, or 0 if NTP has not synced), `value` (numeric), `unit` (string), `sensor` (string), and `calculated` (boolean). Publishing SHALL only begin once the device has been up for at least 60 seconds, to avoid burst publishes during early boot.

#### Scenario: Publish path

- **WHEN** the configured prefix is `home/sensors` and a temperature measurement is available
- **THEN** the measurement SHALL be published to `home/sensors/temperature` with a JSON payload that includes the `time`, `value`, `unit`, `sensor`, and `calculated` fields

#### Scenario: 60-second startup hold

- **WHEN** the device has been up for less than 60 seconds since boot
- **THEN** no MQTT publishes SHALL be issued, even if the publish interval has elapsed

### Requirement: Last Will and Testament

The firmware SHALL register a Last Will and Testament (LWT) when connecting. The LWT topic SHALL be `{prefix}/status` and the LWT message SHALL be `offline`. On clean disconnect the firmware SHALL publish `online` to the same topic before disconnecting.

#### Scenario: Broker publishes LWT on crash

- **WHEN** the firmware loses its broker connection without sending an `MQTT DISCONNECT` (e.g., a crash or power loss)
- **THEN** the broker SHALL publish `offline` to `{prefix}/status` to notify subscribers

### Requirement: Device-internal measurements are published

The MQTT publish path SHALL publish every measurement produced by
`DeviceSensor` (the synthetic sensor that reports RSSI, chip
temperature, free heap, uptime, and largest free block) on the
same schedule and with the same payload shape as measurements
produced by physical I2C sensors. The topic for each
`DeviceSensor` measurement SHALL be
`{prefix}/<measurement_type_label>` and the payload SHALL be the
same JSON object documented in the
"Publish topic format and payload" requirement.

#### Scenario: Largest free block is published to MQTT

- **WHEN** MQTT is enabled and connected, the device has been up
  for at least 60 seconds, and `DeviceSensor::read()` has produced
  a `LargestFreeBlock` measurement in the most recent cycle
- **THEN** the network task publishes the measurement to
  `{prefix}/largest_free_block` with a JSON payload of the form
  `{"time":<epoch>,"value":<kb>,"unit":"kB","sensor":"ESP32","calculated":false}`

#### Scenario: Topic label is stable

- **WHEN** the firmware is built
- **THEN** `Sensor::measurementTypeLabel(MeasurementType::LargestFreeBlock)`
  equals the string `"largest_free_block"`, so the MQTT topic
  suffix does not change between firmware versions

### Requirement: MQTT TX buffer state is observable

The firmware SHALL observe the actual MQTT TX buffer size PubSubClient ends up with (via `getBufferSize()` after `setBufferSize()`), expose the following via `MqttClient` and `GET /api/mqtt`:
- `buffer_size` (bytes) — the size PubSubClient is operating with
- `buffer_degraded` (boolean) — `true` if and only if `buffer_size` is below the requested `MQTT_BUFFER_SIZE` (1024 bytes)
- `truncated_publishes` (counter, monotonically increasing) — the number of `MqttClient::publish()` calls that returned `false` while `buffer_degraded` was `true`

The firmware MUST log an `ESP_LOGE` line identifying the publish as a likely buffer-truncation whenever `MqttClient::publish()` returns `false` while `buffer_degraded` is `true`. The firmware MUST retry `setBufferSize(MQTT_BUFFER_SIZE)` immediately after a successful MQTT (re)connect; if the retry succeeds, the firmware MUST clear `bufferDegraded` and emit an `ESP_LOGI` recovery line.

#### Scenario: setBufferSize succeeds on boot

- **WHEN** `MqttClient::begin()` is called and the heap can satisfy a 1024-byte allocation
- **THEN** `buffer_size` SHALL equal 1024, `buffer_degraded` SHALL be `false`, and the buffer-state fields SHALL be observable from `GET /api/mqtt`

#### Scenario: setBufferSize fails on a fragmented heap

- **WHEN** `MqttClient::begin()` is called and `setBufferSize(1024)` returns `false` (heap too fragmented)
- **THEN** `buffer_size` SHALL reflect the actual size PubSubClient fell back to (e.g. 256), `buffer_degraded` SHALL be `true`, and the warning log emitted at boot SHALL be visible in the serial output

#### Scenario: Publish returns false while buffer is degraded

- **WHEN** `MqttClient::publish(topic, payload)` is called while `buffer_degraded` is `true` and the underlying `PubSubClient::publish()` returns `false`
- **THEN** `truncated_publishes` SHALL increment by 1, the per-cycle `failed` counter SHALL also increment by 1, and a single `ESP_LOGE` line identifying the publish as a likely truncation SHALL be emitted for the cycle

#### Scenario: Publish returns false while buffer is healthy

- **WHEN** `MqttClient::publish(topic, payload)` is called while `buffer_degraded` is `false` and the underlying `PubSubClient::publish()` returns `false`
- **THEN** `truncated_publishes` SHALL NOT increment, the per-cycle `failed` counter SHALL increment by 1, and NO `ESP_LOGE` truncation line SHALL be emitted

#### Scenario: Buffer recovers on reconnect

- **WHEN** the broker disconnects and a subsequent MQTT (re)connect succeeds while the heap can now satisfy a 1024-byte allocation
- **THEN** the post-connect `setBufferSize(1024)` retry SHALL succeed, `buffer_size` SHALL become 1024, `buffer_degraded` SHALL become `false`, and an `ESP_LOGI` recovery line SHALL be emitted

#### Scenario: GET /api/mqtt includes the new fields

- **WHEN** a client GETs `/api/mqtt`
- **THEN** the response SHALL include `buffer_size` (integer), `buffer_degraded` (boolean), and `truncated_publishes` (integer ≥ 0) alongside the existing `stats` block

