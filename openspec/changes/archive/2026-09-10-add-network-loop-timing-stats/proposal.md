## Why

The Network task (`src/Network.cpp`) runs the device's 1 s housekeeping
tick — status LED, heating actuator, e-paper display, low-heap watchdog,
WiFi supervision, NTP, MQTT, internet-failure recovery — and there is
currently no way to tell from the field whether any of those steps is
getting slow. The task already computes a `workMs` value every iteration
and logs a `Tick slow work` line at DEBUG when it exceeds 500 ms, but
that signal is invisible unless a developer is watching the serial log
at the right moment. The Sensor Monitor task has carried a
`Support::Stats` for per-tick sleep duration since the cross-task race
fix landed; the Network task deserves the same observability so a single
`GET /api/about` call can show whether the housekeeping loop has been
keeping pace.

## What Changes

- Add a `Support::Stats` member to `Network` that records the per-iteration
  work duration (the existing `workMs` already computed at the bottom of
  each tick).
- Expose an indivisible `StatsSnapshot` accessor on `Network`, matching
  the pattern on `Task::SensorMonitor`.
- Extend the 15-minute diagnostics log on the Network task to include the
  four counters (count / avg / min / max) of the new stats alongside the
  existing heap and stack-HWM line.
- Extend `GET /api/about` to include the Network-loop counters under
  `stats.net_cycle_count`, `stats.net_avg_cycle_work_ms`,
  `stats.net_min_cycle_work_ms`, `stats.net_max_cycle_work_ms` — read
  through a single snapshot call.

No breaking changes. The new fields are additive on `/api/about`.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `networking`: add a requirement that the Network task accumulates
  per-iteration work-duration stats via `Support::Stats` and logs them in
  the periodic diagnostics block.
- `http-api`: extend `GET /api/about` to surface the Network-loop cycle
  counters (the existing `cycle_*` fields from the Sensor Monitor task
  remain unchanged).

## Impact

- `src/Network.h` — new `Support::Stats` member and
  `getStatsSnapshot()` accessor.
- `src/Network.cpp` — feed `stats.add(workMs)` each iteration; extend
  the 15-min diagnostics `ESP_LOGI` line to print the four counters.
- `src/routes/StatusRoutes.cpp` — read the snapshot under one accessor
  call and emit the four `net_*` keys into the existing `stats`
  sub-object of `/api/about`.
- No dependency, build, or hardware changes. The work already happens
  (the existing `workMs` and the existing 15-min diagnostics block); the
  change only feeds it into `Support::Stats` and surfaces the result.