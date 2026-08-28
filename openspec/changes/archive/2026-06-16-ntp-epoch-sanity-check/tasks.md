## 1. Network header — bounds, helper, counter

- [x] 1.1 Created `src/support/NtpEpoch.h` with `NtpEpoch::MIN_VALID` (2020-01-01) and `NtpEpoch::MAX_VALID` (2100-01-01) constants at namespace scope (extracted out of `Network.h` so unit tests can include the small header without pulling in the full Network class hierarchy / FreeRTOS)
- [x] 1.2 In `src/support/NtpEpoch.h`, added the header-inline helper `inline bool isNtpEpochPlausible(uint32_t epoch) { return epoch >= NtpEpoch::MIN_VALID && epoch <= NtpEpoch::MAX_VALID; }`
- [x] 1.3 In `src/Network.h`, added `uint32_t ntpBogusSyncCount = 0;` private field next to the existing NTP sync state

## 2. Network — apply the check at the initial sync site

- [x] 2.1 In `src/Network.cpp` `Network::connectSTA()` (around line 280), wrapped the `if (safeNtpUpdate())` block with a plausibility check on `ntpClient.getEpochTime()`: only set `ntpSynced = true` and update `lastNtpUpdateEpoch` when `isNtpEpochPlausible(epoch)` is true
- [x] 2.2 In the same block, when `safeNtpUpdate()` returns `true` but the epoch is not plausible, emit `ESP_LOGE(TAG, "NTP initial sync returned implausible epoch: %u (expected between %u and %u)", ...)` and increment `ntpBogusSyncCount`
- [x] 2.3 In the same block, leave `ntpSynced` unchanged (stays `false`) and leave `lastNtpUpdateEpoch` unchanged (stays `0`) on the bogus path

## 3. Network — apply the check at the unsynced-retry site

- [x] 3.1 In `src/Network.cpp` `Network::loop()`'s unsynced-retry branch (around line 683), wrapped the `if (safeNtpUpdate())` block with the same plausibility check as 2.1
- [x] 3.2 In the same branch, when the epoch is not plausible, emit the same `ESP_LOGE` line as 2.2 (slightly different prefix: "NTP retry returned implausible epoch") and increment `ntpBogusSyncCount`
- [x] 3.3 In the same branch, leave `ntpSynced` and `lastNtpUpdateEpoch` unchanged on the bogus path (the existing 1-minute retry timer will fire again)

## 4. Network — apply the check at the periodic refresh site

- [x] 4.1 In `src/Network.cpp` `Network::loop()`'s periodic-refresh branch (around line 655), wrapped the `if (safeNtpUpdate())` block with the plausibility check
- [x] 4.2 When the epoch is not plausible, emit the same `ESP_LOGE` line as 2.2 (slightly different prefix: "NTP update returned implausible epoch"), call `reportInternetFailure()`, and increment `ntpBogusSyncCount`
- [x] 4.3 On the bogus path, keep `lastNtpUpdateEpoch` unchanged (preserve the previous valid value) and keep `ntpSynced = true` (do not flap into the unsynced state); the next 1-hour interval will retry

## 5. Unit tests for the plausibility helper

- [x] 5.1 Created `test/test_ntp_epoch_sanity/test_ntp_epoch_sanity.cpp` with a Unity test suite that exercises `isNtpEpochPlausible()` at the boundaries: 0, `MIN_VALID - 1`, `MIN_VALID`, `MIN_VALID + 1`, `MAX_VALID - 1`, `MAX_VALID`, `MAX_VALID + 1`, `UINT32_MAX`, plus a typical-2026 value
- [x] 5.2 Added `+<test/test_ntp_epoch_sanity/>` to `build_src_filter` in `platformio.ini`
- [x] 5.3 Confirmed the new tests run under `pio test -e native` — 9/9 PASS (the test only includes `support/NtpEpoch.h`, no Arduino/Network dependencies)

## 6. Audit, build, test, archive

- [x] 6.1 Grep confirmed the new field/helper/counter names appear in `src/Network.h`, `src/Network.cpp`, `src/support/NtpEpoch.h`, and the new test file: `rg "NtpEpoch::MIN_VALID|NtpEpoch::MAX_VALID|isNtpEpochPlausible|ntpBogusSyncCount" src/ test/`
- [x] 6.2 Build for ESP32 target: `pio run -e adafruit_qtpy_esp32s2` — SUCCESS, zero new warnings; flash +928 bytes (1232170 vs 1231242)
- [x] 6.3 Run native tests: `pio test -e native` — 245/245 PASS (236 existing + 9 new boundary tests)
- [ ] 6.4 Archive the change with `/opsx:archive` to fold the spec delta into `openspec/specs/networking/spec.md`
