## Why

The OTA transport layer is the most failure-prone code in the firmware and the hardest to test on-device: an OTA bug only manifests with a real release, a live GitHub redirect chain, and a fragmented heap after `WiFi.begin()`. The existing `ota-updates` spec already pins down the invariants — strict `ota::version_compare`, `https`-only redirects, exact asset-name match, internal-heap gating — but none of those deterministic predicates has a native test. Bugs regress silently and surface only when a user clicks "check for updates" in the field.

This change adds native coverage for the predicates that already have spec requirements, and tightens the transport layer where the existing code is correct but not robust against adjacent failure modes the spec doesn't yet call out (Location-header capture ordering, status-line parse failures, host-allowlist composition).

## What Changes

- **New native test suite** `test/test_ota_transport/` exercising the deterministic transport predicates:
  - `Support::isNewerVersion` covering each branch of `vMAJOR.MINOR.PATCH` comparison plus the suffix-ignore cases from the spec (untagged dev build, tag equal to running, tag older than running).
  - `redirectTargetIsSecure` covering absolute `https://`, absolute `http://`, relative `/path`, and the truncation edge case (Location header longer than the 32-byte capture buffer).
  - URL composition: `https://api.github.com/repos/{owner}/{repo}/releases/latest`, asset URL `https://github.com/{owner}/{repo}/releases/download/{tag}/firmware.bin`, host-allowlist prefix match for `info.downloadUrl.startsWith("https://github.com/")`.
  - Asset-name strict match: `assetName == "firmware.bin"` rejects `littlefs.bin`, `bootloader.bin`, `ota-firmware.bin`, and any `.bin` whose prefix differs.
- **Transport hardening** (no observable behavior change beyond what the spec already requires):
  - The `Location`-header capture buffer and `redirectTargetIsSecure` classifier become independently testable: the classifier takes the captured prefix as an argument rather than reading the file-static, so the native test can drive it directly. The `otaHttpEventHandler` still writes the file-static; the test exercises the function it feeds.
  - `openWithRedirects` adds an explicit log line naming the resolved host (`api.github.com` or the redirect target) at the start of each hop, so a connection-refused failure points at the URL rather than the bare transport.
  - The GitHub API host allowlist is composed once (constant) instead of being constructed per request; the existing `https://api.github.com/` literal moves to `OTAConfig.h` alongside `OTA_FIRMWARE_ASSET`.
- **No public API change.** `OTAUpdater.h`, `routes/OtaRoutes.*`, and the `FirmwareInfo` struct are unchanged. The web UI and `/api/ota/*` handlers see no difference.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `ota-updates`: add a requirement covering testability of the transport predicates, with one scenario per predicate already exercised. The new requirement does not change observable behavior; it documents the test surface so future refactors know what must keep working.

## Impact

- **Code:** `src/OTAUpdater.cpp` (Location-capture extraction, host-constant move), `src/OTAConfig.h` (new constant), `test/test_ota_transport/test_ota_transport.cpp` (new).
- **APIs:** None.
- **Dependencies:** None new. The native tests run under the existing `native` PlatformIO environment.
- **Build:** No change to `platformio.ini`. `pio test -e native` picks up the new directory automatically.
- **Runtime behavior:** Identical from the user and web-UI perspective. The added log line is at `ESP_LOGD` level and is off in the release build (`CORE_DEBUG_LEVEL=0`).
