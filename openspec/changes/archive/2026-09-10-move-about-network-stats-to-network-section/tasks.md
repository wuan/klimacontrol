## 1. Move `net_*` keys from `stats` sub-object to Network info section

- [x] 1.1 In `src/routes/StatusRoutes.cpp`, in the `/api/about` lambda,
      delete the four `statsJson["net_cycle_count"] = ...`,
      `statsJson["net_avg_cycle_work_ms"] = ...`,
      `statsJson["net_min_cycle_work_ms"] = ...`, and
      `statsJson["net_max_cycle_work_ms"] = ...` assignments under the
      `stats` sub-object. Keep the `const Support::StatsSnapshot
      netStats = network.getStatsSnapshot();` line and its comment
      block where they are (alongside `cycleStats`), since the snapshot
      call still serves the keys.
- [x] 1.2 In the same file, in the `Network info` section of the
      `/api/about` lambda, add four top-level assignments using the
      `netStats` snapshot taken at the top of the handler:
      `doc["net_cycle_count"] = netStats.count;`,
      `doc["net_avg_cycle_work_ms"] = netStats.average;`,
      `doc["net_min_cycle_work_ms"] = netStats.min;`, and
      `doc["net_max_cycle_work_ms"] = netStats.max;`. Place them
      outside the `if (WiFiClass::status() == WL_CONNECTED) { ... } else
      if (WiFiClass::getMode() == WIFI_AP) { ... }` chain so they are
      emitted unconditionally.
- [x] 1.3 Add a brief comment on the unconditional emission block
      pointing at spec `http-api` → "`/api/about` exposes Network-loop
      work-duration stats" (the modified requirement from this change).

## 2. Validate

- [x] 2.1 Run `openspec validate --all --strict` from the repo root
      and confirm the modified delta passes
- [x] 2.2 Run `pio test -e native` and confirm the native test build
      still compiles (the change is in an `ARDUINO`-only handler and is
      not exercised by native tests, but the change must not break the
      host build)
- [x] 2.3 Run `pio run -e adafruit_qtpy_esp32s2` and confirm the
      firmware build succeeds