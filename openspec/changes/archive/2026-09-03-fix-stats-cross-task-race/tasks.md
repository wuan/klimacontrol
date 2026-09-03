# Tasks: cross-task `Support::Stats` reads are torn

Ordered so each step is independently testable in `native` and the change
is useful after section 3 (lock + snapshot, the heart of the fix). Sections
4 and 5 migrate the call site and add the regression test.

## 1. Spec scaffolding

- [x] 1.1 `openspec validate fix-stats-cross-task-race --strict` returns
      clean (artifacts already exist)
- [ ] 1.2 Re-read `openspec/changes/fix-stats-cross-task-race/proposal.md`
      and `design.md` before touching code, so the decisions are fresh

## 2. Lock on `Support::Stats`

- [x] 2.1 In `src/support/Stats.h`, add
      `mutable std::atomic_flag lock = ATOMIC_FLAG_INIT;` to the `Stats`
      class. `mutable` because `snapshot()` and `get_*()` are const, and
      a spinlock taken to read and released on the way out is the
      textbook idiom. Cite `deviceConfigLock` in the comment
- [x] 2.2 Add private `void lockStats() const` /
      `void unlockStats() const` next to the field, with the same
      `test_and_set(acquire)` / `clear(release)` shape used by
      `deviceConfigLock`

## 3. Snapshot accessor on `Support::Stats`

- [x] 3.1 Add a new POD struct
      `struct StatsSnapshot { uint64_t count; uint64_t average; uint64_t min; uint64_t max; };`
      above the `Stats` class in `Stats.h`
- [x] 3.2 Add a public
      `StatsSnapshot snapshot() const` that takes the lock, reads all
      four fields once, releases, returns by value (NRVO). With zero
      samples, return `{0, 0, 0, 0}` to match the current contract of
      `get_min()` / `get_max()` / `get_average()`
- [x] 3.3 In `src/support/Stats.cpp`, change `add(uint64_t value)` to
      acquire the lock at entry, mutate all four fields under it, release
      on exit. The four-field mutation has to be atomic w.r.t. readers
- [x] 3.4 In `src/support/Stats.cpp`, wrap the existing
      `get_average()` / `get_min()` / `get_max()` / `get_count()` bodies
      in `lockStats()` / `unlockStats()` so single-field reads from
      same-task callers are also consistent against an in-flight `add()`
      (these methods are still exposed to the existing `test_support`
      suite, which continues to use them)

## 4. Migrate `GET /api/about`

- [x] 4.1 In `src/task/SensorMonitor.h`, add a public
      `Support::StatsSnapshot getStatsSnapshot() const` next to the
      existing `getStats()`. `getStats()` stays — it is used
      same-task only (no current call site is cross-task via the
      reference, but the type doesn't change)
- [x] 4.2 In `src/routes/StatusRoutes.cpp:97-101`, replace
      `const auto& cycleStats = sensorMonitor.getStats();` and the four
      `cycleStats.get_*()` calls with one
      `const Support::StatsSnapshot cycleStats = sensorMonitor.getStatsSnapshot();`
      then four locals `count`, `average`, `min`, `max` taken from
      `cycleStats`. Wire the locals into the four `statsJson[...]`
      assignments below
- [x] 4.3 Re-read the handler end-to-end: every reference to the old
      `cycleStats.get_*()` should now read from the local, and the four
      `statsJson[...]` assignments should compile against the new locals

## 5. Native multi-threaded test

- [x] 5.1 Create `test/test_stats_snapshot/test_stats_snapshot.cpp` with
      the `pio test` Unity harness, modelled on
      `test/test_device_config_snapshot/test_device_config_snapshot.cpp`
- [x] 5.2 Add `+<test/test_stats_snapshot/>` to the `native` env
      `build_src_filter` in `platformio.ini` so the suite is picked up
      (review point #8 — the current filter is full-exception, so it
      only takes an explicit `+<>` per suite)
- [x] 5.3 Test the single-snapshot invariant: one writer thread doing
      `add(value)` with a known sequence of values, one reader thread
      calling `snapshot()`. Assert that for every snapshot,
      `snap.count * snap.average == snap.total` (modulo integer
      truncation, which is bounded by `snap.count`) — i.e., the four
      counters come from the same logical instant and obey an identity
      the writer only ever produces as a unit
- [x] 5.4 Test the min/max invariant: for every snapshot,
      `snap.min <= snap.max`, and both fields are drawn from the set of
      inserted values (track the writer's history in an array bounded
      by `snap.count`; the min and max can only be values the writer
      has committed)
- [x] 5.5 Two writers, two readers — same invariants under higher
      contention. Mirrors the device-config pattern at the same line
      in `test_device_config_snapshot.cpp`
- [x] 5.6 Re-run the existing `test_support` suite. The existing
      single-task tests still pass because all four getters now lock
      around their read; behavior is unchanged for same-task callers

## 6. Verify and close out

- [x] 6.1 `pio test -e native` — all green, including the new
      `test_stats_snapshot` and the unchanged `test_support`
- [x] 6.2 `pio run -e adafruit_qtpy_esp32s2` — builds and reports the
      same flash / RAM shape as the baseline (one spinlock byte, no new
      allocations on the hot path)
- [x] 6.3 Re-read `src/routes/StatusRoutes.cpp` end-to-end to confirm
      the four `statsJson[...]` assignments are wired correctly to the
      snapshot locals and the JSON output is byte-identical in shape
- [x] 6.4 Update `docs/CODE_REVIEW.md` finding #4 to mark it resolved,
      in the shape of the existing entries (point at the change name
      and the files touched, list `src/support/Stats.{h,cpp}`,
      `src/task/SensorMonitor.h`, `src/routes/StatusRoutes.cpp`,
      plus the new test directory)
- [x] 6.5 `/opsx:archive` to fold the spec delta into
      `openspec/specs/system-architecture/spec.md` and move the change
      to `openspec/changes/archive/`
