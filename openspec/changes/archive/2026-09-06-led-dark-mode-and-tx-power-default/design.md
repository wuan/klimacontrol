## Context

The status NeoPixel is driven by a small `LedState` machine in `StatusLed` (`OFF`, `ON`, `STARTUP`, `TRANSMIT_DATA`, `ERROR`). The Network task calls `statusLed.update()` once per second and, in the MQTT block, calls `setProgress()` followed by `setState(LedState::ON)` every second; on each publish it first sets `TRANSMIT_DATA`, and one second later the same loop sets `ON` again. `StatusLed` has no notion of time today: the spec claims it uses `millis()`, but the code never reads a clock, and the documented `STARTUP` blink is in fact a solid blue.

Energy settings live in `EnergyConfig` (`wifi_power`, `wifi_sleep_mode`), persisted in NVS by `ConfigManager::load/saveEnergyConfig()` and validated by `validateEnergyConfig()`. They are exposed at `GET/POST /api/settings/energy` in `SettingsRoutes.cpp`; the POST handler unconditionally calls `config.requestRestart(1000)` because both existing fields are only applied in `Network::connectSTA()`. The settings page warns that the device will restart.

`Constants::DEFAULT_WIFI_POWER = 68`. In arduino-esp32, `WIFI_POWER_17dBm = 68` and `WIFI_POWER_13dBm = 52`. Both code comments and the UI fallback (`data.wifi_power || 52`) say 13 dBm.

Constraints: `StatusLed`, the new wrapper, and `Config.cpp` are in the native test build filter, so anything added must compile without Arduino and be testable there. The Network task is the only writer of the LED in steady state; the settings route runs on the AsyncTCP task.

## Goals / Non-Goals

**Goals:**
- Make the default TX power match its documented 13 dBm intent.
- Let the LED go dark after a configurable period of normal operation, with the publish flash suppressed, while keeping `STARTUP` and `ERROR` always visible.
- Re-light the LED for a fresh period whenever the device leaves normal operation (reconnect, error, boot).
- Apply the LED setting live; stop rebooting the heating controller for settings that do not need it.
- Keep the LED behaviour fully unit-tested on the native build.

**Non-Goals:**
- No new `LedState`. The state machine stays as is.
- No dimming levels, schedules, or night-time windows. A single "dark after N seconds" is enough.
- No coupling to the e-paper display. Devices with a display keep their LED behaviour identical to devices without one.
- No change to how `wifi_power` or `wifi_sleep_mode` are applied (still at association, still via restart).
- No RSSI-adaptive TX power.

## Decisions

### D1. Dark mode is a decorator around `StatusLed`, not a change to it

`StatusLed` stays a pure state-to-colour machine with no notion of time. A new class `DarkModeStatusLed` owns a `StatusLed` by value and offers the same call surface (`begin`, `setState`, `getState`, `setProgress`, `on/off/toggle`) plus `update(uint32_t nowMs)`, `setDarkAfterSeconds()`, `isDark()`, and `inner()`. It tracks the *logical* state the firmware asked for and forwards an *effective* state to the wrapped LED: identical, except that `ON` and `TRANSMIT_DATA` are forwarded as `OFF` once dark mode has engaged. `STARTUP` and `ERROR` always pass through. `getState()` on the wrapper returns the logical state.

The wrapper keeps a `darkAfterMs` threshold (0 = disabled) and an `onSinceMs` anchor. The anchor is set when the logical state enters `ON` from any state other than `ON` or `TRANSMIT_DATA`; it is cleared on transition to `OFF`, `STARTUP`, or `ERROR`. `ON`↔`TRANSMIT_DATA` leaves it untouched, so the 15 s publish flash cannot keep resetting the timer.

`main.cpp` owns a `DarkModeStatusLed`; `Network` and `SensorController` hold that type instead of `StatusLed`. `SensorController` goes through the wrapper (not `inner()`) so its `ERROR` cannot be overridden by the wrapper's next `update()`.

*Why a wrapper rather than adding the policy inside `StatusLed`?* Separation of concerns: `StatusLed` remains trivially testable as "state X renders colour Y", and every time-based policy (dark mode today, possibly night schedules later) composes on top without touching the state machine or its existing tests.

*Why not a `LedState::DARK`?* The Network task re-issues `setState(ON)` every second and `setState(TRANSMIT_DATA)` every publish. A DARK state would be knocked back to ON on the next tick, forcing the Network task to know about dark mode, and would make `getState()` lie about what the device is doing.

*Why not a timer in `Network::task()`?* It scatters LED policy across a 900-line task and still fights the per-second `setState(ON)` call.

*Why anchor on entering `ON` rather than boot?* A reconnect then re-lights the LED for a full period, which surfaces WiFi instability visually at no cost.

### D2. Caller-supplied clock: `DarkModeStatusLed::update(uint32_t nowMs)`

The wrapper's `update()` takes the monotonic millisecond clock as a parameter, exactly as `Display::RefreshPolicy::evaluate(..., uint32_t nowMs, ...)` does. `Network::task()` already has `now` in scope at the call site. `setState()` applies immediately using the last `nowMs` seen by `update()` (`lastNowMs`), so callers without a clock (the `SensorController` error path, `Network::setStatusLedState`) need no changes. Wrap-safe `uint32_t` subtraction is used throughout. `StatusLed::update()` itself keeps its no-argument signature.

*Alternative rejected:* calling `millis()` inside the wrapper under `#ifdef ARDUINO` with a fake on native. It makes the tests depend on a global fake clock rather than an explicit parameter.

### D3. Setting lives in `EnergyConfig` as `uint16_t led_dark_after_s`, default 300, 0 = disabled

One integer replaces a boolean plus a hard-coded timeout, so the period can be tuned without a release. NVS key `led_dark_s` (under 15 characters, consistent with the short-key convention noted at `PrefsKeys::ENERGY_WIFI_SLEEP_MODE`). `validateEnergyConfig()` clamps to at most 3600 s; 0 is valid and means disabled. The defaulted struct is what the UI's "5 min" option maps to.

*Alternative rejected:* a separate `LedConfig` struct with its own route and UI section. Correct separation on paper, but a new struct, route, spec section, and UI section for one field is disproportionate.

### D4. `POST /api/settings/energy` restarts only when a WiFi field changed

The handler loads the current `EnergyConfig`, applies the JSON fields, saves, then compares `wifi_power` and `wifi_sleep_mode` before/after. If either differs it calls `requestRestart(1000)` as today. Otherwise it applies `led_dark_after_s` live via `network.setLedDarkAfterSeconds()` (a thin forwarder to the wrapper's `setDarkAfterSeconds()`) and responds without restarting. When a restart *is* scheduled the LED value is picked up at task start, so no live call is needed in that branch. The success JSON carries `"restart": true|false` so the UI can show the right message.

*Why this matters:* the heating actuator lease closes the valve around a reboot, so restarting to toggle an LED would interrupt heating. It also fixes an existing rough edge where re-saving unchanged values rebooted the device for nothing.

*Thread safety:* `darkAfterMs` in `DarkModeStatusLed` is a `std::atomic<uint32_t>` because the AsyncTCP task writes it while the Network task reads it. All other wrapper members remain touched only from the Network task (plus the one-off `SensorController` error path at init, which precedes steady state).

### D5. Boot wiring

`Network::task()` calls `statusLed.setDarkAfterSeconds(config.loadEnergyConfig().led_dark_after_s)` right after `statusLed.begin()` so the persisted value is in force before the first `ON`. `connectSTA()` already loads `EnergyConfig` for TX power; that load is left as is rather than merged, to keep the change small.

### D6. TX power default fix

`Constants::DEFAULT_WIFI_POWER` becomes `52`. Comments at `Constants.h` and `Config.h` already say 13 dBm and need no edit. `validateEnergyConfig()` falls back to the constant for invalid stored values, so it inherits the fix. The allowed set `{8, 34, 52, 68, 80}` and the UI options are unchanged.

## Risks / Trade-offs

- [Devices that never saved energy settings drop from 17 to 13 dBm; one at the edge of coverage could start flapping] → The existing resilience path (forced reconnect, flapping backstop, AP fallback) recovers it, and the post-association log line already prints TX power. Called out in release notes; users can re-select 17 dBm in the UI.
- [Upgraded devices go dark five minutes after boot; someone using the gradient as a glance indicator may think the device died] → `ERROR` still shows red, the web UI shows status, and the dropdown can set Off. Release notes.
- [Dark mode hides a lost MQTT connection] → It already does: today the LED shows a green gradient regardless of MQTT state as long as WiFi is up. No regression; noted as a possible follow-up.
- [Wrapper `setState()` applying immediately via `lastNowMs`: a `setState(ON)` issued before the first `update(nowMs)` would arm the anchor at 0] → Acceptable: the Network task calls `update()` before any `setState(ON)`, and an anchor of 0 with `darkAfterMs = 300000` merely means "dark once uptime exceeds 5 min", which is the intended behaviour anyway.
- [Race on `darkAfterMs` between tasks] → atomic; a stale read for one tick is harmless.
- [Energy-saving benefit is small (a few mA)] → Understood and stated in the proposal; the primary value is the dark room and one fewer RMT write per second.

## Migration Plan

1. Ship firmware. NVS gains `led_dark_s` on first save; absent key reads as default 300.
2. No data migration. Rollback is a plain firmware downgrade: the old firmware ignores the unknown NVS key, and an explicitly saved `wifi_power` persists across versions.
3. Release notes mention both field-visible behaviour changes.

## Open Questions

- None blocking. Whether devices with an e-paper display should force dark mode is a possible follow-up, deliberately excluded here.
