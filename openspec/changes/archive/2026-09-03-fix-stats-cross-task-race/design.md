# Design: cross-task `Support::Stats` reads

## Context

`Support::Stats` (in `src/support/Stats.{h,cpp}`) accumulates four `uint64_t`
counters — a running total, a sample count, the running min, and the
running max — for the SensorMonitor loop period. One caller writes
(`Task::SensorMonitor::task()` line 85, once per loop iteration) and one
caller reads (`routes/StatusRoutes.cpp:97-101`, four times per
`GET /api/about` invocation, on the AsyncTCP task).

Today every counter is a naked `uint64_t`. The individual counters can tear
on a 64-bit load across two 32-bit bus cycles (Xtensa LX7 has no
naturally-aligned 64-bit atomic), and `min_value` / `max_value` are
additionally updated by an unguarded compare-store that has no happens-
before relationship with a concurrent read.

The DeviceConfig fix in `2026-09-03-fix-device-config-cross-task-race`
established the project pattern: a `std::atomic_flag` spinlock plus a
`getXxxSnapshot()` accessor that returns an indivisible copy of the
backing struct. This change applies the same pattern to a smaller,
simpler object — four scalars instead of one ~250-byte struct.

## Goals / Non-Goals

**Goals:**

- Provide an indivisible `StatsSnapshot` value that any cross-task reader
  can rely on to be coherent across all four fields.
- Keep the existing `add()` / `get_min()` / `get_max()` / `get_average()` /
  `get_count()` API so the existing unit tests (`test_support`) keep
  compiling and passing without modification.
- Use the same spinlock discipline already used by `deviceConfigLock`
  (`Config.h:386-396`) so the code reads as one consistent pattern across
  the codebase.
- Cover the new snapshot path with a multi-threaded regression test.

**Non-Goals:**

- Per-field `std::atomic<uint64_t>`. The Xtensa toolchain reports
  `std::atomic<uint64_t>` as not lock-free (same caveat as in
  `Config.h:353-356`); going per-field would either silently fall back to
  a libgcc call or fail to compile under `-Watomic-alignment`.
  Per-field atomics would also leave the four-field observation
  inconsistent with each other, which is the *bigger* defect this change
  is fixing — `/api/about` currently reports four stats for one tick
  that may actually be four different ticks.
- A general framework for "all cross-task shared objects". Stats,
  DeviceConfig, the gains-pending flag, and the OTA `Activity` state are
  four known cases; addressing them individually keeps each change reviewable.
- A monotonically updating `min_value` / `max_value` that other tasks can
  observe. The contract is "snapshot of all four counters at one logical
  instant", which is what `/api/about` needs.

## Decisions

### D1. `std::atomic_flag` spinlock over `portMUX_CRITICAL` or `FreeRTOS` mutex

Same shape as `deviceConfigLock` (`Config.h:386-396`). `std::atomic_flag`
is the lightest primitive that gives `acquire` / `release` semantics on
the toolchain the project ships, has no FreeRTOS dependency (so the
`native` test environment compiles and runs identically), and is what
`restartLock` (`Config.h:360`) and `deviceConfigLock` already establish
as project precedent.

`portENTER_CRITICAL` / `portEXIT_CRITICAL` would compile under `ARDUINO`
only and break the native harness. A FreeRTOS mutex requires
`vSemaphoreCreateBinary` / `xSemaphoreCreateMutex` at `begin()` and
brings priority-inversion considerations that the project already chose
to avoid for short critical sections in `Config.h:381-385`.

### D2. One snapshot accessor, not a snapshot per field

A `getMinSnapshot()`, `getMaxSnapshot()` pair still lets the four
counters be observed at four different ticks. The whole point of the
fix is to give `/api/about` a consistent set of numbers for one cycle.

`StatsSnapshot { uint64_t count, average, min, max; }` carries all four
fields, computed under one spinlock acquisition.

### D3. Existing getters remain, lock-protected

The native tests in `test/test_support/test_support.cpp` exercise
`get_min()` / `get_max()` / `get_average()` / `get_count()` directly,
and there are no other call sites to update. Keeping the methods and
making each one acquire the lock around its single read costs one
extra `test_and_set` / `clear` per method call (a few dozen cycles)
and removes a separate breaking change.

When the `/api/about` migration lands, the four methods fall out of the
hot path; the tests stay as documentation of the simple-read shape.

### D4. No `begin()` / constructor work

`std::atomic_flag` default-initializes to *clear* in C++20, but the
project uses C++17 where `ATOMIC_FLAG_INIT` is the explicit clear form.
The `atomic_flag` member therefore reads:

```cpp
mutable std::atomic_flag lock = ATOMIC_FLAG_INIT;
```

identical to the pattern in `Config.h:386`. No constructor body, no
`begin()`, no FreeRTOS precondition — the lock is ready by the time
`SensorMonitor`'s `startTask()` schedules the first `add()` call, and
synchronous-with the constructor of the host object.

### D5. `SensorMonitor::getStatsSnapshot()` next to `getStats()`

`getStats()` returning a `const Support::Stats&` is fine for same-task
callers; the new `getStatsSnapshot()` returns a `Support::StatsSnapshot`
by value, acquires the lock to read all four counters, then releases.
`StatusRoutes.cpp` switches to the snapshot getter and never touches the
reference again.

### D6. Test: writers and readers, both single and double

Following the device-config snapshot test pattern
(`test/test_device_config_snapshot/test_device_config_snapshot.cpp`):

- one writer thread looping `add(value)` with a sequence of values
  traceable from the reader side;
- one or two reader threads looping `snapshot()` and asserting
  `snapshot.count == k` whenever the writer has done `k` `add()` calls;
- assert that, for every snapshot, `snapshot.min` and `snapshot.max`
  appear in the writer's history (no "min > max", no "value outside
  the inserted range");

The bounded test runs in well under a second on host. The firmware build
is unchanged in shape — same flash / RAM headroom as the baseline.

## Risks / Trade-offs

- **Spinlock priority inversion.** The spinlock is held for the duration
  of one struct copy (a handful of bus cycles) — strictly shorter than
  any of the existing `deviceConfigLock` critical sections. Both the
  writer (Sensor Monitor task) and the reader (AsyncTCP handler) are at
  the same priority, so even busy-wait inversion here is bounded by
  the duration of one struct copy.
- **Stale numbers.** The snapshot reflects a single, internally
  consistent state. It may still be "1 s old" by the time it leaves the
  handler — that is unchanged from today; the bug being fixed is the
  internal inconsistency, not the age.
- **Test sensitivity.** A torn-read regression test can produce
  flakiness rather than deterministic failure if the lock is missing.
  The test asserts a non-trivial property (`snapshot.min` belongs to the
  writer's history AND `snapshot.max >= snapshot.min` AND
  `snapshot.count * snapshot.average == snapshot.total ± N`), so an
  unguarded implementation fails on the very first race window observed.
  *Mitigation:* run the test 100 times in CI by virtue of being in
  `pio test -e native`; document the property, not just one observation.
- **StatsSnapshot vs. the existing getters.** Two ways to read, two
  correct shapes, one wrong shape (mixing them). *Mitigation:* the
  snapshot is the documented cross-task API; the legacy getters stay
  for tests and same-task use only. A short comment on `getStats()`
  says "same-task readers only" and points to the snapshot.

## Migration Plan

Standard non-breaking rollout — `Support::Stats` gains a method, every
existing call site continues to compile and run, one call site moves
to the new method, and the regression test enters the existing test
suite. No flag day, no version bump, no data migration.

Rollback is `git revert` of the change.

## Open Questions

None. The shape mirrors `deviceConfigLock` exactly, and the only call
site is `routes/StatusRoutes.cpp:97-101`.
