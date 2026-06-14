## Context

`ConfigManager` exposes a tiny "deferred restart" facility so an HTTP handler can
return its response cleanly before the device reboots. The current implementation
in `src/Config.h:159-165` and `src/Config.cpp:15-34` uses two `volatile` scalars:

```cpp
volatile bool restartRequested = false;
#ifdef ARDUINO
volatile uint32_t restartAt = 0;
#endif
```

`requestRestart(delayMs)` writes `restartAt` first, then `restartRequested = true`.
`checkRestart()` reads `restartRequested` and, if true, reads `restartAt`. Each
field is independently volatile, so the compiler is free to keep them in separate
registers and even reorder them across function boundaries as long as the
load/store order within a single variable is preserved. The header comment
explicitly relies on "32-bit aligned scalars are atomic on ESP32", which is a
property of the load/store instructions — not a guarantee about the *visibility*
of a logically-related pair of variables.

Today, on the single-core ESP32-S2, the AsyncTCP/web-request task that calls
`requestRestart()` is preempted only at FreeRTOS yield points, and the main loop
that calls `checkRestart()` runs in the same task that owns the loop, so the
pattern has not produced an observable bug. The fragility shows up the moment
the producer runs on a different core (ESP32-S3, or any future dual-core
target) or the producer is migrated to a higher-priority task that preempts the
loop mid-publish.

The existing wrap-safe deadline comparison
`static_cast<int32_t>(millis() - restartAt) >= 0` must be preserved — the new
state word still needs to expose the same `int32_t` deadline math so the loop
behaves identically ~49 days after boot.

## Goals / Non-Goals

**Goals:**

- Replace the flag + timestamp pair with a single atomic word so a reader either
  sees "no restart requested" or "restart requested with this exact deadline".
- Keep the public API (`requestRestart(uint32_t)`, `isRestartPending()`,
  `checkRestart()`) byte-for-byte identical so no caller in `WebServerManager`
  or the main loop needs to change.
- Preserve the existing wrap-safe deadline comparison semantics.
- Make the design portable to dual-core targets without further changes.
- Add unit tests for the pack/unpack helpers and a multi-producer smoke test
  that asserts no torn reads.

**Non-Goals:**

- Refactoring the rest of `ConfigManager` (validation, NVS access, struct
  layout). The change is local to the restart-scheduling state.
- Switching to a lock-based design (e.g. `portMUX`). The 64-bit atomic
  primitive is enough to make the pair indivisible and avoids blocking.
- Adding a new capability spec; the existing `configuration` requirement is
  tightened in place.

## Decisions

### Decision 1 — Spinlock-protected 64-bit state (not lock-free atomic)

**Choice**: Replace the two volatile fields with a single 64-bit `uint64_t
restartState` (low bit = "requested" flag, upper 63 bits = unsigned deadline),
guarded by a `std::atomic_flag` spinlock that serializes all reads and writes.

**Rationale**: A 64-bit aligned `std::atomic<uint64_t>` is *not* reported as
lock-free by the Xtensa toolchain (it conservatively falls back to a libgcc
call, even though the LX7 has 64-bit `L64I`/`S64I`). A compile-time
`static_assert(std::atomic<uint64_t>::is_always_lock_free)` fails on the
ESP32-S2 toolchain we use today, so the lock-free path is off the table for
the current target. A `std::atomic_flag` spinlock is portable to both the
native (host) test environment and every ESP32 variant, is
constexpr-initializable, and the critical section is a few loads/stores — the
busy-wait cost is negligible compared to the I/O the surrounding code does.

**Alternatives considered**:

- **Plain `std::atomic<uint64_t>` (initial design choice)** — fails the
  `is_always_lock_free` check on the Xtensa toolchain. Hardware supports it,
  but the standard library doesn't promise it.
- **`portMUX` spinlock** — works, but `#include <esp_system.h>` and the
  `portMUX_TYPE` API are only available under `ARDUINO`, which would split
  the code path between native tests and firmware. `std::atomic_flag` is
  one standard C++ facility that works in both.
- **`std::atomic_flag` + `std::atomic<uint32_t>`** — two atomics still lets
  the writer interleave stores if the producer is preempted between them.
  Same race, different surface.
- **Two separate `std::atomic` fields with a `release`/`acquire` sequence** —
  does not work; `std::atomic` does not give you a multi-variable fence
  that says "this set was committed together".

### Decision 2 — Bit layout: low-bit flag, upper 63 bits deadline

**Choice**: `state = (deadline63 << 1) | flag`. Packing helpers live as
`static` methods on `ConfigManager` (no public API surface) and are
`constexpr` so the unit tests can exercise them without instantiating
`Preferences`.

**Rationale**: Keeping the flag in the low bit is the cheapest to mask
(`state & 1u`) and leaves the natural alignment of the deadline in the high
bits. The 63-bit deadline is more than enough: at the default 1 ms tick the
upper bound is ~2.9 × 10¹⁴ years, so the lost top bit is not a practical
constraint. (We never call `requestRestart` with a value larger than
`UINT32_MAX` ms — see `requestRestart(uint32_t delayMs)` — and the deadline
is computed as `millis() + delayMs` which is also `uint32_t`, so the
deadline fits comfortably in 63 bits.)

**Alternatives considered**:

- **High-bit flag, low 63 deadline** — equivalent, but the existing call sites
  already use `millis() - restartAt` with `int32_t` casting for wrap safety.
  Aligning the deadline near the high bits keeps the math legible if a future
  reader inspects the raw word.
- **Two separate `std::atomic` fields with a `release`/`acquire` sequence**
  — does not work; `std::atomic` does not give you a multi-variable fence
  that says "this set was committed together".

### Decision 3 — Memory order: `acq_rel`

**Choice**: `requestRestart()` does a single `store(packed, release)`;
`isRestartPending()` and `checkRestart()` do `load(acquire)`.

**Rationale**: The producer publishes two logical values (flag + deadline) and
the consumer needs to see both. `release` on the producer side ensures any
prior writes (e.g. to NVS) are visible before the flag flips; `acquire` on the
consumer side pairs with that. We don't need `seq_cst` because the only
consumer is the main loop and we don't care about ordering relative to other
atomics.

**Alternatives considered**:

- **`memory_order_relaxed`** — fine for the flag bit alone, but loses the
  happens-before relationship with the NVS write that motivated the restart
  (the consumer might restart the device before the new NVS value is durable
  on flash).
- **`memory_order_seq_cst`** — works, but the cost on Xtensa is the same as
  `acq_rel` and we have no cross-atomic ordering requirement that would
  justify it.

### Decision 4 — `std::atomic_flag` is enough; no extra lock type

**Choice**: `std::atomic_flag` only — no `portMUX`, no `std::mutex`.

**Rationale**: A `test_and_set`/`clear` pair is the smallest, most portable
synchronization primitive C++17 offers. The critical section is two or three
register-sized operations, so a busy-wait is cheaper than taking a FreeRTOS
mutex and is comparable in cost to a `portMUX` critical section. The flag is
`constexpr`-initializable to `ATOMIC_FLAG_INIT` so no setup is required in
`begin()`.

## Risks / Trade-offs

- **Risk**: A misaligned `uint64_t` on an exotic port would silently fall back
  to a lock and lose the atomicity property.
  **Mitigation**: Add a `static_assert(std::atomic<uint64_t>::is_always_lock_free)`
  in `Config.cpp` so a non-lock-free port fails to compile.
- **Risk**: Someone refactors `requestRestart` and forgets to use the
  packing helpers, falling back to two independent stores.
  **Mitigation**: Keep the fields `private` and expose only
  `isRestartPending()` / `requestRestart()` / `checkRestart()`. The packing
  helpers live as `static` methods on the class.
- **Risk**: Tests on the native env (`pio test -e native`) build without
  `<atomic>` if the include is missing.
  **Mitigation**: `#include <atomic>` in `Config.h` (it's already used
  unconditionally for the new state) and add a native test that constructs
  the manager and asserts `is_always_lock_free` holds.
- **Trade-off**: We lose the ability to read `restartAt` independently of
  `restartRequested` (a minor diagnostic on the rare boot-loop). The deadline
  is still recoverable via a `deadline()` accessor on the class for any future
  logging that needs it.

## Migration Plan

1. Land the change in one PR — the API is identical and the runtime behavior
   is unchanged.
2. Run `pio run -e adafruit_qtpy_esp32s2` to confirm the firmware still builds
   (the change is `#ifdef ARDUINO` aware: the atomic state is still present on
   native so the test suite can exercise it).
3. Run `pio test -e native -f test_config_restart_atomic` to confirm the
   atomic semantics hold under the host compiler.
4. No rollback plan needed beyond `git revert`; the change is local and the
   previous volatile implementation is recoverable from history.

## Open Questions

- None. The dual encoding is the only new design surface, and the wrap-safe
  `int32_t` deadline comparison has a direct port to the packed word.
