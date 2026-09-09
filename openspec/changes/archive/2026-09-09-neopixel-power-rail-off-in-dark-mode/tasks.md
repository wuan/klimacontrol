## 1. StatusLed rail control

- [x] 1.1 Add `setPowerRail(bool on)` and `isPowerRailOn()` to `StatusLed`; rail-off writes black then drives `NEOPIXEL_POWER` to `!NEOPIXEL_POWER_ON`; rail-on drives the pin to `NEOPIXEL_POWER_ON` and resets `lastShownColor`.
- [x] 1.2 Native tests in `test/test_status_led`: rail defaults on, `OFF` does not touch it, rail-on forces a re-render.

## 2. DarkModeStatusLed

- [x] 2.1 Track `suppressed`; on engage call `led.setState(OFF)` then `led.setPowerRail(false)`; on release call `led.setPowerRail(true)` then forward the logical state.
- [x] 2.2 Native tests in `test/test_dark_mode_status_led`: rail off while dark, back on after threshold change to 0, on reconnect, on `ERROR`; plain `OFF` leaves the rail on.

## 3. Verification

- [x] 3.1 `pio test -e native -f test_status_led -f test_dark_mode_status_led` passes.
- [x] 3.2 `pio run -e adafruit_qtpy_esp32s2` builds.
- [x] 3.3 `openspec validate --all --strict` passes from the repo root.
