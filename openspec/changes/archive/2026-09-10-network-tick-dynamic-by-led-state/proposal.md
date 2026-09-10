## Why

The Network task's per-iteration tick is currently a fixed 15 s
(see change `network-loop-adaptive-sleep`), set so the device idles
aggressively when there is no user activity. That is right for a device
that has been running quietly for hours with the status LED in dark
mode — there is nothing to react to, and 15 s is plenty for the slow
tasks (MQTT publish, NTP refresh, low-heap watchdog).

It is wrong for the first ten minutes of operation (or whenever the
LED is otherwise actively emitting): during that window the user is
typically near the device and may be observing behaviour, and the 1 s
cadence the rest of the firmware was built around — slow-log threshold,
low-heap watchdog timing, MQTT publish interval, NTP unsynced retry
— was the right choice. The current 15 s sleep, even when made
adaptive, still bottoms out at 15 s + `WAKE_MARGIN_MS`, so the
internal cadence gates coalesce into "every iteration" with no way to
recover the 1 s feel.

`DarkModeStatusLed` already exposes exactly the signal we need:
`bool isDark(uint32_t nowMs)` returns true once dark mode has actively
suppressed the LED. Switching `TICK_MS` between 1000 and 15000 based
on `isDark()` lets the Network task run at the right cadence for the
current state — fast when the LED is showing the user something,
slow when it has decided nobody is watching.

## What Changes

- The Network task SHALL compute its per-iteration tick at the top of
  each iteration as
  `tickMs = statusLed.isDark(static_cast<uint32_t>(millis())) ? 15000 : 1000`.
- The previous requirement's `TICK_MS` constant is replaced by this
  runtime computation. `WAKE_MARGIN_MS = 2` is unchanged.
- The slow-log DEBUG threshold (`workMs > 500`) is unchanged. At a
  1 s tick it remains "more than half the budget" (the originally
  intended signal); at a 15 s tick a healthy iteration will sit well
  below it. The line is `ESP_LOGD` and is off in production builds.
- When the LED transitions into or out of dark mode, the change takes
  effect on the next iteration's sleep. At most one iteration of
  cadence lag — acceptable because the transition itself (a user
  walking away, or returning) happens on a much slower timescale.
- No new API surface, no new HTTP fields, no new logging (the
  existing 15-min diagnostics line continues to log the `net_*`
  counters and would naturally reflect any cadence change).

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `networking`: modify the requirement added in change
  `network-loop-adaptive-sleep` ("Network task sleeps adaptively
  based on previous iteration's work") so `TICK_MS` becomes a
  runtime computation depending on the LED's dark state instead of
  a constant `15000`.

## Impact

- `src/Network.cpp` — replace the `static constexpr uint32_t TICK_MS = 15000`
  with a runtime `tickMs` computed from `statusLed.isDark(millis())`
  at the top of each iteration. `WAKE_MARGIN_MS` stays as a constant.
- No change to `src/Network.h` (the `statusLed` member and
  `DarkModeStatusLed` reference already exist).
- No change to `Support::Stats`, the snapshot accessor, the
  `/api/about` endpoint, or any other consumer.
- No native-test impact (the change is in `Network::task()` which is
  `ARDUINO`-only). Firmware build impact: none beyond the constant
  replacement.