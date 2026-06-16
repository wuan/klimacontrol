## 1. MqttClient header — new fields and accessors

- [x] 1.1 Add `uint16_t bufferSize = 0;` and `bool bufferDegraded = false;` private fields to `MqttClient` in `src/MqttClient.h`
- [x] 1.2 Add `uint32_t truncatedPublishes = 0;` private field and `uint32_t getTruncatedPublishes() const { return truncatedPublishes; }` accessor to `MqttClient` in `src/MqttClient.h`
- [x] 1.3 Add `uint16_t getBufferSize() const { return bufferSize; }` and `bool isBufferDegraded() const { return bufferDegraded; }` accessors to `MqttClient` in `src/MqttClient.h`

## 2. MqttClient — capture buffer state in `begin()`

- [x] 2.1 In `MqttClient::begin()` at `src/MqttClient.cpp:52`, after the `setBufferSize(MQTT_BUFFER_SIZE)` call, capture the actual size via `bufferSize = mqttClient.getBufferSize();`
- [x] 2.2 Set `bufferDegraded = (bufferSize < MQTT_BUFFER_SIZE);` right after the capture
- [x] 2.3 When `setBufferSize()` fails (the existing `ESP_LOGW` branch), augment the warning to include the actual `bufferSize` PubSubClient fell back to, e.g. `"setBufferSize(%u) failed — running with %u bytes (degraded)"`

## 3. MqttClient — increment `truncatedPublishes` in `publish()`

- [x] 3.1 In `MqttClient::publish()` at `src/MqttClient.cpp:138`, when `mqttClient.publish(topic, payload)` returns `false` AND `bufferDegraded` is `true`, increment `truncatedPublishes++` and emit a single `ESP_LOGE(TAG, "MQTT publish likely truncated: topic=%s (buffer_size=%u, requested=%u)", topic, bufferSize, MQTT_BUFFER_SIZE);` per failing call
- [x] 3.2 When `publish()` returns `false` AND `bufferDegraded` is `false`, do NOT increment `truncatedPublishes` and do NOT emit the truncation log line (existing behaviour preserved)

## 4. MqttClient — retry `setBufferSize()` on successful connect

- [x] 4.1 In `MqttClient::loop()` at `src/MqttClient.cpp:126` (the `if (connected)` branch), immediately after the `ESP_LOGI(TAG, "Connected to %s:%u", ...)` line, attempt `mqttClient.setBufferSize(MQTT_BUFFER_SIZE);`
- [x] 4.2 Read the new size back via `bufferSize = mqttClient.getBufferSize();` and update `bufferDegraded = (bufferSize < MQTT_BUFFER_SIZE);`
- [x] 4.3 If `bufferDegraded` transitioned from `true` to `false`, emit `ESP_LOGI(TAG, "MQTT buffer recovered: %u bytes", bufferSize);`; otherwise emit nothing (avoid log spam)

## 5. Web route — surface the three new fields in `GET /api/mqtt`

- [x] 5.1 In `src/routes/MqttRoutes.cpp` `GET /api/mqtt` handler (around line 25), add three top-level fields to the JSON document: `doc["buffer_size"] = mqtt->getBufferSize();`, `doc["buffer_degraded"] = mqtt->isBufferDegraded();`, `doc["truncated_publishes"] = mqtt->getTruncatedPublishes();`
- [x] 5.2 Confirm the new fields sit alongside the existing `stats` block (top-level, not inside `stats`)

## 6. Unit tests — new accessors

- [x] 6.1 In `test/test_mqtt_client/test_mqtt_client.cpp`, add `test_buffer_size_default_zero` that asserts `MqttClient::getBufferSize()` returns 0 on a freshly constructed instance
- [x] 6.2 Add `test_buffer_degraded_default_false` that asserts `MqttClient::isBufferDegraded()` returns `false` on a freshly constructed instance
- [x] 6.3 Add `test_truncated_publishes_default_zero` that asserts `MqttClient::getTruncatedPublishes()` returns 0 on a freshly constructed instance
- [x] 6.4 Wire the new tests into `runUnityTests()`

## 7. Audit, build, test, archive

- [x] 7.1 Grep the tree for the new field names to confirm they appear in `MqttClient.h`, `MqttClient.cpp`, the route handler, and the test file: `rg "bufferSize|bufferDegraded|truncatedPublishes|buffer_size|buffer_degraded|truncated_publishes" src/ test/`
- [x] 7.2 Build for ESP32 target: `pio run -e adafruit_qtpy_esp32s2` — expect zero new warnings
- [x] 7.3 Run native tests: `pio test -e native` — expect 233 + 3 new tests, all green
- [ ] 7.4 Archive the change with `/opsx:archive` to fold the spec deltas into `openspec/specs/mqtt-integration/spec.md` and `openspec/specs/http-api/spec.md`
