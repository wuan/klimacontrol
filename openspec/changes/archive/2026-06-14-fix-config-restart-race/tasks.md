## 1. Update Config.h header

- [x] 1.1 Add `#include <atomic>` to `src/Config.h` (unconditional, so native tests can exercise the new state)
- [x] 1.2 Replace the two `volatile` fields (`restartRequested`, `restartAt`) with a single `std::atomic<uint64_t> restartState` member of `ConfigManager`
- [x] 1.3 Add private `static constexpr` and `static` packing helpers (`pack`, `unpack`, `deadlineOf`, `isRequestedOf`) on `ConfigManager`
- [x] 1.4 Replace the misleading "32-bit aligned scalars are atomic on ESP32" comment with one that documents the packed atomic word, the bit layout, and the `acq_rel` memory ordering

## 2. Rewrite the restart-scheduling logic in Config.cpp

- [x] 2.1 Add a `static_assert(std::atomic<uint64_t>::is_always_lock_free)` at the top of `Config.cpp` so non-lock-free ports fail to compile
- [x] 2.2 Rewrite `ConfigManager::requestRestart()` to compute the new packed value and `store()` it with `memory_order_release`; keep the existing `ESP_LOGI` line
- [x] 2.3 Rewrite `ConfigManager::checkRestart()` to `load()` the packed state with `memory_order_acquire`, decode the flag + deadline via the helpers, and preserve the wrap-safe `int32_t` deadline comparison
- [x] 2.4 Update `ConfigManager::isRestartPending()` (currently an inline `const` accessor) to `load()` the packed state with `memory_order_acquire` and decode via `isRequestedOf`

## 3. Add native tests for the atomic restart state

- [x] 3.1 Create `test/test_config_restart_atomic/` with a `test_config_restart_atomic.cpp` file that includes `<unity.h>`, `<Config.h>`, and `<thread>`
- [x] 3.2 Add a `setUp` / `tearDown` pair and a `setRunTest` / `app_main` if the existing test suites use one (mirror `test_config` patterns)
- [x] 3.3 Add a unit test that asserts `std::atomic<uint64_t>::is_always_lock_free` is true
- [x] 3.4 Add a unit test that exercises `pack` / `unpack` round-trips for: zero, a normal 1 s deadline, the maximum representable deadline (`(1ULL << 63) - 1` shifted), and a deadline where the low bit is set in the raw input (should not leak into the flag)
- [x] 3.5 Add a concurrency test that spawns two producer threads each calling `requestRestart(...)` in a loop while a consumer thread calls `isRestartPending()`; assert that every observed "requested" sample has a non-zero deadline and that no consumer sample is "requested" with deadline 0
- [x] 3.6 Add the new suite to `platformio.ini` (or the test runner config) so `pio test -e native -f test_config_restart_atomic` picks it up

## 4. Verify the build and tests

- [x] 4.1 Run `pio run -e adafruit_qtpy_esp32s2` and confirm the firmware still builds with the new atomic state
- [x] 4.2 Run `pio test -e native` and confirm the existing `test_config` suite still passes
- [x] 4.3 Run `pio test -e native -f test_config_restart_atomic` and confirm the new suite passes
- [x] 4.4 Sanity-check `WebServerManager` and `Network` for any direct reads of the removed volatile fields; update or remove them if found
