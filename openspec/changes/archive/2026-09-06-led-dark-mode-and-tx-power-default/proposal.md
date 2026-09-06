## Why

Two small energy-related defects surfaced during a power review. `Constants::DEFAULT_WIFI_POWER` is `68`, which arduino-esp32 defines as `WIFI_POWER_17dBm`, while the constant's comment, the `EnergyConfig` field comment, and the settings UI fallback all say 13 dBm (`52`). Every device that never saved energy settings transmits 4 dB hotter than intended. Separately, the status NeoPixel redraws its green-to-red MQTT gradient roughly once a second and stays lit indefinitely; in a bedroom or living room that is unwanted light, and there is no way to turn it off short of disabling MQTT.

## What Changes

- Correct `Constants::DEFAULT_WIFI_POWER` from `68` (17 dBm) to `52` (13 dBm) so the default matches its documented intent. **BREAKING** for devices that never saved energy settings: they drop from 17 to 13 dBm on the next firmware boot. Devices with an explicitly saved value are unaffected.
- Add a **LED dark mode**: a new `EnergyConfig::led_dark_after_s` field (seconds, default `300`, `0` disables). Once the LED has been in the normal connected `ON` state for that long, it renders dark. The MQTT publish flash is suppressed too. `STARTUP` and `ERROR` always render. Any transition out of normal operation (`STARTUP`, `ERROR`, `OFF`) re-arms the timer, so a WiFi reconnect re-lights the LED for another period.
- Dark mode is implemented as a decorator, `DarkModeStatusLed`, that wraps `StatusLed` and forwards an effective state to it. `StatusLed` itself is unchanged apart from a `lastColor()` test accessor. The wrapper's `update(uint32_t nowMs)` takes a caller-supplied clock, following the `Display::RefreshPolicy` pattern, so the timing is unit-testable on the native build.
- `POST /api/settings/energy` accepts and `GET /api/settings/energy` returns `led_dark_after_s`. The POST handler only schedules a restart when `wifi_power` or `wifi_sleep_mode` actually changed; the LED setting is applied live.
- Settings UI: the Energy section gains a "LED dark mode" dropdown (Off / 1 min / 5 min / 30 min) and its restart warning is reworded to say a restart happens only when WiFi settings change.
- Spec cleanup in `status-led`: the incorrect "blinks at 1 Hz" and "uses `millis()`" statements are corrected to match the code.

## Capabilities

### New Capabilities

_None._

### Modified Capabilities

- `status-led`: adds a dark-mode rendering requirement; modifies the state-machine and periodic-update requirements to take an injected clock and to describe the dark-mode override of `ON` and `TRANSMIT_DATA`.
- `configuration`: `EnergyConfig` gains `led_dark_after_s` with its NVS key, default, and validation; the default WiFi TX power is fixed at 13 dBm.
- `http-api`: adds a Settings requirement for `GET/POST /api/settings/energy` covering the new field and the conditional restart.

## Impact

- `src/Constants.h`, `src/Config.h`, `src/Config.cpp`, `src/PrefsKeys.h`: default fix, new field, NVS load/save/validate.
- `src/DarkModeStatusLed.h/.cpp` (new): dark-mode anchor and threshold, clock-injected `update()`, live setter. `src/StatusLed.h/.cpp`: `lastColor()` accessor only. `main.cpp`, `Network`, `SensorController` hold the wrapper type.
- `src/Network.cpp`: passes `now` to `update()`, applies the loaded `led_dark_after_s` at task start, exposes a forwarder so the settings route can apply changes live.
- `src/routes/SettingsRoutes.cpp`: new JSON field, change detection before `requestRestart()`.
- `data/settings.html`: dropdown, JS load/save, warning text (regenerates `src/generated/settings_gz.h` via the pre-build hook).
- Tests: new `test/test_dark_mode_status_led` (dark-mode timing via injected clock), `test/test_sensor_controller_mutex_init` (wrapper type), `test/test_config` (defaults, validation, and the corrected TX power default).
- Specs: `openspec/specs/status-led`, `configuration`, `http-api`.
- Field behaviour: existing devices go dark five minutes after boot once upgraded, and unconfigured-energy devices transmit at 13 dBm. Both should be called out in release notes.
