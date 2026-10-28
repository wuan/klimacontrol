# mqtt-integration Specification

## Purpose
The mqtt-integration capability defines how the device reports its
measurements to a broker. It specifies the configuration model, the
PubSubClient-based connection management, the publish topic and
payload format, the last-will testament, and the observability of the
MQTT TX buffer state.
## Requirements
### Requirement: MQTT configuration model

The firmware SHALL persist MQTT configuration in NVS as `MqttConfig` with fields `host`, `port`, `username`, `password`, `prefix`, `interval`, `enabled`, and `ha_discovery`. Default values SHALL be: `port = 1883`, `prefix = "sensors"`, `interval = 15` seconds, `enabled = false`, `ha_discovery = false`.

#### Scenario: Default config on fresh device

- **WHEN** the firmware boots with no stored MQTT config
- **THEN** `loadMqttConfig()` SHALL return the defaults above and MQTT SHALL remain disabled

#### Scenario: Discovery default off on fresh device

- **WHEN** the firmware boots with no stored MQTT config
- **THEN** `ha_discovery` SHALL be `false` and no Home Assistant discovery SHALL be published

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

The firmware SHALL register a Last Will and Testament (LWT) when connecting. The LWT topic SHALL be `{prefix}/status`, the LWT message SHALL be `offline`, and the will SHALL be registered with the retain flag set. On a successful connect the firmware SHALL publish a retained `online` to the same topic, and on a clean disconnect it SHALL publish a retained `offline` before disconnecting.

#### Scenario: Broker publishes LWT on crash

- **WHEN** the firmware loses its broker connection without sending an `MQTT DISCONNECT` (e.g., a crash or power loss)
- **THEN** the broker SHALL publish a retained `offline` to `{prefix}/status` to notify subscribers

#### Scenario: Online on connect

- **WHEN** the MQTT client connects successfully
- **THEN** the firmware SHALL publish a retained `online` to `{prefix}/status`

#### Scenario: Offline on clean disconnect

- **WHEN** the firmware disconnects from the broker cleanly (for example, because the configuration changed)
- **THEN** it SHALL publish a retained `offline` to `{prefix}/status` before disconnecting

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
- `buffer_degraded` (boolean) — `true` if and only if `buffer_size` is below the requested `MQTT_BUFFER_SIZE` (2048 bytes)
- `truncated_publishes` (counter, monotonically increasing) — the number of `MqttClient::publish()` calls that returned `false` while `buffer_degraded` was `true`

The firmware MUST log an `ESP_LOGE` line identifying the publish as a likely buffer-truncation whenever `MqttClient::publish()` returns `false` while `buffer_degraded` is `true`. The firmware MUST retry `setBufferSize(MQTT_BUFFER_SIZE)` immediately after a successful MQTT (re)connect; if the retry succeeds, the firmware MUST clear `bufferDegraded` and emit an `ESP_LOGI` recovery line.

#### Scenario: setBufferSize succeeds on boot

- **WHEN** `MqttClient::begin()` is called and the heap can satisfy a 2048-byte allocation
- **THEN** `buffer_size` SHALL equal 2048, `buffer_degraded` SHALL be `false`, and the buffer-state fields SHALL be observable from `GET /api/mqtt`

#### Scenario: setBufferSize fails on a fragmented heap

- **WHEN** `MqttClient::begin()` is called and `setBufferSize(2048)` returns `false` (heap too fragmented)
- **THEN** `buffer_size` SHALL reflect the actual size PubSubClient fell back to (e.g. 256), `buffer_degraded` SHALL be `true`, and the warning log emitted at boot SHALL be visible in the serial output

#### Scenario: Publish returns false while buffer is degraded

- **WHEN** `MqttClient::publish(topic, payload)` is called while `buffer_degraded` is `true` and the underlying `PubSubClient::publish()` returns `false`
- **THEN** `truncated_publishes` SHALL increment by 1, the per-cycle `failed` counter SHALL also increment by 1, and a single `ESP_LOGE` line identifying the publish as a likely truncation SHALL be emitted for the cycle

#### Scenario: Publish returns false while buffer is healthy

- **WHEN** `MqttClient::publish(topic, payload)` is called while `buffer_degraded` is `false` and the underlying `PubSubClient::publish()` returns `false`
- **THEN** `truncated_publishes` SHALL NOT increment, the per-cycle `failed` counter SHALL increment by 1, and NO `ESP_LOGE` truncation line SHALL be emitted

#### Scenario: Buffer recovers on reconnect

- **WHEN** the broker disconnects and a subsequent MQTT (re)connect succeeds while the heap can now satisfy a 2048-byte allocation
- **THEN** the post-connect `setBufferSize(2048)` retry SHALL succeed, `buffer_size` SHALL become 2048, `buffer_degraded` SHALL become `false`, and an `ESP_LOGI` recovery line SHALL be emitted

#### Scenario: GET /api/mqtt includes the new fields

- **WHEN** a client GETs `/api/mqtt`
- **THEN** the response SHALL include `buffer_size` (integer), `buffer_degraded` (boolean), and `truncated_publishes` (integer ≥ 0) alongside the existing `stats` block

### Requirement: Control and actuator state is published

The firmware SHALL publish the thermostat's control and actuator state alongside sensor measurements: the setpoint, the control-enabled flag, the PID output as a percentage, and whether heating is confirmed. The setpoint and control-enabled topics SHALL be retained.

#### Scenario: Control-state topics and payloads

- **WHEN** the control and actuator state is published
- **THEN** the setpoint SHALL be published to `{prefix}/target_temperature` as its numeric value, the control-enabled flag to `{prefix}/control_enabled` as `ON` or `OFF`, the PID output to `{prefix}/control_output` as a percentage, and the confirmed heating state to `{prefix}/heating` as `ON` or `OFF`

#### Scenario: Control state survives a subscriber restart

- **WHEN** the setpoint or the control-enabled flag is published
- **THEN** it SHALL be published with the retain flag set, so a subscriber that connects later still receives the current value

#### Scenario: Published on change

- **WHEN** the setpoint, the control-enabled flag, the PID output or the heating state changes
- **THEN** the corresponding topic SHALL be published without waiting for the next measurement publish interval

#### Scenario: Not held back by the startup settle period

- **WHEN** the device has been up for less than 60 seconds and a control-state value changes or the MQTT client connects
- **THEN** the control-state publish SHALL still occur, because the 60-second hold applies only to sensor measurements

### Requirement: Control state is republished on every control-loop computation

The firmware SHALL republish the thermostat's full control and actuator state — setpoint, control-enabled flag, PID output and confirmed heating — on every control-loop computation, once per configured control interval. This SHALL be independent of the publish-on-change behaviour and of the sensor measurement interval, so a subscriber observes the setpoint and demand of each run even when nothing changed.

#### Scenario: Full set republished each computation

- **WHEN** the control loop completes a PID computation
- **THEN** all four control-state topics SHALL be published, including `{prefix}/target_temperature`, even if their values are unchanged since the previous publish

#### Scenario: Republish cadence follows the control interval

- **WHEN** the control interval is 60 seconds and the control loop computes at 60-second spacing
- **THEN** the control-state set SHALL be published approximately once per minute

#### Scenario: Not gated by the measurement interval or the settle period

- **WHEN** a control-loop computation occurs before the sensor measurement interval has elapsed, or within 60 seconds of boot
- **THEN** the control-state set SHALL still be published

### Requirement: Inbound commands set the setpoint and toggle control

The firmware SHALL subscribe to the setpoint and control-enabled command topics and apply received commands. A setpoint that is not a finite number within `[10.0, 30.0]` °C SHALL be rejected, leaving the stored value unchanged. The control command SHALL accept only `ON` and `OFF`.

#### Scenario: Subscribed on connect

- **WHEN** the MQTT client connects
- **THEN** it SHALL subscribe to `{prefix}/target_temperature/set` and `{prefix}/control_enabled/set`

#### Scenario: Valid setpoint command

- **WHEN** a setpoint command carrying `23.5` is received
- **THEN** the target temperature SHALL become `23.5` °C and SHALL be persisted

#### Scenario: Out-of-range or non-numeric setpoint command

- **WHEN** a setpoint command carrying `40.0`, a non-numeric payload, or a payload outside `[10.0, 30.0]` is received
- **THEN** the command SHALL be rejected and the stored setpoint SHALL remain unchanged

#### Scenario: Enable and disable commands

- **WHEN** a control command carrying `ON` is received the control SHALL be enabled and persisted, and when it carries `OFF` the control SHALL be disabled and persisted

#### Scenario: Unrecognised control payload

- **WHEN** a control command carries a payload other than `ON` or `OFF`
- **THEN** it SHALL be ignored and the control state SHALL remain unchanged
