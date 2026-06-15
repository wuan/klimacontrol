## 1. Add the backoff helper to `Network`

- [x] 1.1 Declare `static uint32_t Network::staFailureBackoffMs(uint8_t failures)` in `src/Network.h` with a comment that explains the doubling schedule and the 5-minute cap
- [x] 1.2 Define the helper in `src/Network.cpp` (a `static` method) returning `min(2000ULL * (1ULL << (failures - 1)), 300000ULL)`; clamp `failures` to 1 to keep the shift well-defined if a future caller passes 0

## 2. Use the helper in the boot-time failure path

- [x] 2.1 Replace the fixed `vTaskDelay(2000 / portTICK_PERIOD_MS)` in `Network::task()` (line 422) with a computed delay: capture `newFailures`, compute `uint32_t backoffMs = staFailureBackoffMs(newFailures)`, log `Waiting %u ms before retry (failure %u/%u)`, and `vTaskDelay(backoffMs / portTICK_PERIOD_MS)`
- [x] 2.2 Add a one-line comment above the call site pointing to the spec requirement "Exponential backoff on boot-time STA failure" in `network-wifi-resilience`

## 3. Add native unit tests

- [x] 3.1 Create `test/test_wifi_restart_backoff/` with `test_wifi_restart_backoff.cpp` that includes `<unity.h>` and `"support/WifiBackoff.h"`
- [x] 3.2 Add a `setUp` / `tearDown` pair (no state to reset; just symmetry with the other suites)
- [x] 3.3 Add a test that asserts the doubling portion of the curve: `staFailureBackoffMs(1..8) == {2000, 4000, 8000, 16000, 32000, 64000, 128000, 256000}`
- [x] 3.4 Add a test that asserts the cap: `staFailureBackoffMs(9) == 300000`, `staFailureBackoffMs(10) == 300000`, `staFailureBackoffMs(12) == 300000`, `staFailureBackoffMs(32) == 300000`, `staFailureBackoffMs(200) == 300000` (no overflow on large counts)
- [x] 3.5 Add a test that asserts monotonicity: for `failures = 1..16`, the delay is non-decreasing
- [x] 3.6 Add the new suite to `platformio.ini` under `[env:native] build_src_filter` (`+<support/WifiBackoff.cpp> +<test/test_wifi_restart_backoff/>`)

## 4. Verify the build and tests

- [x] 4.1 Run `pio run -e adafruit_qtpy_esp32s2` and confirm the firmware still builds with the new helper
- [x] 4.2 Run `pio test -e native` and confirm all existing native tests still pass (the new sources in `build_src_filter` may bring in transitive dependencies — fix any pre-existing -Werror warnings that surface, mirroring the `fail-hard-mutex-init-failure` change)
- [x] 4.3 Run `pio test -e native -f test_wifi_restart_backoff` and confirm the new suite passes
- [x] 4.4 Sanity-check that the helper is the only call site for the schedule: grep `staFailureBackoffMs` across `src/` and confirm both the declaration and the single call in `Network::task()` are present, with no other implementations or hardcoded `vTaskDelay` paths that bypass the helper
