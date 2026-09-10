## Context

The Network task (`src/Network.cpp`) currently sleeps for a fixed
`vTaskDelay(1000 / portTICK_PERIOD_MS)` at the top of each iteration
of its `while (true)` loop. The work that follows varies from a few ms
on a healthy link to hundreds of ms when MQTT stalls or NTP times out,
and the fixed 1 s sleep adds on top of that. The Sensor Monitor task
already implements the adaptive equivalent at `src/task/SensorMonitor.cpp:98-107`,
specified at `openspec/specs/sensor-management/spec.md:336`.

The change set `add-network-loop-timing-stats` (just shipped) made the
Network task's per-iteration work duration observable via a
`Support::Stats` member and `GET /api/about` (`net_avg_cycle_work_ms`,
etc.). The same `workMs` value is the input to the new adaptive sleep.

## Goals / Non-Goals

**Goals:**

- Replace the fixed `vTaskDelay(1000)` at the top of the Network task
  with `vTaskDelay(pdMS_TO_TICKS(sleepMs))` where `sleepMs` is computed
  from the previous iteration's `workMs`.
- Mirror the Sensor Monitor's `WAKE_MARGIN_MS = 2` and
  `tick - elapsed + WAKE_MARGIN_MS` formula exactly so both tasks
  follow the same long-run cadence contract.

**Non-Goals:**

- No change to the existing 1 s target cadence (the `+ WAKE_MARGIN_MS`
  preserves it on average).
- No change to the 15-minute diagnostics cadence, the MQTT publish
  cadence, the NTP refresh cadence, or any other period inside the
  loop — those are gated by their own `now - lastX >= Y` checks and
  are unaffected by a tighter base tick.
- No change to `Support::Stats`, `lastWorkMs` is just an internal
  `uint32_t` carrier across iterations.

## Decisions

### D1. `TICK_MS` and `WAKE_MARGIN_MS` as `static constexpr` inside the task body

The Network task is defined inside `Network::task()` in
`src/Network.cpp`. Both constants are file-internal, used only inside
that function, and the existing `MIN_FREE_INTERNAL_BYTES` /
`DIAGNOSTICS_INTERVAL_MS` / `NTP_UNSYNCED_RETRY_MS` constants in the
same body are already declared `static constexpr` in the function body
for exactly this reason. Following the existing pattern keeps the
constants local to where they are used, avoids polluting `Network.h`'s
public interface, and makes the values visible right next to the only
caller.

### D2. `lastWorkMs` as a `uint32_t` member of `Network`

The variable has to outlive one iteration of the `while (true)` loop.
A `static` inside `task()` would work but is harder to test and harder
to read across the iteration boundary in a long function body. A
private member on `Network` (initialised to `0` so the first iteration
sleeps `TICK_MS + WAKE_MARGIN_MS = 1002` ms, matching the spec
scenario) makes the variable's lifetime obvious and matches the
placement of other Network-task-local state (`lastActuatorTickMs`,
`lastSecond`, `lastBlockExitMs`).

### D3. Compute `sleepMs` at the top of the loop, store `workMs` at the bottom

This matches the Sensor Monitor's reading order (work first, then
sleep), but the Network loop's existing structure puts the sleep at
the top and the work after, so the roles reverse: the sleep at the
top of iteration N uses `lastWorkMs` measured at the bottom of
iteration N-1, and at the bottom of iteration N the code assigns
`lastWorkMs = workMs` for iteration N+1's sleep. The dependency is
strictly between consecutive iterations; there is no first-iteration
bootstrap problem because `lastWorkMs = 0` produces
`TICK_MS + WAKE_MARGIN_MS = 1002` ms, exactly what the spec wants on
the first call.

### D4. No change to the `stats.add(workMs)` call

The new `lastWorkMs` member is the same integer-millisecond value
already computed as `workMs` and already passed to `stats.add(workMs)`.
The two share one variable name's value at the bottom of the iteration
and diverge at the top of the next: `stats.add(...)` accumulates into
the `Support::Stats` accumulator, `lastWorkMs = ...` carries one
integer across the iteration boundary. No double work, no extra
measurement.

## Risks / Trade-offs

- **`vTaskDelay` rounding.** `pdMS_TO_TICKS(x)` on ESP32-S2 with
  `portTICK_PERIOD_MS = 1` is a no-op, so `vTaskDelay(pdMS_TO_TICKS(N))`
  is exactly `vTaskDelay(N)`. The actual wake is "anywhere in
  `(N-1, N]` ms" because `vTaskDelay` returns at a tick boundary —
  the `WAKE_MARGIN_MS = 2` accounts for this exactly the same way the
  Sensor Monitor does (`src/task/SensorMonitor.h:43-50`).

- **Long-running iterations get the 1 ms floor, not zero sleep.** If a
  tick takes longer than 1 s the next sleep is `1` ms, not `0`. This
  matches the Sensor Monitor's contract (`src/task/SensorMonitor.cpp:103`)
  and yields to RTOS scheduling instead of busy-looping, which is the
  correct behaviour on a single-core ESP32-S2 where this task shares
  the core with SensorMonitor.

- **No impact on the existing `wait` time field.** The DEBUG slow-log
  line at `src/Network.cpp:991-996` reports `waitMs` (now minus
  `lastBlockExitMs`) and `workMs`. With the new sleep, `waitMs` is
  slightly shorter on fast ticks (`1000 - workMs - 2` instead of
  `1000`), but the field's semantics ("time spent waiting between
  iterations") are unchanged and the threshold that triggers the log
  (`workMs > 500`) is also unchanged.

## Migration Plan

No data migration, no schema migration, no version bump. The change is
internal to `src/Network.cpp` (and one `lastWorkMs` member on
`Network`). The externally visible behaviour (loop cadence, log lines,
API fields) is fully covered by the existing diagnostics and
`/api/about` stats added in `add-network-loop-timing-stats`.

Rollback: revert the single commit (one file changed, one header
member added).

## Open Questions

None. The pattern is already established in `Task::SensorMonitor`,
the constants are picked, and the storage location is decided.