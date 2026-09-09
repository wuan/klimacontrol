# status-led Specification

## Purpose
TBD - created by archiving change baseline-capabilities. Update Purpose after archive.
## Requirements
### Requirement: Hardware target

The firmware SHALL drive the single built-in NeoPixel on the Adafruit QT Py ESP32-S2 board, using the `NEO_GRB` color order.

#### Scenario: Pixel write

- **WHEN** `StatusLed::show()` is called
- **THEN** exactly one NeoPixel SHALL be updated, with the current color expressed in GRB order

### Requirement: State machine

The LED SHALL be controlled by a `LedState` enum with the values `OFF`, `ON`, `STARTUP`, `TRANSMIT_DATA`, `ERROR`. `StatusLed` SHALL provide `setState(LedState)` (applied immediately), `update()` (re-renders the current state; no clock, the class has no time dependency), and a private colour writer that pushes to the NeoPixel only when the colour changed. The current state SHALL be readable via `getState()`, and the most recently rendered colour via `lastColor()` for tests and diagnostics.

#### Scenario: State transition

- **WHEN** `setState(LedState::ON)` is called
- **THEN** the LED SHALL immediately render the solid-on color, subsequent `update()` calls SHALL keep it there, and `getState()` SHALL return `ON`

#### Scenario: Transition to error state

- **WHEN** `setState(LedState::ERROR)` is called
- **THEN** `getState()` SHALL return `ERROR` and the LED SHALL render solid red

### Requirement: State-to-behavior mapping

- `OFF` SHALL leave the LED dark.
- `ON` SHALL display a steady color encoding the MQTT publish progress as a green→red gradient (driven by `setProgress(float)`); when progress is unavailable the color SHALL be plain green. When the `DarkModeStatusLed` wrapper has engaged dark mode it forwards `OFF` instead, so the pixel is dark.
- `STARTUP` SHALL display a steady dim blue to indicate boot/association is in progress.
- `TRANSMIT_DATA` SHALL show a brief dim white to indicate an MQTT publish. When the wrapper has engaged dark mode it forwards `OFF` instead, so no flash is visible.
- `ERROR` SHALL display solid red to indicate a fatal init error (e.g. mutex allocation failure in `SensorController`). The wrapper SHALL always forward `ERROR` unchanged.

#### Scenario: Boot indicator

- **WHEN** the firmware is in the early-boot path before WiFi is associated
- **THEN** the LED SHALL be in `STARTUP` state and SHALL show dim blue

#### Scenario: MQTT publish flash

- **WHEN** a measurement is about to be published via MQTT and dark mode has not engaged
- **THEN** the network task SHALL call `setState(LedState::TRANSMIT_DATA)`, producing a short visible flash

#### Scenario: Error indicator

- **WHEN** the firmware drives the LED to indicate a fatal init error
- **THEN** the LED SHALL be in `ERROR` state and SHALL display solid red so a human looking at the device can see the cause

### Requirement: Periodic update from the network task

The network task SHALL hold a `DarkModeStatusLed` and call its `update(now)` on each 1-second iteration, passing its own `millis()` sample, so time-dependent policy (dark mode) advances correctly. Neither `StatusLed` nor the wrapper SHALL read `millis()` itself; the clock is always supplied by the caller so both classes are testable on the native build. The network task SHALL apply the persisted `led_dark_after_s` via `setDarkAfterSeconds()` immediately after `begin()`, before the first `ON` transition.

#### Scenario: Cadence guarantee

- **WHEN** the network task is running normally
- **THEN** `DarkModeStatusLed::update(nowMs)` SHALL be invoked at least once per second with a monotonically non-decreasing clock

#### Scenario: Persisted threshold in force at boot

- **WHEN** NVS holds `led_dark_after_s = 60` and the device boots and associates
- **THEN** the LED SHALL go dark 60 seconds after the first `ON` transition

### Requirement: Dark mode after sustained normal operation

Dark mode SHALL be implemented by a decorator class `DarkModeStatusLed` that owns a `StatusLed` and exposes the same call surface (`begin`, `setState`, `getState`, `setProgress`, `getProgress`, `on`, `off`, `toggle`) plus `update(uint32_t nowMs)`, `setDarkAfterSeconds(uint16_t seconds)` (where `0` disables), `getDarkAfterSeconds()`, `isDark(uint32_t nowMs)`, and `inner()`. `StatusLed` itself SHALL remain a pure state-to-colour mapping with no time dependency. The firmware (`main.cpp`, `Network`, `SensorController`) SHALL hold the wrapper, never the wrapped `StatusLed` directly.

The wrapper SHALL track the logical state requested by callers and forward an effective state to the wrapped LED. It SHALL record a dark-mode anchor timestamp when the logical state enters `ON` from any state other than `ON` or `TRANSMIT_DATA`, and SHALL clear that anchor on any transition to `OFF`, `STARTUP`, or `ERROR`. A transition between `ON` and `TRANSMIT_DATA` in either direction SHALL NOT change the anchor.

When the anchor is armed, the threshold is non-zero, and `nowMs - anchor >= threshold` (wrap-safe unsigned arithmetic), the wrapper SHALL forward `OFF` to the wrapped LED in place of `ON` or `TRANSMIT_DATA`, so the pixel renders dark (`0x000000`), and SHALL then call `setPowerRail(false)` on the wrapped LED. When that suppression ends for any reason (threshold change, logical transition to `OFF`, `STARTUP`, or `ERROR`, or the anchor being re-armed) the wrapper SHALL call `setPowerRail(true)` before forwarding the new effective state. The rail SHALL be changed only on these two edges; the wrapper SHALL NOT touch the rail for any transition that does not engage or release dark mode. `STARTUP` and `ERROR` SHALL always be forwarded unchanged. `getState()` on the wrapper SHALL continue to return the logical state while dark.

The threshold SHALL be stored in a `std::atomic<uint32_t>` so it can be written from the HTTP handler task while the network task reads it.

#### Scenario: Wrapped LED is OFF while the logical state is ON

- **WHEN** dark mode has engaged with the logical state `ON`
- **THEN** `getState()` SHALL return `ON`, `inner().getState()` SHALL return `OFF`, and `isDark(nowMs)` SHALL return true

#### Scenario: Power rail is cut while dark

- **WHEN** dark mode has engaged
- **THEN** `inner().isPowerRailOn()` SHALL return false

#### Scenario: Power rail restored when dark mode releases

- **WHEN** dark mode has engaged and then `setDarkAfterSeconds(0)` is called and `update(nowMs)` runs, or `setState(STARTUP)` or `setState(ERROR)` is called
- **THEN** `inner().isPowerRailOn()` SHALL return true and the wrapped LED SHALL render the effective state's colour

#### Scenario: Logical OFF outside dark mode leaves the rail on

- **WHEN** dark mode has not engaged and `setState(OFF)` is called
- **THEN** `inner().isPowerRailOn()` SHALL return true

#### Scenario: LED goes dark after the threshold

- **WHEN** `setDarkAfterSeconds(300)` is set, `setState(ON)` is called at `nowMs = 1000`, and `update()` is later called with `nowMs = 301000`
- **THEN** the wrapped LED SHALL be rendered dark and the wrapper's `getState()` SHALL return `ON`

#### Scenario: Publish flash is suppressed while dark

- **WHEN** dark mode has engaged and the network task calls `setState(TRANSMIT_DATA)`
- **THEN** the LED SHALL remain dark and the anchor SHALL be unchanged

#### Scenario: Gradient still shows before the threshold

- **WHEN** `setDarkAfterSeconds(300)` is set, `setState(ON)` is called at `nowMs = 1000`, and `update()` is called with `nowMs = 200000`
- **THEN** the LED SHALL render the normal green-to-red gradient colour

#### Scenario: Reconnect re-arms the timer

- **WHEN** dark mode has engaged, the state transitions to `STARTUP`, and then back to `ON` at `nowMs = T`
- **THEN** the LED SHALL render the gradient until `nowMs - T >= threshold`, after which it SHALL be dark again

#### Scenario: Error always visible

- **WHEN** dark mode has engaged and `setState(ERROR)` is called
- **THEN** the LED SHALL render solid red

#### Scenario: Threshold zero disables dark mode

- **WHEN** `setDarkAfterSeconds(0)` is set and the state has been `ON` for longer than any previous threshold
- **THEN** the LED SHALL render the normal gradient

#### Scenario: Threshold change applies live

- **WHEN** the LED is `ON` and dark, and `setDarkAfterSeconds(0)` is called from another task
- **THEN** the wrapper's next `update(nowMs)` SHALL forward `ON` again and the gradient SHALL render, without a logical state transition or restart

### Requirement: NeoPixel power rail control

`StatusLed` SHALL provide `setPowerRail(bool on)` and `isPowerRailOn()`. The rail SHALL be reported on after construction, matching the board variant's power-on state. `setPowerRail(false)` SHALL first render black and then drive `NEOPIXEL_POWER` to the inactive level; `setPowerRail(true)` SHALL drive `NEOPIXEL_POWER` to `NEOPIXEL_POWER_ON` and SHALL discard the remembered last colour so the next render writes the pixel unconditionally. `setState(LedState::OFF)` and `update()` SHALL NOT change the rail. On the native build the rail flag SHALL be tracked without a pin write.

#### Scenario: Plain OFF leaves the rail on

- **WHEN** `setState(LedState::ON)` is followed by `setState(LedState::OFF)`
- **THEN** `isPowerRailOn()` SHALL return true

#### Scenario: Re-powering forces a re-render

- **WHEN** the LED is `ON`, `setPowerRail(false)` is called, `setPowerRail(true)` is called, and `update()` is called
- **THEN** `lastColor()` SHALL be the `ON` colour after `update()`, even though it was unchanged before the rail cycle

