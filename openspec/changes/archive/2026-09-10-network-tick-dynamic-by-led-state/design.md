## Context

The change `network-loop-adaptive-sleep` introduced a constant
`TICK_MS = 15000` for the Network task's adaptive sleep. The change
`add-network-loop-timing-stats` made `workMs` observable. This change
makes the tick itself dynamic: 1 s while the LED is actively emitting,
15 s once dark mode has suppressed it.

The signal is `DarkModeStatusLed::isDark(uint32_t nowMs)` — a method
that already exists, returns `true` exactly when dark mode has
actively engaged (the ON/TRANSMIT_DATA flash is held dark and the
NeoPixel rail is cut), and is called from the Network task anyway as
part of `statusLed.update(now)` once per iteration. The new
`tickMs` computation simply queries this state at the top of each
iteration and feeds it into the existing adaptive-sleep formula.

The `statusLed` member on `Network` (`src/Network.h:73`) is a
`DarkModeStatusLed &` already wired in at construction. No new
dependency, no new method, no new field.

## Goals / Non-Goals

**Goals:**

- Compute `tickMs` at the top of each iteration as
  `statusLed.isDark(now) ? 15000 : 1000`, replacing the
  `static constexpr uint32_t TICK_MS = 15000` introduced by
  `network-loop-adaptive-sleep`.
- Keep the rest of the adaptive-sleep formula
  (`sleepMs = (lastWorkMs < tickMs) ? (tickMs - lastWorkMs +
  WAKE_MARGIN_MS) : 1u`) unchanged.
- Keep `WAKE_MARGIN_MS = 2` as a constant.

**Non-Goals:**

- No change to the slow-log DEBUG threshold (`workMs > 500` stays as
  is — documented trade-off below).
- No change to the LED's dark-mode configuration, the `darkAfterMs`
  threshold, or `DarkModeStatusLed` itself.
- No new logging, no new API fields, no new HTTP endpoint. The
  existing 15-min diagnostics line carries the `net_*` counters
  which already reflect the cadence implicitly.

## Decisions

### D1. Compute `tickMs` from `statusLed.isDark(millis())` at the top of each iteration

The tick needs to reflect the LED's state at the time the sleep is
computed. The cleanest way is to query `isDark()` at the very top of
the iteration, before the sleep, using the monotonic clock the loop
already depends on (`millis()`). The LED state has already been
updated by the previous iteration's `statusLed.update(now)` call at
the bottom of that iteration's work, so `isDark()` returns the
up-to-date value.

Alternative considered: track a `bool isLedDark` member updated by
`statusLed.update()`'s side effects. Rejected because it would
require either extending `DarkModeStatusLed` with a callback or
adding a second state-tracking variable on `Network`. The direct
`isDark()` query is one atomic load (the underlying
`darkAnchorArmed`/`suppressed` state is local to `DarkModeStatusLed`)
and avoids any synchronization concerns.

### D2. `WAKE_MARGIN_MS` stays a constant

`WAKE_MARGIN_MS = 2` is a small constant whose only purpose is to
defeat an early RTOS wake. It does not need to scale with `tickMs`:
2 ms is 0.2 % of a 1 s budget and 0.013 % of a 15 s budget, neither
of which is meaningful.

### D3. Slow-log threshold stays at `workMs > 500`

The slow-log DEBUG line (`ESP_LOGD(TAG, "Tick slow work: work=%lums ...")
fires when an iteration's work exceeds 500 ms. At `tickMs == 1000`
this is "more than half the budget" — the originally intended signal
of "something blocking inside the block". At `tickMs == 15000` it
is `1/30` of the budget and will fire on healthy iterations
(rendering a refresh, MQTT connect, NTP forceUpdate can each exceed
500 ms on a fresh 15 s tick).

This is an accepted trade-off:
- The log line is `ESP_LOGD`, off in production builds by default.
- In development builds it surfaces slow iterations during the dark
  phase, which is information, not noise — a developer profiling the
  firmware wants to see "the dark-mode tick ran a 600 ms MQTT
  reconnect, so the next tick is only 14.4 s away".
- The threshold is not adjusted per-tick because the rule of thumb
  "more than half the budget" no longer applies once the budget is
  dynamic, and `tickMs / 2` would be 7500 ms at dark — so high that
  real stalls (a 5 s MQTT block) would slip past it.

Documented as a known limitation; not addressed by this change. A
follow-up could introduce a separate "blocking" threshold that
ignores the tick entirely, but that is a separate decision.

### D4. One iteration of cadence lag at LED transitions

When the LED transitions into or out of dark mode, the change in
`tickMs` takes effect on the *next* iteration's sleep, not the
current one. The current iteration's sleep was already computed
under the old tick.

This is acceptable because:
- The LED transition itself happens on a much slower timescale
  (dark mode kicks in after `darkAfterMs` of stable ON; clearing it
  requires a state change to OFF/STARTUP/ERROR).
- One iteration of cadence lag (1 s or 15 s) is invisible to the
  user; the LED's transition is what they observe, not the network
  loop's.

## Risks / Trade-offs

- **Slow-log noise at 15 s tick.** As discussed in D3. Acceptable;
  flagged.
- **No transition log.** A developer reading the serial log during a
  dark-mode transition would not see "tick changed from 1000 to
  15000" unless they correlated the `statusLed` debug output with
  the `workMs` measurements. Acceptable: the 15-min diagnostics
  line reports the average workMs over the window, which already
  shifts in response to the cadence change.
- **`isDark()` is called from outside `DarkModeStatusLed::update()`.**
  `isDark(nowMs)` is a pure query — it reads the current
  `darkAnchorArmed` / `suppressed` state and compares `nowMs`
  against the configured `darkAfterMs` threshold. No side effects,
  no shared mutable state with `update()`. Safe to call as often as
  we like; one call per iteration is trivial.

## Migration Plan

None. Single-file change inside `Network::task()`. No data migration,
no schema migration, no version bump. The change is additive on the
spec (new behavior on top of the existing adaptive sleep) and
transparent to all consumers.

Rollback: revert the single commit.

## Open Questions

None. The signal (`isDark()`), the values (1 s / 15 s), the
evaluation point (top of iteration), and the threshold
non-adjustment are all decided.