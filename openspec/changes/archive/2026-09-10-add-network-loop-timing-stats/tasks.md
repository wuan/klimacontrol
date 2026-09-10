## 1. Add stats member and accessor to `Network`

- [x] 1.1 In `src/Network.h`, add `#include "support/Stats.h"` and a
      `Support::Stats stats;` member alongside the other private members
- [x] 1.2 In `src/Network.h`, add a `Support::StatsSnapshot
      getStatsSnapshot() const { return stats.snapshot(); }` public
      accessor next to the existing public getters, with a comment
      pointing at the cross-task snapshot discipline in
      `openspec/specs/system-architecture/spec.md` (mirror the wording
      on `Task::SensorMonitor::getStatsSnapshot()`)

## 2. Feed and log the Network-loop stats

- [x] 2.1 In `src/Network.cpp`, just after the existing
      `unsigned long workMs = blockExit - now;` at the bottom of the
      Network task's inner loop, add `stats.add(workMs);` so each
      iteration records its work duration
- [x] 2.2 In the same file, extend the existing 15-minute diagnostics
      `ESP_LOGI(TAG, "Diagnostics: ...")` line so it also includes
      `net_cycle_count=<N> net_avg_cycle_work_ms=<A> net_min_cycle_work_ms=<min> net_max_cycle_work_ms=<max>`
      populated from a single `stats.snapshot()` call taken on the
      network task (same-task read, per-field getters are fine here
      per the system-architecture spec — but prefer one snapshot for
      consistency with the cross-task contract)

## 3. Surface the Network-loop stats at `GET /api/about`

- [x] 3.1 In `src/routes/StatusRoutes.cpp`, extend the existing `stats`
      sub-object of the `/api/about` handler with four new keys:
      `net_cycle_count`, `net_avg_cycle_work_ms`,
      `net_min_cycle_work_ms`, `net_max_cycle_work_ms`, populated from a
      single `const Support::StatsSnapshot netStats =
      network.getStatsSnapshot();` call — matching the pattern already
      used for the Sensor Monitor's `cycleStats` snapshot above

## 4. Validate

- [x] 4.1 Run `openspec validate --all --strict` from the repo root and
      confirm the new delta specs pass
- [x] 4.2 Run `pio test -e native` and confirm the native test build
      still compiles and tests pass (no native coverage for the new
      feature itself — it is device-only — but the change must not
      break the host build)
- [x] 4.3 Run `pio run -e adafruit_qtpy_esp32s2` and confirm the
      firmware build succeeds