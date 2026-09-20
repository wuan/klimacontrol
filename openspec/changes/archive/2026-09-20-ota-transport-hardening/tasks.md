## 1. Extract redirect-scheme classifier

- [x] 1.1 Create `src/support/RedirectScheme.h` with `inline bool Support::isSecureRedirectTarget(const char *location)`. Behavior: returns `false` for null or empty input; returns `true` for absolute URLs starting with `https://`; returns `true` for relative URLs (no `://` anywhere in the captured prefix); returns `false` otherwise. Use the `KLIMACONTROL_REDIRECT_SCHEME_H` include guard.
- [x] 1.2 In `src/OTAUpdater.cpp`, replace the body of `HttpClient::redirectTargetIsSecure()` with a single call to `Support::isSecureRedirectTarget(redirectLocation)`. The private static becomes a thin alias (or is removed entirely if no callers remain).
- [x] 1.3 Add `#include "support/RedirectScheme.h"` to `src/OTAUpdater.cpp` next to the other `support/` includes.

## 2. Move GitHub host literals to OTAConfig.h

- [x] 2.1 Add `OTA_GITHUB_API_HOST` (`"https://api.github.com/"`) and `OTA_GITHUB_RELEASE_HOST` (`"https://github.com/"`) to `src/OTAConfig.h` in the GitHub Configuration section, with a brief comment matching the style of the surrounding entries.
- [x] 2.2 In `src/OTAUpdater.cpp`, replace the literal in `checkForUpdate()` (`String("https://api.github.com/repos/") + ...`) with `String(OTA_GITHUB_API_HOST) + ...`. Keep the `String` concatenation shape so the resulting `apiUrl.c_str()` lifetime is unchanged.
- [x] 2.3 In `src/OTAUpdater.cpp`, replace `info.downloadUrl.startsWith("https://github.com/")` in `startBackgroundUpdateFromLatestCheck` with `info.downloadUrl.startsWith(OTA_GITHUB_RELEASE_HOST)`.

## 3. Add host-naming log line to openWithRedirects

- [x] 3.1 In `src/OTAUpdater.cpp`, inside `HttpClient::openWithRedirects()`, add an `ESP_LOGD(TAG, "OTA hop %d: %s -> %s", i, currentHost, nextHost)` line that prints the host portion of the captured `redirectLocation` after each successful `set_redirection()` call. Use `ESP_LOGD` so it is compiled out under `CORE_DEBUG_LEVEL=0`.

## 4. Create native test suite

- [x] 4.1 Create `test/test_ota_transport/test_ota_transport.cpp` with the standard `void setUp() {}` / `void tearDown() {}` / `int main()` / `int runUnityTests()` skeleton (see `test/test_network_helpers/test_network_helpers.cpp` for the exact pattern).
- [x] 4.2 Add version-compare tests covering: strictly newer (`v0.0.74` over `v0.0.73`), strictly older (returns false), equal (`v0.0.74` over `v0.0.74`), untagged dev build over its base tag (`v1.2.3-4-gabc1234` vs `v1.2.3` returns false), null current (returns false), unparseable tag (returns 0/false).
- [x] 4.3 Add redirect-scheme tests covering: absolute `https://`, absolute `http://`, relative `/path`, null, empty string, and the 31-character truncation edge case (Location longer than 32 bytes gets null-terminated at the cap, still classifies correctly).
- [x] 4.4 Add URL-composition test asserting `wuan`/`klimacontrol` produces exactly `https://api.github.com/repos/wuan/klimacontrol/releases/latest`.
- [x] 4.5 Add host-allowlist tests asserting the prefix-check returns true for `https://github.com/wuan/klimacontrol/releases/download/v0.0.74/firmware.bin` and false for `https://example.com/firmware.bin`.
- [x] 4.6 Add asset-name strict-match tests asserting `firmware.bin` is accepted and `littlefs.bin`, `bootloader.bin`, `ota-firmware.bin`, `firmware.bin.sha256` are rejected. The match logic lives inline in `checkForUpdate()`; either extract it into a small `Support::isExpectedFirmwareAsset(const char *)` helper in `OTAConfig.h` (preferred, keeps it native-testable) or duplicate the literal comparison in the test (acceptable since the literal is also in `OTAConfig.h`).
- [x] 4.7 Wire all `test_*` functions into `runUnityTests()` with `RUN_TEST` and return `UNITY_END()`.

## 5. Verify

- [x] 5.1 Run `pio test -e native` from the repo root. Confirm `test_ota_transport` appears in the suite and all tests pass.
- [x] 5.2 Run `pio run -e adafruit_qtpy_esp32s2` from the repo root. Confirm the firmware still builds with no warnings introduced by the refactor.
- [x] 5.3 Run `openspec validate --all --strict` from the repo root. Confirm the new capability passes.