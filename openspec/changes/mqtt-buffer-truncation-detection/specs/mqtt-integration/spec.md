## ADDED Requirements

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
