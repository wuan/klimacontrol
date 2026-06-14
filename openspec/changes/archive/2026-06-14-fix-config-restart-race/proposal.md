## Why

`ConfigManager` writes the deferred restart signal as two independent volatile scalars
(`restartRequested` and `restartAt`) from the AsyncTCP/web-request task and reads them
from the main loop. On the current single-core ESP32-S2 the interleaving is benign
because the writer is preempted only at fixed points, but the pattern is a textbook
TOCTOU race: a reader can observe a stale `restartAt` paired with a freshly set
`restartRequested`, or vice versa, with no compiler or hardware guarantee of atomicity.
Moving to a dual-core ESP32-S3 (already on the roadmap) would turn this into a real
data race, and even on the S2 the volatile-only approach relies on assumptions that
`std::atomic` makes explicit.

## What Changes

- Replace the two volatile fields (`restartRequested` + `restartAt`) with a single
  64-bit `restartState` word that encodes both the "requested" flag and the deadline,
  guarded by a lightweight `std::atomic_flag` spinlock so the writer publishes both
  halves and the reader acquires both halves as one indivisible pair.
- The 64-bit encoding is `flag` in the low bit, `deadline` in the upper 63 bits,
  with a small set of `constexpr` helpers (`pack`, `unpack`, `isRequestedOf`,
  `deadlineOf`) so the call sites stay readable.
- The lock is acquired/released around every read and write of `restartState` via
  `lockRestart()` / `unlockRestart()` helpers; the spinlock is the alternative
  offered in the original race-condition report and is what the Xtensa toolchain
  supports (a plain `std::atomic<uint64_t>` is not lock-free on the target).
- Update the comment in `Config.h` so the rationale reflects the new design and no
  longer advertises the unsafe "32-bit aligned scalars are atomic" claim.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `configuration`: the **Restart management** requirement is tightened to require a
  single indivisible handoff (no separate flag + timestamp pair) so the scheduling
  state can be observed consistently by the main loop regardless of the producer
  task. The producer and consumer SHALL serialize their access to the shared
  state with a lightweight spinlock (e.g. `std::atomic_flag`) or a
  lock-free equivalent.

## Impact

- **Code**
  - `src/Config.h` — replace the two volatile fields with the atomic state and
    helper accessors; update the explanatory comment.
  - `src/Config.cpp` — rewrite `requestRestart()` and `checkRestart()` to pack/load
    the atomic state; `isRestartPending()` reads the same packed value.
- **APIs**: no public signature changes. `requestRestart(uint32_t)`,
  `isRestartPending()`, and `checkRestart()` keep their current shape.
- **Behavior**: no observable behavior change on the S2 today; the change is a
  portability / correctness hardening for future multi-core targets and a clearer
  contract for reviewers.
- **Dependencies**: relies on `<atomic>` from C++17 (already required by the
  PlatformIO `build_flags = -std=gnu++17`). The native test environment must gain
  the same include so the unit tests still build.
- **Tests**: add a `test_config_restart_atomic` suite under
  `test/test_config_restart_atomic/` that exercises the pack/unpack helpers and
  asserts that `isRestartPending()` flips atomically with the deadline (no torn
  reads under concurrent producers).
