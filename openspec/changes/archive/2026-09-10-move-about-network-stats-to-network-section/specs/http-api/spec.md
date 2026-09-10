## MODIFIED Requirements

### Requirement: `/api/about` exposes Network-loop work-duration stats

The `GET /api/about` endpoint SHALL include the Network task's
per-iteration work-duration counters in the `Network info` section of
the JSON response (the same block that already carries `wifi_ssid`,
`wifi_rssi`, `wifi_tx_power`, `ip_address`, `mac_address` when WiFi is
connected, and `ap_ssid`, `ap_ip`, `ap_clients` when the device is in AP
mode). The new keys SHALL be top-level fields named `net_cycle_count`,
`net_avg_cycle_work_ms`, `net_min_cycle_work_ms`, and
`net_max_cycle_work_ms`, SHALL be `uint64_t` integers, and SHALL be
emitted unconditionally regardless of WiFi state. The values SHALL be
read through a single `Network::getStatsSnapshot()` call so the four
numbers describe one logical snapshot, matching the indivisible-read
contract already applied to the Sensor Monitor's counters in the
`system-architecture` spec.

The keys MUST NOT appear under the `stats` sub-object; that sub-object
is reserved for sensor-read statistics and the Sensor Monitor's
cycle-delay stats (`stats.cycle_*`).

#### Scenario: Network stats fields are present in the network section

- **WHEN** a client GETs `/api/about` and the Network task has completed
  at least one iteration since boot
- **THEN** the parsed JSON contains a top-level `net_cycle_count` key
  whose value is a positive integer, `net_avg_cycle_work_ms` whose value
  is the integer-millisecond average work duration, and
  `net_min_cycle_work_ms` / `net_max_cycle_work_ms` whose values bracket
  the recorded work durations. None of these four keys appear under a
  `stats` sub-object.

#### Scenario: Network stats fields are zero before the first iteration

- **WHEN** a client GETs `/api/about` before the Network task has
  completed its first iteration
- **THEN** each of the top-level `net_cycle_count`,
  `net_avg_cycle_work_ms`, `net_min_cycle_work_ms`, and
  `net_max_cycle_work_ms` keys is `0`, matching the zero-sample contract
  on `Support::Stats`

#### Scenario: Network stats appear in AP mode

- **WHEN** a client GETs `/api/about` while the device is in AP mode
  (i.e. `ap_ssid`, `ap_ip`, `ap_clients` are emitted instead of the
  `wifi_*` family)
- **THEN** the four `net_*` keys are still present at the top level of
  the response, alongside `ap_ssid` / `ap_ip` / `ap_clients`

#### Scenario: Sensor Monitor cycle fields remain unchanged

- **WHEN** a client GETs `/api/about`
- **THEN** the `stats.cycle_count`, `stats.avg_cycle_delay`,
  `stats.min_cycle_delay`, and `stats.max_cycle_delay` keys are still
  present and unchanged in name, type, meaning, and parent sub-object;
  the Network loop's `net_*` keys are top-level additions that do not
  replace them and do not duplicate them under `stats`