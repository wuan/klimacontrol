## Why

The Network task's tick was made LED-aware: 1 s while the status LED is
visible, 15 s once dark mode has engaged. In practice the 15 s coarse
tick was not worth its cost. Everything the loop services — MQTT
`loop()` keepalives, reconnect back-off, NTP retry, WiFi state checks,
the web server's request-side bookkeeping — was designed around a 1 s
cadence, and stretching it to 15 s made MQTT keepalive timing fragile
and delayed recovery from transient stalls by up to 15 s with no
measurable power or heap benefit on a mains-powered device. The code has
been reverted to a fixed 1 s tick; this change brings the spec back in
line with it.

## What Changes

- The Network task's tick is a fixed `1000` ms (`TICK_MS_FINE`) again.
  It no longer consults `DarkModeStatusLed::isDark()`.
- The adaptive sleep formula is retained: a fast iteration still sleeps
  `1000 - lastWorkMs + WAKE_MARGIN_MS`. An iteration whose work took
  `>= 1000` ms now skips the delay entirely and re-enters the loop
  immediately (the earlier draft slept one RTOS tick).
- MQTT publishes are gated solely by the configured publish interval;
  the short-lived "publish on every coarse tick" shortcut is gone.
- Scenarios describing the 15 s dark-mode budget and the tick switching
  on LED transitions are removed.

## Capabilities

### Modified Capabilities

- `networking`: the requirement "Network task sleeps adaptively based on
  previous iteration's work" no longer varies the tick with the LED's
  dark state.

## Impact

- `src/Network.cpp` (`Network::task()`): already reverted in the working
  tree; no further code change.
- `openspec/specs/networking/spec.md`: one requirement rewritten.
- `status-led` spec is unaffected; it already describes the network task
  as calling `DarkModeStatusLed::update()` on each 1-second iteration.
