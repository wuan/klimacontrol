## 1. Add the watchdog-feeding helper

- [x] 1.1 Create `src/support/NetworkWatchdog.h` declaring `Support::setWdtResetHook(std::function<void()>)`, `Support::resetWdtResetHook()`, `Support::feedWdt()`, and the `Support::guardedCall(Fn&&)` template that runs the callable and feeds the watchdog before and after
- [x] 1.2 Create `src/support/NetworkWatchdog.cpp` with the static hook storage and the default implementation: under `ARDUINO` the default hook is `esp_task_wdt_reset`; on native it is a no-op `[] {}` (the test installs its own hook)
- [x] 1.3 Add `#include "NetworkWatchdog.h"` and the new `src/support/NetworkWatchdog.cpp` to `platformio.ini` under `[env:native] build_src_filter` so native tests can exercise the helper

## 2. Wire the helper into `Network`

- [x] 2.1 Add a `bool Network::safeNtpUpdate()` private method to `src/Network.h` that wraps `ntpClient.forceUpdate()` in `Support::guardedCall(...)`
- [x] 2.2 Define `Network::safeNtpUpdate()` in `src/Network.cpp` as a one-line call to the helper
- [x] 2.3 Replace the three `ntpClient.forceUpdate()` call sites in `src/Network.cpp` (lines 262, 584, 601) with `safeNtpUpdate()` and add a comment at each site pointing to the helper for future contributors
- [x] 2.4 Add a one-line comment block in `startSTA()` and in the periodic NTP section of `Network::task()` explaining why the helper exists (TWDT starvation on a hung UDP call)

## 3. Add native unit tests

- [x] 3.1 Create `test/test_network_ntp_watchdog/` with `test_network_ntp_watchdog.cpp` including `<unity.h>`, `<functional>`, and `"NetworkWatchdog.h"`
- [x] 3.2 Add a `setUp` / `tearDown` pair that calls `Support::resetWdtResetHook()` so tests do not leak state
- [x] 3.3 Add a test that installs a counting hook via `Support::setWdtResetHook`, calls `Support::guardedCall([]{ return true; })`, and asserts the hook fired exactly twice (before and after)
- [x] 3.4 Add a test that asserts `guardedCall` returns the callable's return value (both `true` and `false` paths)
- [x] 3.5 Add a test that installs a hook which throws (or asserts that the hook is still called after a callable that returns `false`)
- [x] 3.6 Add the new suite to `platformio.ini` under `[env:native] build_src_filter`

## 4. Verify the build and tests

- [x] 4.1 Run `pio run -e adafruit_qtpy_esp32s2` and confirm the firmware still builds with the new helper
- [x] 4.2 Run `pio test -e native` and confirm all existing native tests still pass
- [x] 4.3 Run `pio test -e native -f test_network_ntp_watchdog` and confirm the new suite passes
- [x] 4.4 Sanity-check: grep `forceUpdate` across `src/` to confirm only the three call sites inside `Network.cpp` (now replaced with `safeNtpUpdate()`) and the new helper itself exist; no other call site was missed by the audit
