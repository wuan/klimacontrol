## Context

`initVariant()` in the Arduino variant for the QT Py ESP32-S2 sets `NEOPIXEL_POWER` (GPIO 38, on = HIGH) as an output and high before `setup()`. `StatusLed` only ever writes colour data; `DarkModeStatusLed` turns the pixel black by forwarding `OFF`. A black WS2812 still draws roughly 1 mA.

## Goals / Non-Goals

**Goals:**

- Cut the pixel supply while dark mode holds the LED dark, restore it the moment dark mode releases, and re-render the colour the pixel lost while unpowered.
- Keep `StatusLed` a state-to-colour machine: the rail is a separate, explicit control, not a side effect of `OFF`.

**Non-Goals:**

- Cutting the rail for a plain `OFF` state, during boot, or in `ERROR`. The user asked for dark mode only; `OFF` before `begin()` and short `OFF` transitions are not worth a rail cycle.
- Any change to when dark mode engages.

## Decisions

**D1: The rail is controlled by `StatusLed`, decided by `DarkModeStatusLed`.**
`StatusLed` owns the pin, so it gets `setPowerRail(bool)`. The wrapper owns the dark-mode policy, so it is the only caller. Alternative: fold the rail into `StatusLed::OFF`. Rejected because it would also cut power on every `OFF` and the user explicitly scoped this to dark mode.

**D2: Black first, then rail low; rail high, then forget the last colour.**
Writing black before dropping the rail leaves the data line idle-low into an unpowered part. On power-up the pixel's latch is empty, so `lastShownColor` is reset to the impossible sentinel to force the next `showColor()` to write even if the colour is unchanged. `applyEffectiveState()` calls `led.setState()` after raising the rail, and `update()` calls `led.update()` right after, so the re-render happens in the same iteration.

**D3: Edge-triggered.**
`DarkModeStatusLed` remembers whether it is currently suppressing and touches the rail only on a change, so the 1 s `update()` does not toggle a GPIO every second.

## Risks / Trade-offs

- A WS2812 needs a few microseconds after power-up before accepting data. The first frame after re-powering may be missed; the ON gradient changes colour every second, so the pixel catches up on the next `update()`. `STARTUP`/`ERROR` are re-rendered each iteration only when the colour changes, but `lastShownColor` was reset so the first write after power-up happens and any missed frame is corrected by the state's next transition. Accepted as negligible for a status indicator.
