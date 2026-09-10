## ADDED Requirements

### Requirement: `/api/about` exposes Network-loop work-duration stats

The `GET /api/about` endpoint SHALL include the Network task's
per-iteration work-duration counters under the existing `stats`
sub-object, alongside the existing Sensor Monitor cycle-delay counters.
The new keys SHALL be named `net_cycle_count`,
`net_avg_cycle_work_ms`, `net_min_cycle_work_ms`, and
`net_max_cycle_work_ms`, and SHALL be `uint64_t` integers. The values
SHALL be read through a single `Network::getStatsSnapshot()` call so
the four numbers describe one logical snapshot, matching the
indivisible-read contract already applied to the Sensor Monitor's
counters in the `system-architecture` spec.

#### Scenario: Network stats fields are present in the response

- **WHEN** a client GETs `/api/about` and the Network task has completed
  at least one iteration since boot
- **THEN** the parsed JSON contains a `stats.net_cycle_count` key whose
  value is a positive integer, `stats.net_avg_cycle_work_ms` whose value
  is the integer-millisecond average work duration, and
  `stats.net_min_cycle_work_ms` / `stats.net_max_cycle_work_ms` whose
  values bracket the recorded work durations

#### Scenario: Network stats fields are zero before the first iteration

- **WHEN** a client GETs `/api/about` before the Network task has
  completed its first iteration
- **THEN** each of `stats.net_cycle_count`, `stats.net_avg_cycle_work_ms`,
  `stats.net_min_cycle_work_ms`, and `stats.net_max_cycle_work_ms` is
  `0`, matching the zero-sample contract on `Support::Stats`

#### Scenario: Sensor Monitor cycle fields remain unchanged

- **WHEN** a client GETs `/api/about`
- **THEN** the existing `stats.cycle_count`, `stats.avg_cycle_delay`,
  `stats.min_cycle_delay`, and `stats.max_cycle_delay` keys are still
  present and unchanged in name, type, and meaning; the new `net_*`
  keys are additive and do not replace them