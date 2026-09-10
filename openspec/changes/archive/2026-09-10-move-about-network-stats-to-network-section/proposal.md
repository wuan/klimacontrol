## Why

`/api/about` currently exposes the Network task's per-iteration
work-duration stats under `stats.net_cycle_count` / `stats.net_avg_cycle_work_ms` /
`stats.net_min_cycle_work_ms` / `stats.net_max_cycle_work_ms`. The
Sensor Monitor's cycle-delay stats (`stats.cycle_*`) describe the sensor
read loop, which fits semantically under `stats`; the Network task's
work-duration stats describe the network housekeeping loop, which fits
better under the `Network info` section of the response (the same block
that already carries `wifi_ssid`, `wifi_rssi`, `ip_address`, etc.). A
client grouping everything-network into one object should not have to
re-merge fields from a separate `stats` sub-object.

## What Changes

- Move the four `net_*` keys from the `stats` sub-object of `/api/about`
  to top-level keys in the existing `Network info` section. Rename the
  keys for consistency with the existing top-level `wifi_*` /
  `ip_address` naming and the inline-JSON-file convention used in
  `data/*.html`:
  - `net_cycle_count` → `net_cycle_count` (unchanged)
  - `net_avg_cycle_work_ms` → `net_avg_cycle_work_ms` (unchanged)
  - `net_min_cycle_work_ms` → `net_min_cycle_work_ms` (unchanged)
  - `net_max_cycle_work_ms` → `net_max_cycle_work_ms` (unchanged)
- The single `network.getStatsSnapshot()` call that feeds these keys
  stays where it is (next to `sensorMonitor.getStatsSnapshot()` near
  the top of the handler, so both snapshots are taken under the same
  `Support::Stats::snapshot()` discipline); only the JSON emission
  sites change.
- The four keys are emitted unconditionally (whether WiFi is connected,
  in AP mode, or transitioning) so a client inspecting network health
  can read the loop stats in any state — matching the pattern used for
  `wifi_connected` and `ap_*` fields.

**BREAKING**: clients reading `stats.net_cycle_count` (or any of the
other three `stats.net_*` keys) from `/api/about` will no longer find
them. They have moved to top-level `net_*` keys alongside the existing
network-info fields.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `http-api`: modify the requirement added in change
  `add-network-loop-timing-stats` ("`/api/about` exposes Network-loop
  work-duration stats") so the four `net_*` keys live in the `Network
  info` section of the JSON response instead of the `stats`
  sub-object.

## Impact

- `src/routes/StatusRoutes.cpp` — relocate the four
  `statsJson["net_*"] = ...` assignments into the `Network info`
  block, where `wifi_ssid` / `wifi_rssi` / `ip_address` /
  `ap_ssid` / `ap_clients` are already emitted.
- The `getStatsSnapshot()` call site stays unchanged. Only the JSON
  emission sites move.
- No firmware build impact, no dependency change, no native-test
  impact (the route handler is `ARDUINO`-only and is not exercised by
  `pio test -e native`).