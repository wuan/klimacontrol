## Why

The Network task (`src/Network.cpp`) currently sleeps for a fixed
`vTaskDelay(15000)` at the top of every iteration. When the iteration
itself takes 50 ms the task unnecessarily sleeps the full 15 s on top of
that — the device is idling for ~99.7% of its wall-clock time even
though nothing is waiting. When an iteration overruns (MQTT blocking,
NTP stalled, mDNS re-advertise), the task still waits the full 15 s
before noticing, delaying the next attempt to recover. The Sensor
Monitor task already does this correctly: it sleeps for
`tick - elapsed + WAKE_MARGIN_MS` after each iteration
(`src/task/SensorMonitor.cpp:103`,
`openspec/specs/sensor-management/spec.md:336`). The Network task should
do the same so a slow iteration re-evaluates quickly and a fast iteration
returns to sleep promptly.

## What Changes

- The Network task SHALL sleep for `TICK_MS - lastWorkMs + WAKE_MARGIN_MS`
  at the top of each iteration, where `lastWorkMs` is the integer
  millisecond duration of the previous iteration's work,
  `TICK_MS = 15000` (matching the current fixed sleep), and
  `WAKE_MARGIN_MS = 2` (mirroring `Task::SensorMonitor::WAKE_MARGIN_MS`).
- If `lastWorkMs >= TICK_MS`, the task SHALL sleep for one RTOS tick
  (1 ms) so it re-enters the work loop as soon as possible.
- The first iteration (with `lastWorkMs == 0`) SHALL sleep for
  `TICK_MS + WAKE_MARGIN_MS = 15002 ms` instead of the current
  `15000 ms`. This is harmless and matches the Sensor Monitor behaviour
  on first tick.

No breaking changes. No new API surface. The work-duration stats added
in `add-network-loop-timing-stats` already make the new sleep behaviour
visible at `GET /api/about` (`stats.net_*`).

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `networking`: add a requirement that the Network task computes its
  sleep adaptively from the previous iteration's work duration,
  mirroring the Sensor Monitor's "tick - elapsed + WAKE_MARGIN_MS"
  pattern specified at `sensor-management/spec.md:336`.

## Impact

- `src/Network.h` — add a `uint32_t lastWorkMs = 0;` member to carry the
  previous iteration's work duration across iterations. `TICK_MS` and
  `WAKE_MARGIN_MS` live as `static constexpr` inside the task body.
- `src/Network.cpp` — change the top-of-loop `vTaskDelay(15000)` to
  `vTaskDelay(pdMS_TO_TICKS(sleepMs))` where `sleepMs` is computed
  from `lastWorkMs`, and record `lastWorkMs = workMs` at the bottom
  of the iteration.
- No dependency, build, or hardware changes. The change is purely
  internal timing; the externally visible behaviour (loop cadence,
  log lines, API fields) is fully covered by the existing
  diagnostics and `/api/about` stats.