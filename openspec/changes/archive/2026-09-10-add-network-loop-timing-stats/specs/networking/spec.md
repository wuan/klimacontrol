## ADDED Requirements

### Requirement: Network loop accumulates per-iteration work-duration stats

The Network task SHALL maintain a `Support::Stats` instance that records
the duration of each tick's work (the time the task spends executing
its 1 s housekeeping iteration, from the start of the tick to the
moment the existing `workMs` value is read at the bottom of the loop).
The instance SHALL be fed once per iteration with the integer number of
milliseconds the iteration's work consumed. A freshly started task
SHALL report zero in every counter (count, average, min, max) until the
first iteration completes.

#### Scenario: First iteration records a non-zero count

- **WHEN** the Network task has completed its first 1 s iteration after boot
- **THEN** `Network::getStatsSnapshot().count` SHALL be `1` and
  `Network::getStatsSnapshot().average` SHALL equal the value recorded
  for that first iteration

#### Scenario: Counter accumulates across iterations

- **WHEN** the Network task has completed N iterations since boot
- **THEN** `Network::getStatsSnapshot().count` SHALL be `N`, the
  `min` and `max` SHALL bracket the N recorded values, and the
  `average` SHALL equal `total / N`

#### Scenario: Newly constructed Network reports zeros

- **WHEN** `Network::getStatsSnapshot()` is called before the task's
  first iteration completes
- **THEN** every field (count, average, min, max) SHALL be `0`, matching
  the zero-sample contract on `Support::Stats::snapshot()`

#### Scenario: Stats feed uses integer milliseconds

- **WHEN** an iteration's recorded work duration is W milliseconds
- **THEN** the value passed to `stats.add(...)` SHALL be the integer W
  (no fractional milliseconds, no scaling)

### Requirement: Network loop stats are surfaced in periodic diagnostics

The Network task SHALL include the four counters of its per-iteration
work-duration `Support::Stats` instance in the existing periodic
diagnostics log line that runs every 15 minutes. The counters SHALL
appear in a single `ESP_LOGI` line alongside the existing heap and
stack-HWM fields, formatted so a developer can read the trend from the
serial log without scraping `/api/about`.

#### Scenario: 15-minute diagnostics line contains the stats

- **WHEN** the Network task's 15-minute diagnostics timer fires and
  the task has completed at least one iteration
- **THEN** the diagnostics `ESP_LOGI` line SHALL contain
  `net_cycle_count=<N>`, `net_avg_cycle_work_ms=<A>`,
  `net_min_cycle_work_ms=<min>`, and `net_max_cycle_work_ms=<max>`
  reflecting the current state of the `Support::Stats` instance