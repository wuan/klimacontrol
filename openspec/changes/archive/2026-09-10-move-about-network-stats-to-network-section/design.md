## Context

The change `add-network-loop-timing-stats` added four
`stats.net_cycle_count` / `stats.net_avg_cycle_work_ms` /
`stats.net_min_cycle_work_ms` / `stats.net_max_cycle_work_ms` keys to
the `stats` sub-object of `/api/about`. That placement groups the
Network-loop stats with the Sensor Monitor's `stats.cycle_*`
sensor-read-delay stats — which is wrong conceptually: the Sensor
Monitor stats describe the sensor read loop, while the Network-loop
stats describe the network housekeeping loop. The `Network info`
section of the same JSON response (the block carrying `wifi_ssid`,
`wifi_rssi`, `wifi_tx_power`, `ip_address`, `mac_address`, and in AP
mode `ap_ssid` / `ap_ip` / `ap_clients`) is the right home.

The change is a single-file edit: relocate four `JsonObject`
assignments inside `WebServerManager::setupStatusRoutes()`'s
`/api/about` lambda. The snapshot call (`network.getStatsSnapshot()`)
stays where it is — it already pairs cleanly with the Sensor Monitor's
`sensorMonitor.getStatsSnapshot()` next to it, and the cross-task
read discipline only requires one acquisition per handler call.

## Goals / Non-Goals

**Goals:**

- Move the four `net_*` keys from the `stats` sub-object of `/api/about`
  to top-level keys in the existing `Network info` block.
- Keep the key names (`net_cycle_count`, `net_avg_cycle_work_ms`,
  `net_min_cycle_work_ms`, `net_max_cycle_work_ms`) and types
  (`uint64_t`) unchanged — only the parent in the JSON object moves.
- Emit the four keys unconditionally (whether WiFi is connected, in AP
  mode, or transitioning), so a client checking network loop health
  during a transition still gets the counters.

**Non-Goals:**

- No change to the snapshot call site, no change to `Network.h` /
  `Network.cpp`, no change to `Support::Stats`.
- No change to the Sensor Monitor's `stats.cycle_*` keys — they stay
  in the `stats` sub-object.
- No new endpoints, no schema migration tooling, no client-side
  compatibility shim. The change is a breaking API change (clients
  reading `stats.net_*` will need to update) and the breaking nature
  is documented in the proposal.

## Decisions

### D1. Top-level keys, not a `network` sub-object

The existing `Network info` section already emits top-level keys
(`wifi_ssid`, `wifi_rssi`, `wifi_tx_power`, `ip_address`,
`mac_address`, `ap_ssid`, `ap_ip`, `ap_clients`) directly on the
top-level `JsonDocument` rather than under a `network` sub-object.
Adding `net_cycle_count` etc. as top-level keys matches that pattern
exactly. Wrapping the network info in a `network` sub-object would
require also moving `wifi_*` / `ip_address` / `ap_*`, which is a much
larger change than the user asked for and a strict superset of this
one.

### D2. Keep the snapshot call where it is

The `const Support::StatsSnapshot netStats = network.getStatsSnapshot();`
call sits next to `const Support::StatsSnapshot cycleStats =
sensorMonitor.getStatsSnapshot();` near the top of the handler. Both
snapshots are taken in the same handler invocation, which is the
correct cross-task read discipline: each `Support::Stats::snapshot()`
acquires its own spinlock, but they are independent counters, so
nothing reads them as a pair. Moving the Network snapshot acquisition
into the `Network info` block below would not improve anything and
would scatter cross-task read call sites across the handler.

### D3. Unconditional emission of the `net_*` keys

The `Network info` block currently gates `wifi_*` on
`WL_CONNECTED` and `ap_*` on `WIFI_AP`. The four `net_*` keys describe
the Network task, which is always running — they are meaningful in
every WiFi state, including during a transition where neither branch
of the existing `if`/`else if` fires. Putting them outside the
`if (WiFiClass::status() == WL_CONNECTED) { ... } else if (... WIFI_AP)
{ ... }` chain so they are emitted every time a client GETs
`/api/about`. This matches the spec scenario for AP mode.

## Risks / Trade-offs

- **Breaking change for clients reading `stats.net_*`.** Any client
  that read `stats.net_cycle_count` from `/api/about` (added in the
  just-shipped `add-network-loop-timing-stats` change) will now find
  the field absent and need to update to top-level `net_cycle_count`.
  Mitigated by the change being small and by the fact that the
  previous change has not yet shipped to a release (`add-network-loop-timing-stats`
  is a pending change, not yet archived) — the only clients that
  could possibly be reading `stats.net_*` are the firmware's own code,
  which is updated in the same commit.
- **Frontend (`data/*.html`) is not updated.** The about page on the
  web UI parses `/api/about` to populate its fields; if it ever
  read `stats.net_*` it would silently break. A quick grep would be
  prudent before the next device release to make sure no JS template
  was relying on the `stats.net_*` path. Out of scope for this
  change (the user asked specifically about the API shape, not the
  frontend), but flagged for follow-up.
- **No migration path.** Unlike a deprecation, the keys are moved, not
  duplicated. Old clients simply stop receiving the data. Acceptable
  because the previous change is unreleased.

## Migration Plan

None beyond the breaking change note. The change is a single-file edit
inside the `/api/about` lambda in `src/routes/StatusRoutes.cpp`. After
this commit the four `net_*` keys are top-level instead of
`stats.net_*`. No data migration, no version bump, no client-side
shim.

Rollback: revert the single commit.

## Open Questions

None. The placement (top-level alongside `wifi_*` / `ap_*`), the key
names (unchanged), and the snapshot acquisition site (unchanged) all
follow established patterns or are dictated by the user's request.