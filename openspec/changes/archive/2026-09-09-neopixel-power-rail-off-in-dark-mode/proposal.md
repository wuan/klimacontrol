## Why

When dark mode engages, `DarkModeStatusLed` forwards `OFF` and the NeoPixel renders black, but the pixel's supply rail stays up: the QT Py ESP32-S2 variant drives `NEOPIXEL_POWER` (GPIO 38) high in `initVariant()` and nothing ever lowers it. A dark WS2812 still draws its quiescent current. Dark mode exists so the device runs cooler and draws less; cutting the rail finishes the job.

## What Changes

- `StatusLed` gains `setPowerRail(bool on)` and `isPowerRailOn()`. Turning the rail off writes black to the pixel first and then drives `NEOPIXEL_POWER` low; turning it on drives the pin high and forgets the last shown colour so the next render re-pushes it to the freshly powered pixel.
- `DarkModeStatusLed` lowers the rail when dark mode engages and raises it when dark mode releases (threshold change, reconnect, `STARTUP`, `ERROR`). The rail is touched **only** by dark mode: a logical `OFF`, boot, or any other state leaves the rail in the variant's power-on state.
- Native build: the pin write is `#ifdef ARDUINO`; the rail flag is tracked on both builds so the behaviour is tested natively.

## Capabilities

### New Capabilities

_None._

### Modified Capabilities

- `status-led`: "Dark mode after sustained normal operation" additionally requires the power rail to follow dark mode; a new requirement defines the `StatusLed` rail control.

## Impact

- `src/StatusLed.{h,cpp}`, `src/DarkModeStatusLed.{h,cpp}`.
- `test/test_dark_mode_status_led`, `test/test_status_led`.
- No config, API, or network change.
