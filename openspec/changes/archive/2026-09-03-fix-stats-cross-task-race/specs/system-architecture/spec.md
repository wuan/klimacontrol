## ADDED Requirements

### Requirement: Cross-task reads of `Support::Stats` use a snapshot accessor

`Support::Stats` exposes four `uint64_t` cycle-delay counters at `GET /api/about` (`cycle_count`, `avg_cycle_delay`, `min_cycle_delay`, `max_cycle_delay`) that are written once per loop iteration from the Sensor Monitor task and read from the AsyncTCP task. Cross-task readers SHALL observe those counters through a single `Support::Stats::snapshot()` accessor that returns an indivisible copy of all four fields under a spinlock, so a single handler call observes one coherent set of numbers rather than four independent ones. Same-task readers MAY continue to use the per-field getters. The spinlock SHALL be a `std::atomic_flag` initialised with `ATOMIC_FLAG_INIT`, matching the existing `deviceConfigLock` / `restartLock` discipline.

#### Scenario: AsyncTCP handler reads four consistent counters per request

- **WHEN** the `GET /api/about` handler running on the AsyncTCP task
  observes cycle-delay stats while the Sensor Monitor task is in the
  middle of a `stats.add(duration)` call
- **THEN** every one of `cycle_count`, `avg_cycle_delay`,
  `min_cycle_delay`, and `max_cycle_delay` describes the same logical
  snapshot, and the JSON response carries no torn combination of
  pre-update and post-update field values

#### Scenario: Lock-free native build still links

- **WHEN** `Support::Stats` is exercised from the `native` test
  environment without the ESP-IDF FreeRTOS port
- **THEN** the snapshot accessor and the lock compile and run
  identically to the firmware build, because the synchronization
  primitive is `std::atomic_flag` rather than a FreeRTOS-only API

#### Scenario: Min and max are consistent within one snapshot

- **WHEN** a reader takes a single snapshot while a writer has just
  inserted a value `v`
- **THEN** `snapshot.min <= v <= snapshot.max`, and `snapshot.min` and
  `snapshot.max` are both drawn from the same set of inserted values
  (no value outside the inserted range can appear, no value below the
  running minimum can appear in either field)
