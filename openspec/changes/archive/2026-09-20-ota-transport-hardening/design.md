## Context

The OTA subsystem has a comprehensive spec (`openspec/specs/ota-updates/spec.md`) covering version compare, asset-name strict match, redirect transport enforcement, internal-heap gating, task lifecycle, and mutual exclusion. Every requirement has a deterministic predicate behind it, and every predicate is a candidate for native tests. None currently has one. Bugs regress silently and surface only on real devices with a live GitHub redirect chain and a fragmented heap — which is the worst possible place to discover them.

The transport code itself (`HttpClient`, `EspHttpReader`, `otaHttpEventHandler`) is wrapped in `#ifdef ARDUINO` because it depends on `<esp_http_client.h>`. We can't bring the transport itself into a native test, but we can pull the predicates out of it (same pattern as `src/support/VersionCompare.h` and `src/support/HostValidation.h`).

The existing `src/support/` directory holds header-only `inline` functions in `namespace Support`. The new pieces slot in there.

## Goals / Non-Goals

**Goals:**

- Native coverage for every deterministic predicate the OTA spec promises.
- Independent testability of `redirectTargetIsSecure` (currently a `private static` member of `HttpClient`, hidden behind `#ifdef ARDUINO`).
- A single source of truth for the GitHub API and release hosts, in `OTAConfig.h` alongside `OTA_FIRMWARE_ASSET`.
- No change to public API, no change to web-UI behavior, no change to runtime behavior beyond an `ESP_LOGD`-level host-naming log line that is off in the release build.

**Non-Goals:**

- Porting `explainNetworkFailure` from the ledz implementation. Out of scope; the user's chosen scope was "tests + transport hardening", not diagnostics. Tracked as a separate change if desired.
- SHA-256 verification of the downloaded image. Out of scope; would change observable behavior (release pipeline needs to ship `.sha256` sidecars, update becomes a no-op if missing).
- Tests for the actual `openWithRedirects` HTTP state machine. Out of scope because it requires a real `<esp_http_client.h>` transport; this design extracts the predicates so the state machine's *invariants* are tested instead.
- Changes to `routes/OtaRoutes.*` or `data/*.html`.

## Decisions

### Decision 1: Extract `redirectTargetIsSecure` to `src/support/RedirectScheme.h`

**Choice.** Promote `HttpClient::redirectTargetIsSecure()` from a `private static` member to an `inline` free function in `namespace Support` in a new header `src/support/RedirectScheme.h`. The `HttpClient::openWithRedirects()` body calls `Support::isSecureRedirectTarget(redirectLocation)` instead of the static.

**Rationale.** `HostValidation.h` and `VersionCompare.h` already use this exact pattern: pure C++, no Arduino-only headers, native-test-safe. The classifier itself is three lines of pointer arithmetic and has no transport dependency — it was a static member only because the code happened to be a class. Pulling it out is mechanical and makes the contract independently testable.

**Alternatives considered.** *Keep as private static, expose via a `friend` test class.* Ugly — the test wouldn't be able to include `OTAUpdater.cpp` (which is `#ifdef ARDUINO`-guarded) without dragging in `<esp_http_client.h>`. *Move it into an anonymous namespace and re-`#include` the source from the test.* Works but breaks the existing single-translation-unit design. The header move is the smallest change that achieves native testability.

### Decision 2: Move GitHub host literals to `OTAConfig.h`

**Choice.** Add two constants to `src/OTAConfig.h`:

```cpp
#define OTA_GITHUB_API_HOST    "https://api.github.com/"
#define OTA_GITHUB_RELEASE_HOST "https://github.com/"
```

Replace the inline literals in `OTAUpdater.cpp`:
- `String("https://api.github.com/repos/") + owner + "/" + repo + "/releases/latest"` uses `OTA_GITHUB_API_HOST`.
- `info.downloadUrl.startsWith("https://github.com/")` uses `OTA_GITHUB_RELEASE_HOST`.

**Rationale.** `OTA_FIRMWARE_ASSET` is already there because it's a policy constant the spec explicitly names. The two hosts are policy constants in the same sense: the spec already requires the check URL to be `api.github.com` and the download URL prefix to be `https://github.com/`. Centralizing them removes the only place the firmware knows the GitHub URL by construction rather than reference.

**Alternatives considered.** *Leave them inline.* They're tested once they exist as constants; leaving them inline means the test has to assert on a literal string every time. Not worth the duplication.

### Decision 3: Native test file is one directory, not three

**Choice.** `test/test_ota_transport/test_ota_transport.cpp` covering version compare, scheme classification, URL composition, host-allowlist prefix match, and asset-name strict match.

**Rationale.** Existing test layout (`test_network_helpers`, `test_sensor_scheduling`) groups related predicates into one file. The five predicates above all serve one capability (`ota-updates`) and are exercised together when a transport regression is suspected. Splitting into five directories (`test_ota_version_compare/`, `test_ota_redirects/`, ...) would add five `runUnityTests()` aggregators and five `platformio.ini` entries for no observable benefit.

**Alternatives considered.** *One test directory per predicate.* Matches the `test_*` per-directory convention but adds CI cost and doesn't improve signal — a refactor that breaks two related predicates at once is more useful as a single file's failure than as two.

### Decision 4: Add `ESP_LOGD` host-naming log line in `openWithRedirects`

**Choice.** At the start of each redirect hop, log the current and next URL at `ESP_LOGD`. The `api.github.com` first-hop line and the CDN second-hop line now name the URL explicitly. Compiled out in release because `CORE_DEBUG_LEVEL=0`.

**Rationale.** When an OTA fails in the field, the existing log says `"Redirect refused: target is not HTTPS"` with no host. Adding the host makes the failure self-explanatory. The cost is bytes in debug builds only.

**Alternatives considered.** *Log at `ESP_LOGI`.* Visible in production logs but adds noise on every working update. `ESP_LOGD` is the right level for "diagnostic context when something has gone wrong."

## Risks / Trade-offs

- **`RedirectScheme.h` extraction is a behavioral no-op but a refactor.** [Risk: subtle change in evaluation order if the test runner inlines differently.] → Mitigation: the function takes `const char *` and returns `bool`. No state. No side effects. The test asserts the same outputs as before. Native tests cover every branch.

- **Host-constant move changes two source lines.** [Risk: a typo in the constant definition breaks the URL composition silently.] → Mitigation: the native test asserts the composed URL byte-for-byte. A typo fails the test.

- **The native tests cover predicates, not the state machine that wires them together.** [Risk: `openWithRedirects` itself could regress (e.g., forget to close before `set_redirection`, leak file-static state across requests) without a failing test.] → Mitigation: the spec's existing scenarios around redirect enforcement are the contract; this change adds *predicate* coverage, not state-machine coverage. State-machine bugs would require a test that drives `esp_http_client`, which means a PlatformIO integration environment rather than `native`. Out of scope for this change but worth flagging as follow-up.

- **`#ifdef ARDUINO` boundary stays where it is.** [Risk: a future contributor might assume `RedirectScheme.h` is callable from Arduino code paths, or vice versa, and break the boundary.] → Mitigation: the header is pure C++ with no Arduino-only includes, identical to `HostValidation.h` and `VersionCompare.h`. The pattern is already established.

- **No new build-system entries.** [Risk: someone adds `test_build_src = yes` or `test_filter` to `platformio.ini` and the new test silently stops running.] → Mitigation: none beyond following the existing layout, which already does this for the other 19 test directories.

## Migration Plan

No deployment steps. The change is source-only:

1. Add `src/support/RedirectScheme.h`.
2. Extend `src/OTAConfig.h` with the two host constants.
3. Modify `src/OTAUpdater.cpp` to use them (host constants) and to call `Support::isSecureRedirectTarget` (classifier extraction). Add the `ESP_LOGD` host-naming line.
4. Add `test/test_ota_transport/test_ota_transport.cpp`.
5. Run `pio test -e native` to confirm green.
6. Run `pio run -e adafruit_qtpy_esp32s2` to confirm firmware still builds.

Rollback: revert the four file changes. No runtime state is changed in any branch where the firmware reaches a steady state.

## Open Questions

- **Should `Support::isSecureRedirectTarget` also reject the empty string?** Currently `redirectLocation[0] = '\0'` is the "no Location captured" sentinel and `isSecureRedirectTarget("")` would return `false` (no `://` and no `https://` prefix, but `strstr("", "://") == nullptr` is also true so it returns `true`). Need to either add an explicit empty check or document that the caller must reset the buffer first. The current `openWithRedirects` does reset it (`redirectLocation[0] = '\0';` at the top of each iteration), so the behavior is correct *in context* but the predicate is wrong in isolation. The native test will pin this down — likely the right answer is "the function rejects empty strings explicitly, the caller does not need to pre-reset." Worth confirming during implementation.
- **Do we want to assert on `User-Agent: "ESP32-OTA/1.0"` in a native test?** The header is sent but never inspected by the server in a meaningful way; it would be a brittle test. Skipping unless someone asks.