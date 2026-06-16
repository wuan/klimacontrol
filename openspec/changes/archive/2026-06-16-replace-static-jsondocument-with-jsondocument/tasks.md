## 1. Status routes

- [x] 1.1 Replace `StaticJsonDocument<512> doc;` at `src/routes/StatusRoutes.cpp:20` with `JsonDocument doc;`
- [x] 1.2 Replace `StaticJsonDocument<1024> doc;` at `src/routes/StatusRoutes.cpp:80` with `JsonDocument doc;`
- [x] 1.3 Replace `StaticJsonDocument<2048> doc;` at `src/routes/StatusRoutes.cpp:153` with `JsonDocument doc;`

## 2. Syslog routes

- [x] 2.1 Replace `StaticJsonDocument<512> doc;` at `src/routes/SyslogRoutes.cpp:20` with `JsonDocument doc;`
- [x] 2.2 Replace `StaticJsonDocument<512> doc;` at `src/routes/SyslogRoutes.cpp:42` with `JsonDocument doc;`

## 3. Control routes

- [x] 3.1 Replace `StaticJsonDocument<512> doc;` at `src/routes/ControlRoutes.cpp:24` with `JsonDocument doc;`

## 4. OTA routes

- [x] 4.1 Replace `StaticJsonDocument<512> doc;` at `src/routes/OTARoutes.cpp:27` with `JsonDocument doc;`
- [x] 4.2 Replace `StaticJsonDocument<512> doc;` at `src/routes/OTARoutes.cpp:37` with `JsonDocument doc;`
- [x] 4.3 Replace `StaticJsonDocument<512> doc;` at `src/routes/OTARoutes.cpp:98` with `JsonDocument doc;`
- [x] 4.4 Replace `StaticJsonDocument<512> doc;` at `src/routes/OTARoutes.cpp:134` with `JsonDocument doc;`

## 5. MQTT routes

- [x] 5.1 Replace `StaticJsonDocument<512> doc;` at `src/routes/MqttRoutes.cpp:18` with `JsonDocument doc;`
- [x] 5.2 Replace `StaticJsonDocument<512> doc;` at `src/routes/MqttRoutes.cpp:53` with `JsonDocument doc;`

## 6. I2C routes

- [x] 6.1 Replace `StaticJsonDocument<512> doc;` at `src/routes/I2CRoutes.cpp:16` with `JsonDocument doc;`

## 7. Settings routes

- [x] 7.1 Replace `StaticJsonDocument<512> doc;` at `src/routes/SettingsRoutes.cpp:29` with `JsonDocument doc;`
- [x] 7.2 Replace `StaticJsonDocument<512> doc;` at `src/routes/SettingsRoutes.cpp:78` with `JsonDocument doc;`
- [x] 7.3 Replace `StaticJsonDocument<512> doc;` at `src/routes/SettingsRoutes.cpp:116` with `JsonDocument doc;`
- [x] 7.4 Replace `StaticJsonDocument<512> doc;` at `src/routes/SettingsRoutes.cpp:203` with `JsonDocument doc;`
- [x] 7.5 Replace `StaticJsonDocument<512> doc;` at `src/routes/SettingsRoutes.cpp:223` with `JsonDocument doc;`

## 8. Sensor routes

- [x] 8.1 Replace `StaticJsonDocument<512> doc;` at `src/routes/SensorRoutes.cpp:22` with `JsonDocument doc;`
- [x] 8.2 Replace `StaticJsonDocument<512> doc;` at `src/routes/SensorRoutes.cpp:52` with `JsonDocument doc;`
- [x] 8.3 Replace `StaticJsonDocument<512> doc;` at `src/routes/SensorRoutes.cpp:81` with `JsonDocument doc;`
- [x] 8.4 Replace `StaticJsonDocument<1024> doc;` at `src/routes/SensorRoutes.cpp:122` with `JsonDocument doc;`

## 9. WebServerManager

- [x] 9.1 Replace `StaticJsonDocument<512> doc;` at `src/WebServerManager.cpp:117` with `JsonDocument doc;`
- [x] 9.2 Replace `StaticJsonDocument<128> responseDoc;` at `src/WebServerManager.cpp:158` with `JsonDocument responseDoc;`

## 10. Documentation

- [x] 10.1 Update the "JSON buffer" bullet in `AGENTS.md` (around line 464) to describe the v7 idiom: `JsonDocument` on the handler's stack frame; data via the default allocator, freed at handler return

## 11. Audit and verify

- [x] 11.1 Grep the tree for any residual `StaticJsonDocument` references in `src/` and confirm none remain: `rg "StaticJsonDocument" src/` should return no matches
- [x] 11.2 Build for ESP32 target: `pio run -e adafruit_qtpy_esp32s2` — expect zero `StaticJsonDocument` deprecation warnings on route-handler translation units
- [x] 11.3 Run native tests: `pio test -e native` — must remain green (route handlers are behind `#ifdef ARDUINO`, so the test build is unaffected, but guards against accidental include-graph breakage)
- [ ] 11.4 Archive the change with `/opsx:archive` to fold the spec deltas into `openspec/specs/http-api/spec.md` and `openspec/specs/memory-management/spec.md`
