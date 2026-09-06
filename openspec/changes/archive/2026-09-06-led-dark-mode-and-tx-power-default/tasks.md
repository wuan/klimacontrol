## 1. TX power default

- [x] 1.1 Change `Constants::DEFAULT_WIFI_POWER` from `68` to `52` in `src/Constants.h`; confirm the comments in `src/Constants.h` and `src/Config.h` (`13 dBm`) now match
- [x] 1.2 Update `test/test_config/test_config.cpp` energy default and invalid-power fallback tests to expect `52`

## 2. EnergyConfig field and persistence

- [x] 2.1 Add `uint16_t led_dark_after_s = 300;` to `Config::EnergyConfig` in `src/Config.h`
- [x] 2.2 Add `ENERGY_LED_DARK_AFTER_S = "led_dark_s"` to `src/PrefsKeys.h`
- [x] 2.3 Extend `validateEnergyConfig()` in `src/Config.cpp` to clamp `led_dark_after_s` to at most `3600` (0 remains valid)
- [x] 2.4 Load (`getUShort`, default 300) and save (`putUShort`) the field in `loadEnergyConfig()` / `saveEnergyConfig()`; include it in the debug log lines
- [x] 2.5 Add native tests in `test/test_config`: default is 300, zero preserved, 7200 clamps to 3600

## 3. Dark-mode decorator

- [x] 3.1 Keep `StatusLed` a pure state-to-colour machine (`update()` unchanged); add only a `lastColor()` accessor so tests can observe the rendered colour on native
- [x] 3.2 Add `src/DarkModeStatusLed.h/.cpp`: owns a `StatusLed`, tracks the logical state, `std::atomic<uint32_t> darkAfterMs`, `darkAnchorArmed`, `onSinceMs`, `lastNowMs`; `setDarkAfterSeconds()`, `getDarkAfterSeconds()`, `isDark(nowMs)`, `inner()`
- [x] 3.3 In the wrapper's `setState()`, arm the anchor with `lastNowMs` when entering `ON` from a state other than `ON`/`TRANSMIT_DATA`; clear it on `OFF`, `STARTUP`, `ERROR`; leave it untouched for `ON`↔`TRANSMIT_DATA`
- [x] 3.4 In the wrapper, forward `OFF` to the inner LED in place of `ON`/`TRANSMIT_DATA` while dark; forward `STARTUP`/`ERROR` unchanged; `update(nowMs)` re-evaluates then calls `inner.update()`
- [x] 3.5 Switch `main.cpp`, `Network`, and `SensorController` to hold `DarkModeStatusLed`; add `+<DarkModeStatusLed.cpp>` to the native `build_src_filter`
- [x] 3.6 Keep `test/test_status_led` and `test/test_status_led_error` on the unchanged `StatusLed` API; update `test_sensor_controller_mutex_init` to construct the wrapper
- [x] 3.7 Add `test/test_dark_mode_status_led` covering every scenario in the `status-led` delta spec: inner OFF while logical ON, dark after threshold, gradient before threshold, publish flash suppressed while dark, flash does not reset anchor, reconnect re-arms, error/startup visible while dark, OFF clears anchor, threshold 0 disables, threshold change applies live, uint32 wrap-around, setState before first update

## 4. Network task wiring

- [x] 4.1 In `Network::task()`, call `statusLed.setDarkAfterSeconds(config.loadEnergyConfig().led_dark_after_s)` immediately after `statusLed.begin()`
- [x] 4.2 Change the per-iteration call to `statusLed.update(now)` (`src/Network.cpp`, the existing `statusLed.update();` line)
- [x] 4.3 Add `void Network::setLedDarkAfterSeconds(uint16_t seconds)` forwarding to the wrapper's `setDarkAfterSeconds()` (declare in `src/Network.h`)
- [x] 4.4 Update the stale "NTP updates and touch control" comment near the Network task loop header if touched, and fix any other `update()` callers that the compiler reports

## 5. HTTP API

- [x] 5.1 In `src/routes/SettingsRoutes.cpp` GET handler, add `doc["led_dark_after_s"]`
- [x] 5.2 In the POST handler, parse `led_dark_after_s` (accept `0..3600`, else HTTP 400 `{"success":false,"error":"Invalid led_dark_after_s value"}`)
- [x] 5.3 Capture `wifi_power`/`wifi_sleep_mode` before applying the JSON; after `saveEnergyConfig()`, call `requestRestart(1000)` only if either changed, otherwise call `network.setLedDarkAfterSeconds()`; respond with `{"success":true,"restart":true|false}`
- [x] 5.4 Update the handler's ESP_LOGI to include the LED value and whether a restart was scheduled

## 6. Settings UI

- [x] 6.1 In `data/settings.html` Energy section, add a `<select id="ledDarkAfter">` labelled "LED dark mode" with options Off (`0`), 1 min (`60`), 5 min (`300`), 30 min (`1800`) and a hint that STARTUP/error indications stay visible
- [x] 6.2 Update `loadEnergyConfig()` JS to populate the dropdown from `led_dark_after_s` (fall back to 300 if absent)
- [x] 6.3 Update `updateEnergy()` JS to send `led_dark_after_s`, only confirm the restart when a WiFi field differs from the loaded values, and pick the success message from `restart` in the response
- [x] 6.4 Reword the warning box to "The device restarts only when WiFi TX power or sleep mode is changed"
- [x] 6.5 Rebuild so `scripts/compress_web.py` regenerates `src/generated/settings_gz.h`

## 7. Specs and docs

- [x] 7.1 Apply the `status-led`, `configuration`, and `http-api` delta specs (they are archived into `openspec/specs/` on `/opsx:archive`; verify wording matches the implementation)
- [x] 7.2 Add release-note entries: default TX power is now 13 dBm for devices that never saved energy settings; LED goes dark after 5 minutes by default (configurable, Off available)
- [x] 7.3 Update `POWER_OPTIMIZATION.md`: note the corrected default and the new LED dark mode; remove the "LED update every 1 s" suggestion that no longer applies

## 8. Verification

- [x] 8.1 `pio test -e native` passes (config, status_led, status_led_error, dark_mode_status_led suites)
- [x] 8.2 `pio run -e adafruit_qtpy_esp32s2` builds with `-Werror`
- [x] 8.3 On hardware: LED goes dark ~5 min after association; publish flash absent while dark; disconnect/reconnect re-lights it; setting Off in the UI re-lights it without a restart; changing TX power still restarts; association log prints TX power 52 on a device with no saved energy settings
