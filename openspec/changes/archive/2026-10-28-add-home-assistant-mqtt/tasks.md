# Tasks

## 1. Configuration and API surface

- [x] 1.1 Add `ha_discovery` (default `false`) to `Config::MqttConfig`, its NVS key, and `validateMqttConfig()`; verify with a round-trip case in `test/test_config` that the flag persists and defaults to `false`
- [x] 1.2 Expose `ha_discovery` through `GET /api/mqtt` and accept it in `POST /api/mqtt`; verify a POST of `{"ha_discovery":true}` persists and a subsequent GET reports it
- [x] 1.3 Add a "Home Assistant Discovery" toggle to the MQTT section of `data/settings.html` (load/save via `/api/mqtt`) and let the web-compression pre-build hook regenerate `src/generated/settings_gz.h`; verify the settings page loads and round-trips the toggle

## 2. MQTT client foundations

- [x] 2.1 Raise `MQTT_BUFFER_SIZE` to 2048 and update the boot/recovery log text; verify the boot log reports a 2048-byte buffer and `GET /api/mqtt` shows `buffer_degraded: false`
- [x] 2.2 Add a retain-capable `MqttClient::publish(topic, payload, bool retain)` overload (QoS 0 semantics unchanged) and keep the degraded/healthy accounting intact; verify via `test/test_mqtt_client` that the accounting paths still pass
- [x] 2.3 Register the Last Will on connect (topic `{prefix}/status`, payload `offline`, retain set) and publish a retained `online` on connect and retained `offline` on clean disconnect; verify with a broker that `{prefix}/status` reads `online` while connected, becomes `offline` after a clean restart, and is set to `offline` by the broker after an unclean kill

## 3. Control-state publishing

- [x] 3.1 Give `MqttPublisher` access to the control/actuator state it publishes (the `TemperatureController` already on the Network task); verify the firmware still builds with no new cross-task writer introduced
- [x] 3.2 Publish `{prefix}/target_temperature`, `{prefix}/control_enabled`, `{prefix}/control_output` and `{prefix}/heating` with the documented payloads and retention; verify with `mosquitto_sub` that each topic shows the expected value and shape and that the first two are retained
- [x] 3.3 Publish a control-state topic when its value changes and republish all of them on connect, independent of the measurement interval and the 60 s settle period; verify on device that changing the setpoint moves `target_temperature` within ~1 s, including in the first 60 s after boot
- [x] 3.4 Add native coverage for the control-state payload formatting (setpoint number, `ON`/`OFF`, percent, heating) in `test/test_mqtt_payload`; verify the new cases pass

## 4. Inbound commands

- [x] 4.1 Extract the setpoint-range check into a pure predicate and reuse it in `POST /api/temperature/target`; verify with a native test covering in-range, out-of-range and non-finite inputs, and that the route still rejects out-of-range values
- [x] 4.2 Register the PubSubClient callback (static instance trampoline) and, on connect, subscribe to `{prefix}/target_temperature/set` and `{prefix}/control_enabled/set`; verify with a broker that a publish to each topic reaches the handler (log line)
- [x] 4.3 Apply a valid setpoint command through `TemperatureController::setTargetTemperature`, reject out-of-range or non-numeric payloads with a log, and apply `ON`/`OFF` through `setControlEnabled` while ignoring unknown control payloads; verify end-to-end that publishing `23.5` updates `target_temperature` and survives a reboot, publishing `ON`/`OFF` flips `control_enabled`, and an out-of-range value leaves the setpoint unchanged

## 5. Home Assistant discovery payload

- [x] 5.1 Implement `formatHaDiscoveryPayload()` as a pure function emitting the single device-discovery payload (device block, `~` base topic, abbreviated keys, five components); verify in `test/test_mqtt_payload` that parsing the output yields the expected device identifiers and five components with the documented topics
- [x] 5.2 Add a worst-case size assertion (63-char prefix, 31-char device name) that the encoded payload plus topic plus framing fits within `MQTT_BUFFER_SIZE`; verify the test fails if an extra field is added past the buffer

## 6. Discovery lifecycle

- [x] 6.1 Publish the retained discovery message after every successful connect when `ha_discovery` is enabled, outside the settle period, and ensure a degraded buffer is logged as a truncation rather than failing silently; verify in HA that the device and all five entities appear after enabling the toggle, and that the truncation log appears if the buffer is forced degraded
- [x] 6.2 Publish an empty retained payload to the discovery topic when `ha_discovery` is turned off; verify the device disappears from Home Assistant
- [x] 6.3 Republish discovery after a reconnect; verify that dropping and restoring the broker link brings the entities back without re-toggling the setting
- [x] 6.4 Update the user-facing MQTT documentation (the settings hint and any MQTT docs) to describe the toggle, the topics and the availability behaviour; verify the documented steps match what the device actually does

## 7. Integration verification

- [x] 7.1 Run `pio test -e native` and confirm all suites pass, including the new payload and size tests
- [x] 7.2 Run `pio run -e adafruit_qtpy_esp32s2` for a clean firmware build; confirm the boot log shows the new buffer size and note whether the allocation used PSRAM or internal SRAM
- [x] 7.3 Run `openspec validate --all --strict` from the repo root and confirm the change and the main specs pass
- [x] 7.4 End-to-end in Home Assistant: confirm the thermostat shows current temperature, target and off/heat, that the humidity/dew-point/PID/heating entities appear, that changing the setpoint and toggling off/heat from HA reach the device, and that cutting power marks the entities unavailable

## Workflow follow-up

- Archive the change once implementation and verification are complete and the project's review requirements are satisfied.
