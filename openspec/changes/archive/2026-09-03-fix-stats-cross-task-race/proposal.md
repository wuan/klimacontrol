# Cross-task reads of `Support::Stats` are torn

## Why

`Support::Stats` is owned by `Task::SensorMonitor` and written once per tick
from the Sensor Monitor task (`task/SensorMonitor.cpp:85`). The same object
is read four separate times from the AsyncTCP task inside the `GET /api/about`
handler (`routes/StatusRoutes.cpp:97-101`) — once for `cycle_count`, once for
`avg_cycle_delay`, once for `min_cycle_delay`, and once for `max_cycle_delay`.

Every field is `uint64_t`. On the Xtensa ESP32-S2, an unaligned or
multi-bus-cycle 64-bit read can tear even for fields that happen to be
4-byte-aligned in the struct. `min_value` / `max_value` are additionally
updated by a non-atomic compare-load-store (`if (v < min_value) min_value = v`)
that can expose either the old *or* the new value to a concurrent read —
not a corrupted one, but enough to misreport "the slowest tick in the last
five minutes" by a wide margin.

The DeviceConfig fix in `2026-09-03-fix-device-config-cross-task-race`
established the project pattern for this race: a `std::atomic_flag`
spinlock and a `snapshot()` accessor that returns an indivisible copy.
`Support::Stats` is a smaller, simpler version of the same problem.

## What Changes

- `src/support/Stats.{h,cpp}` — add a `std::atomic_flag` spinlock and a
  `snapshot()` accessor that returns an indivisible `StatsSnapshot` (the
  four fields in one struct copy). `add()` takes the lock for the full
  mutation. Existing single-value getters remain and acquire the lock
  for one load each, so the API keeps working for test code that uses them.
- `src/task/SensorMonitor.h` — add `getStatsSnapshot()` next to the
  existing `getStats()` reference accessor (used by same-task callers).
- `src/routes/StatusRoutes.cpp` — replace the four `cycleStats.get_*()`
  calls with one `snapshot()` read into four locals.
- A native multi-threaded test driving concurrent `add()` and
  `snapshot()`, asserting that every snapshot is internally consistent
  (sample N rows back, sum equals total ± N).
- Spec requirement added to `system-architecture` covering the Stats
  share between the Sensor Monitor task and the AsyncTCP handler.

### Non-goals

- Reworking `Support::Stats` into `std::atomic<uint64_t>` per field.
  Splitting the four fields into four atomics gives
  four-field-internal inconsistency at observation time; one snapshot
  accessor over a spinlock gives whole-struct consistency.
- A general "every cross-task shared object needs a lock" sweep.
  Stats, DeviceConfig, the gains-pending flag, and the OTA `Activity`
  state are the project's four known cases today and each is being (or
  has been) addressed individually.
- Persisting `Stats` across reboots. The object resets on power cycle
  the same way it does today.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `system-architecture`: add a requirement that cross-task readers of
  `Support::Stats` use the snapshot accessor — same shape as the existing
  "Thread safety for shared sensor data" requirement that this change
  complements.

## Impact

- **Source**: `src/support/Stats.{h,cpp}` (lock + snapshot accessor),
  `src/task/SensorMonitor.h` (snapshot getter on top of the existing
  reference), `src/routes/StatusRoutes.cpp` (four calls → one snapshot).
- **Tests**: a new `test/test_stats_snapshot/` native suite, plus
  `+<test/test_stats_snapshot/>` added to the `native`
  `build_src_filter` in `platformio.ini`.
- **No web, no NVS schema, no hardware, no observable ABI change.**
  The existing `get_min()` / `get_max()` / `get_average()` / `get_count()`
  callers continue to work, both via the lock.
- **Not blocked.** Independent of every other pending change.
