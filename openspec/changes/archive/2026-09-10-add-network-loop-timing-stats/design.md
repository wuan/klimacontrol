## Context

The Network task (`src/Network.cpp`) drives the firmware's 1 s
housekeeping tick. Each iteration already records its work duration
(`workMs = blockExit - now` at the bottom of the inner loop), and the
existing `Tick slow work` `ESP_LOGD` line at `workMs > 500 ms` is the
only signal that anything inside the tick is taking longer than it
should. There is no aggregate, no min/max/average, no way to read the
trend from `/api/about`. The Sensor Monitor task has carried a
`Support::Stats` for its per-tick sleep duration since the cross-task
race fix landed (`src/task/SensorMonitor.h:29`, surfaced at
`GET /api/about` via `sensorMonitor.getStatsSnapshot()`), and the same
pattern applies cleanly to the Network task: a `Support::Stats` member
on `Network`, fed with `workMs` once per iteration, surfaced through a
`getStatsSnapshot()` accessor, read by the AsyncTCP task under a single
snapshot acquisition.

`Support::Stats` already enforces the cross-task read discipline
(`std::atomic_flag` spinlock, `snapshot()` returns an indivisible
`StatsSnapshot`, per-field getters exist for same-task use — see
`src/support/Stats.{h,cpp}`). The new instance plugs straight into that
contract.

## Goals / Non-Goals

**Goals:**

- Make the Network task's per-iteration work duration observable in
  aggregate (count, average, min, max) the same way Sensor Monitor's
  sleep duration already is.
- Surface the aggregate on `GET /api/about` next to the existing
  `cycle_count` / `avg_cycle_delay` / `min_cycle_delay` / `max_cycle_delay`
  fields, with a `net_` prefix so clients can disambiguate the two.
- Log the aggregate in the existing 15-minute diagnostics `ESP_LOGI`
  block so a developer reading the serial log can see trend without
  scraping `/api/about`.

**Non-Goals:**

- No new HTTP endpoint, no new top-level field shape. The new counters
  go under the existing `stats` sub-object of `/api/about`.
- No change to the Sensor Monitor stats path.
- No change to the `Support::Stats` API itself; the new instance uses
  the existing snapshot accessor contract.
- No thresholds, no alerting, no auto-mitigation. Just measurement and
  log/API exposure — a developer / operator deciding "what does a slow
  Network tick look like" can read the field and form their own view.

## Decisions

### D1. Measure per-iteration `workMs`, not sleep duration

The Network task is interesting when it is doing too much (`workMs`
spike), not when it is sleeping too long (sleep is the desired
`1000 - workMs` budget). The existing `workMs` value at
`src/Network.cpp:991-996` is already the operationally meaningful
signal: the `ESP_LOGD` "Tick slow work" line and its `workMs > 500 ms`
threshold were specifically called out in code comments as the indicator
of "MQTT loop, NTP, mDNS ... blocking inside the block". Measuring sleep
instead would just confirm the task is sleeping — uninformative.

Naming: the fields on `/api/about` use `net_*` prefix and `*_work_ms`
suffix (`net_cycle_count`, `net_avg_cycle_work_ms`,
`net_min_cycle_work_ms`, `net_max_cycle_work_ms`). The `cycle_` half of
the name matches the existing `cycle_*` Sensor Monitor fields so a
client reading both can see they are the same shape of metric (count +
avg/min/max of a per-iteration duration); the `net_` prefix and the
`_work_ms` suffix disambiguate which task and which duration.

### D2. Reuse `Support::Stats`, no new primitive

The Sensor Monitor already provides the precedent. Reusing
`Support::Stats` keeps the snapshot discipline in one place
(`Stats::snapshot()` is the only cross-task-safe read path) and means the
new accessor on `Network` follows the exact same shape as
`SensorMonitor::getStatsSnapshot()` — both return
`Support::StatsSnapshot` and both are cheap (one spinlock acquisition
plus four `uint64_t` copies).

### D3. Log in the existing 15-minute diagnostics block

`Network::task()` already emits an `ESP_LOGI(TAG, "Diagnostics: ...")`
line every `DIAGNOSTICS_INTERVAL_MS = 900000` ms, and a second `ESP_LOGI`
for the task stack HWM right after. The four new counters slot into
the same line as `net_cycle_count=N avg=Xms min=Yms max=Zms` so a
single grep on `Diagnostics:` catches both. No new log cadence, no new
log channel, no new TAG.

### D4. Add a `getStatsSnapshot()` accessor on `Network`, not just a getter

Cross-task reads go through `Stats::snapshot()` (see the existing
`system-architecture` requirement). The new accessor on `Network`
mirrors `Task::SensorMonitor::getStatsSnapshot()` exactly — same name,
same return type, same cost. `StatusRoutes.cpp` calls it once per
request and feeds the four fields into the existing `stats` sub-object
of `/api/about`, alongside the existing Sensor Monitor cycle stats.

### D5. No change to the `Support::Stats` cross-task-read requirement

The existing requirement in `openspec/specs/system-architecture/spec.md`
("Cross-task reads of `Support::Stats` use a snapshot accessor") is
written generically: it specifies the *mechanism* (`std::atomic_flag`
spinlock, indivisible `StatsSnapshot`), not a specific instance. The new
`Network` instance plugs into the same mechanism without needing a
spec delta on `system-architecture`.

## Risks / Trade-offs

- **`workMs` underestimates true work by up to 1 ms.** `blockExit =
millis()` reads the timer once per iteration; between the bottom of
the inner loop and `workMs = blockExit - now`, the inner work itself is
already complete. The measurement is of "time from start of iteration
to first instruction after the slow-log check", which is exactly the
window the existing DEBUG log watches. Acceptable.

- **Stats include the OTA-suspended ticks.** During an OTA the Network
task skips most of its work (MQTT, NTP, display, etc.) and `workMs`
collapses to near zero. A reader of `/api/about` during a long OTA
would see a misleadingly low average. Documented as known behaviour;
not fixed because the average across the whole uptime is more useful
than a per-window average, and an OTA skews the count downward (cheap
ticks), not upward.

- **`count` grows unbounded for the lifetime of the device.** With a
1 s tick and an MTBF measured in years, the `uint64_t` counter wraps
after ~585 billion years. Not a real concern; flagged for completeness
because the type was chosen deliberately.

- **Field naming inconsistency with the Sensor Monitor fields.** The
Sensor Monitor fields are `cycle_count` / `avg_cycle_delay` (suffix
`_delay`); the Network fields are `net_cycle_count` /
`net_avg_cycle_work_ms` (suffix `_work_ms`). The asymmetry is
intentional — the two tasks measure different things (sleep vs. work)
and a common suffix would be misleading — but a reader has to know the
difference. The 15-min log line calls them out the same way.

## Migration Plan

No data migration, no schema migration, no version bump. The change is
additive on `/api/about` (new keys under the existing `stats`
sub-object), additive in the log (extra fields on an existing line),
and additive on `Network` (new member + new method). Existing clients
that don't read the new keys are unaffected.

Rollback: revert the three commits (or the single squashed commit).

## Open Questions

None. The metric choice (`workMs` over `sleepMs`), the accessor shape
(`getStatsSnapshot()` mirroring `SensorMonitor`), the field naming
(`net_*` with `_work_ms` suffix), and the surface (`stats` sub-object
of `/api/about`) all follow established patterns or were resolved
during scoping.