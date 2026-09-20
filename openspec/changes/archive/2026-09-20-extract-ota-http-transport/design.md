## Context

`src/ota/OTAUpdater.cpp` currently holds three unrelated concerns in one
translation unit:

1. **HTTP/TLS transport** (lines 21–173) — `esp_crt_bundle_attach` extern,
   the `extern "C"` mbedTLS allocator override, the `redirectLocation`
   file-static, the `otaHttpEventHandler`, the `EspHttpReader` adapter,
   and the `HttpClient` RAII wrapper with its redirect-following loop.
2. **OTA state machine** — `checkForUpdate`, `performUpdate`, the
   `Activity` claim, the `stateMutex`, the `setUpdateState` reporter.
3. **Parked worker task lifecycle** — `otaCheckTask`, `otaWorkerTask`,
   `begin`, `startBackgroundCheck`, `startBackgroundUpdate`, the static
   task stacks / TCBs.

Concern 1 has zero callers outside concern 2; it could be reused by any
future subsystem that wants to talk HTTPS to GitHub (or any other host
behind a CA bundle) without dragging in OTA state. Today it cannot,
because it's bundled into OTAUpdater's TU.

`src/ota/OTAUpdater.h` carries the transport constant `HTTP_RX_BUFFER =
8192` and a multi-paragraph comment about TLS record framing and mbedTLS
handshake working-set size — both of which are about the transport, not
about OTA. They sit in the OTA header because that's where the
allocator's caller lives; once the transport moves, the comment moves
with it.

The existing flat single-level namespace convention
(`namespace Control`, `namespace Net`, `namespace Display`, …) does not
have a current home for HTTP/TLS code. Introducing a nested namespace
for OTA's transport layer is the first deviation from that convention;
the change documents why.

## Goals / Non-Goals

**Goals:**

- HTTP and TLS code lives under `src/ota/http/` and only there.
- The OTA flow (`OTAUpdater`, `OTAConfig`, `VersionCompare`) stays in
  `src/ota/` and reads from the transport via `OTA::Http::*`.
- The transport is reusable: any future subsystem can include the new
  headers and link the new `.cpp`s without dragging in `OTAUpdater` or
  its state.
- The `#ifdef ARDUINO` boundary stays correct: native tests still build
  without `<esp_http_client.h>`. The `RedirectScheme.h` predicate stays
  reachable from `test_ota_transport.cpp` unchanged in behaviour.
- The behaviour of the OTA flow is byte-for-byte identical: same
  redirect handling, same allocator routing rule, same RX/TX buffer
  sizes, same event-handler hookup.

**Non-Goals:**

- Moving `OTAUpdater` itself into `OTA::Updater`. Pre-existing global
  namespace; not part of this change.
- Adding new HTTP features (compression, retries, parallel requests).
  Pure relocation.
- Changing `RedirectScheme.h`'s public behaviour. Same predicate, new
  namespace.
- Touching the other OTA files (`OTAConfig.h`, `VersionCompare.h`).
- Adding tests for `HttpClient::openWithRedirects` itself — the existing
  predicate coverage in `test_ota_transport.cpp` continues to cover
  `RedirectScheme`, and the HTTP state machine is exercised by the
  on-device integration tests already in place.

## Decisions

### 1. Nested namespace `OTA::Http`, not flat `OtaHttp`

**Choice.** Every new transport symbol lives in `namespace OTA {
namespace Http { ... } }` (C++17 `namespace OTA::Http {}` shorthand,
matching the project's `-std=gnu++2a`).

**Rationale.** The transport is unambiguously OTA's HTTP client, not a
general HTTP utility: it carries OTA-specific constants
(`HTTP_RX_BUFFER` is sized for GitHub's TLS record framing) and
OTA-specific behaviour (the Location-capture handler exists to refuse a
redirect that would downgrade the OTA download to cleartext). The nested
namespace makes the OTA-scoped nature explicit at every call site
(`OTA::Http::HttpClient`) and creates an obvious home for future
OTA-only HTTP helpers (a future `OTA::Http::JsonClient`, a future
`OTA::Http::SignedUrlDownloader`, …) without polluting `namespace Http`.

**Alternative considered.** Flat `namespace OtaHttp` matching the
existing convention. The convention is one flat namespace per directory
(`Control`, `Net`, `Display`, …). Following it here would mean a flat
top-level `OtaHttp` namespace — readable, but loses the "this is OTA's
HTTP, not a general HTTP utility" signal. Rejected: the OTA-scoping is
load-bearing and the C++17 shorthand makes the nesting cost zero
characters at the call site.

### 2. `HttpEventHandler.h` owns the shared `g_redirectLocation` buffer

**Choice.** The redirect-capture event handler and the buffer it writes
live together in `HttpEventHandler.h/.cpp`. `HttpClient.cpp` includes
`HttpEventHandler.h`, reads the buffer through a small accessor, and
resets it at the top of each redirect-following iteration.

**Rationale.** The current code has the event handler write a file-static
and `openWithRedirects` read it — they share state by accident of being
in the same TU. Splitting them into separate TUs forces the state to be
named. Owning the buffer inside `HttpEventHandler` is the right home:
the handler is the only writer, and the buffer's whole purpose is
captured-Location-header prefixes. `HttpClient` is a consumer that asks
"what was the most recent Location prefix?" between redirect hops.

**Alternative considered.** Make `redirectLocation` a member of
`HttpClient` and reach it via `evt->client->user_data` from the handler.
The handler fires during `esp_http_client_open()`, before any
`HttpClient` method runs, so the handler has to access the buffer
through the SDK's handle rather than through the C++ wrapper. That
couples `HttpClient` construction order (must set `user_data` before the
first `open()`) to the event handler's lookup path, and pushes a
SDK-handle concern into the C++ class. The named-buffer-in-header is
simpler and keeps `HttpClient` ignorant of how the handler is wired.

### 3. `HttpEventHandler` is its own pair, not folded into `HttpClient`

**Choice.** `HttpEventHandler.h/.cpp` is a separate header/impl pair
even though it's only one `extern "C"` function plus a 32-byte buffer
plus an accessor.

**Rationale.** Single-responsibility. `HttpClient` owns the
`esp_http_client` lifecycle and the redirect loop. `HttpEventHandler`
owns the HTTP event format and the Location capture. They meet at the
named buffer; neither knows the other's internals. Splitting now keeps
the door open for adding more event handlers (a future content-length
validator, a future logging sink) without growing `HttpClient.cpp`.

**Alternative considered.** Inline the handler into `HttpClient.cpp` to
keep the file count down. Rejected: it couples HTTP event handling to
the HTTP client, which is the coupling we're trying to remove. The
file-count cost is two extra files of ~30 lines each.

### 4. `TlsAllocator` is its own pair

**Choice.** `TlsAllocator.h/.cpp` holds the `extern "C"` overrides of
`esp_mbedtls_mem_calloc` / `esp_mbedtls_mem_free`, the
`esp_crt_bundle_attach` extern declaration, and the `PSRAM_THRESHOLD =
4096` routing rule with its explanatory comment.

**Rationale.** The mbedTLS allocator override is resolved at link time
by the IDF SDK — the SDK calls our function, we don't call ours. It
therefore has no header API to design: the header exists only to mark
the file's existence and host the comment block that explains the
routing rule. Putting the override in `HttpClient.cpp` would force the
comment to live next to the redirect loop, where it does not belong;
putting it in `OTAUpdater.cpp` would force `HttpClient` to depend on a
file it shouldn't know about.

**Alternative considered.** A single combined `HttpTransport.cpp` with
all four concerns (HttpClient, HttpReader, HttpEventHandler,
TlsAllocator). Rejected: the file grows to ~250 lines and mixes
lifecycle, event handling, JSON streaming, and allocator overrides —
the opposite of what this change is trying to do.

### 5. `RedirectScheme.h` moves to `ota/http/` and joins the namespace

**Choice.** `src/ota/RedirectScheme.h` moves to
`src/ota/http/RedirectScheme.h`. Its body is unchanged; the function
moves from `Support::isSecureRedirectTarget` to
`OTA::Http::isSecureRedirectTarget`.

**Rationale.** The function classifies HTTPS vs HTTP redirect targets.
That is an HTTP-layer policy, not a generic "support" predicate —
joining the transport namespace is honest about its scope. The native
test that uses it (`test_ota_transport.cpp`) gains one line: a
`using OTA::Http::isSecureRedirectTarget;` alias in place of
`using Support::isSecureRedirectTarget;`.

**Alternative considered.** Leave it at `src/ota/RedirectScheme.h` to
keep the existing spec scenario "Listing OTA files" intact. Rejected:
the function would then live outside the namespace that owns the rest
of the HTTP/HTTPS code it is a part of, which is exactly the
co-location problem this change is solving.

### 6. `OTAUpdater` stays in the global namespace

**Choice.** `OTAUpdater` and its `CheckState` / `UpdateState` /
`Activity` enums remain in the global namespace, exactly as today.

**Rationale.** They predate this change and are referenced from
`src/main.cpp`, `src/Network.cpp`, `src/routes/OTARoutes.cpp`, and the
test files. Moving them into `OTA::Updater` would touch every caller
and is a separate, larger refactor.

**Alternative considered.** Move them too while we're here. Rejected:
out of scope; the proposal explicitly scopes this change to HTTP/TLS.

## Risks / Trade-offs

- [Risk] The new nested namespace is the first in the codebase, so
  future readers may be surprised. → Mitigation: the spec adds a
  scenario pinning the namespace name; the design doc (this file)
  records the rationale so it doesn't have to be re-derived.

- [Risk] `HttpClient.cpp` and `HttpEventHandler.cpp` share
  `g_redirectLocation` via an extern declaration in
  `HttpEventHandler.h`. If a future contributor adds a second TU that
  defines `g_redirectLocation` independently, the link will fail with a
  multiple-definition error — which is the desired safety net, but only
  if the extern declaration is the only definition site. →
  Mitigation: the buffer is declared `extern` exactly once
  (`HttpEventHandler.h`) and defined exactly once
  (`HttpEventHandler.cpp`). `HttpClient.cpp` does not define it.

- [Risk] The mbedTLS allocator override is a link-time symbol
  replacement. If `TlsAllocator.cpp` is excluded from the Arduino
  build, the SDK falls back to its pre-compiled allocator, which uses
  `MALLOC_CAP_INTERNAL` only and fails on this board. → Mitigation:
  the file is in `src/ota/http/`, and `[env:adafruit_qtpy_esp32s2]`'s
  default `build_src_filter` (which has no `+<...>` /
  `-<...>` overrides) picks up every `.cpp` under `src/`. The CI build
  catches any future exclusion.

- [Risk] The redirect-following loop and the Location capture are
  split across two TUs. A regression that lets the handler write to the
  buffer after `openWithRedirects` reset it would be silent. →
  Mitigation: behaviour is unchanged from today (handler writes a
  file-static, resetter reads and clears the same name). The native
  test continues to cover `isSecureRedirectTarget`'s logic. The HTTP
  state machine itself is exercised by the existing on-device
  integration.

- [Risk] Five new translation units means five TUs to compile into the
  Arduino image. → Mitigation: each is small (≤ 80 lines), the
  Arduino build is incremental, and the resulting link is smaller than
  the current `OTAUpdater.o` once the moved code is removed from it.

## Migration Plan

This is a pure refactor; no runtime migration is involved.

1. Land the change in one commit. The Arduino and native builds both
   run from the same source tree, so a partial state during the
   refactor would not link.
2. Rollback is a single `git revert`; the change does not touch
   persistent state, partition layout, or build flags.

## Open Questions

(none — every decision above is locked in.)
