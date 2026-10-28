# Spec Delta

## MODIFIED Requirements

### Requirement: MQTT configuration model

The firmware SHALL persist MQTT configuration in NVS as `MqttConfig` with fields `host`, `port`, `username`, `password`, `prefix`, `interval`, `enabled`, and `ha_discovery`. Default values SHALL be: `port = 1883`, `prefix = "sensors"`, `interval = 15` seconds, `enabled = false`, `ha_discovery = false`.

#### Scenario: Default config on fresh device

- **WHEN** the firmware boots with no stored MQTT config
- **THEN** `loadMqttConfig()` SHALL return the defaults above and MQTT SHALL remain disabled

#### Scenario: Discovery default off on fresh device

- **WHEN** the firmware boots with no stored MQTT config
- **THEN** `ha_discovery` SHALL be `false` and no Home Assistant discovery SHALL be published

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

#### Scenario: Degraded buffer is called out for the discovery payload

- **WHEN** Home Assistant discovery is enabled and the TX buffer is degraded below the size the discovery payload needs
- **THEN** the failed discovery publish SHALL be logged as a buffer truncation rather than failing silently

#### Scenario: GET /api/mqtt includes the new fields

- **WHEN** a client GETs `/api/mqtt`
- **THEN** the response SHALL include `buffer_size` (integer), `buffer_degraded` (boolean), and `truncated_publishes` (integer ≥ 0) alongside the existing `stats` block

## ADDED Requirements

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
