## Why

`Network::task()` handles a full-boot STA-association failure by
incrementing the persisted `wifi_failures` counter, waiting 2 s, and
calling `ESP.restart()`. With `startSTA()`'s 3×15 s attempts in a row
the actual STA-burst between two restarts is ~57 s, and the device hits
the AP-fallback threshold (every 3rd failure) within ~2 minutes of the
first failure. On a flaky AP that the device can see but cannot stay
associated with, this means a near-constant cycle of
boot→associate-fail→restart that makes the device effectively
unreachable for the user. The 2 s fixed delay gives the link no time
to recover between full boots, so the device keeps burning through the
AP-fallback windows and the user can never get in to reconfigure.

## What Changes

- Replace the fixed `vTaskDelay(2000 / portTICK_PERIOD_MS)` in
  `Network::task()` (line 422) with a call to a new
  `Network::staFailureBackoffMs(uint8_t failures)` helper that returns
  the next restart delay in milliseconds.
- The helper uses a doubling schedule starting at 2 s, capped at
  5 minutes: `min(2000 * 2^(failures-1), 300000)`. Concretely:
  2 s, 4 s, 8 s, 16 s, 32 s, 64 s, 128 s, 256 s, 300 s, 300 s, ….
- The helper is a `static` method on `Network` (no instance state) so
  it is testable on native without instantiating `Network`.
- Add a native unit test that exercises the helper for `failures = 0,
  1, 2, …, 12` and asserts the doubling, the saturation at 5 minutes,
  and the monotonicity of the curve.
- Document the schedule in a new requirement under
  `network-wifi-resilience` (the existing spec already covers
  intra-session resilience and the AP-fallback threshold; this is the
  missing boot-cycle delay).

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `network-wifi-resilience`: add a new **Exponential backoff on boot-time STA failure** requirement. The existing "Retry initial association before declaring failure" requirement stays as-is; the new requirement governs the delay *after* `startSTA()` returns failed, *before* the restart. The new requirement carries two scenarios: (a) first failure waits 2 s, (b) the delay saturates at 5 minutes so the curve does not grow without bound.

## Impact

- **Code**
  - `src/Network.h` — add a `static uint32_t staFailureBackoffMs(uint8_t failures)` declaration.
  - `src/Network.cpp` — define the helper (a `static` method) and replace the fixed `vTaskDelay(2000 / portTICK_PERIOD_MS)` at line 422 with a computed delay.
  - `src/Network.cpp` — log the chosen delay (`Restarting in N ms (failure k/N)`) so the schedule is observable in the serial log.
- **APIs**: no public signature change. The helper is `static` (or a free function in an anonymous namespace); `Network`'s public surface is unchanged.
- **Behavior**: identical on the first failure (2 s, same as today). Subsequent failures wait longer: 4 s, 8 s, 16 s, …, 60 s, 120 s, 300 s, 300 s, …. After ~30 minutes of sustained failure the device is rebooting only every 5 minutes, which gives a transient AP outage room to recover before the device tears down its association state again. The AP-fallback threshold (every 3rd failure) is unchanged.
- **Tests**: new `test/test_wifi_restart_backoff/` suite with a single file `test_wifi_restart_backoff.cpp` that exercises the helper. Add the new sources and test directory to `platformio.ini` under `[env:native] build_src_filter`.
- **Dependencies**: none.
- **Persistence**: the existing `wifi_failures` counter (persisted in
  NVS by `incrementConnectionFailures()`) is the input to the helper;
  no schema change. The counter continues to be reset to 0 on a
  successful association and continues to be incremented after each
  failed boot.
