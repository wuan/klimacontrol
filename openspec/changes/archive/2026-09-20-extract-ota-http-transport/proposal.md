## Why

The OTA subsystem's HTTP and TLS code currently lives inline in
`src/ota/OTAUpdater.cpp` (≈ 150 of its 729 lines: an RAII wrapper around
`esp_http_client`, an event handler that captures `Location` headers, a
streaming reader for ArduinoJson, and an `extern "C"` override of the
mbedTLS allocator) and as a transport constant in `src/ota/OTAUpdater.h`
(`HTTP_RX_BUFFER`, with a multi-paragraph mbedTLS comment). It is
intertwined with the OTA state machine, task lifecycle, and partition
plumbing — three distinct concerns in one translation unit. Separating
the transport layer into its own subfolder makes what is HTTP/TLS and what
is OTA flow immediately obvious from the file listing, lets future
subsystems reuse the same HTTP client without dragging in OTA state, and
establishes the first nested namespace in the codebase as a deliberate
boundary for transport code that lives entirely under OTA's umbrella.

## What Changes

- Create `src/ota/http/` with four header/impl pairs and one moved header:
  - `HttpClient.h/.cpp` — `OTA::Http::HttpClient` RAII wrapper around
    `esp_http_client`, carrying the redirect-following loop and the
    transport's `HTTP_RX_BUFFER` constant.
  - `HttpReader.h/.cpp` — `OTA::Http::HttpReader`, the byte-stream adapter
    that `deserializeJson` reads through so the GitHub releases JSON never
    lands in RAM whole.
  - `HttpEventHandler.h/.cpp` — `OTA::Http::captureRedirectLocation` event
    handler and the shared `g_redirectLocation` buffer it writes, read by
    `HttpClient::openWithRedirects`.
  - `TlsAllocator.h/.cpp` — `extern "C"` overrides of `esp_mbedtls_mem_calloc`
    and `esp_mbedtls_mem_free`, plus the `esp_crt_bundle_attach` extern
    declaration.
  - `RedirectScheme.h` — moves here from `src/ota/`. Body unchanged in
    behaviour; the `Support::isSecureRedirectTarget` function becomes
    `OTA::Http::isSecureRedirectTarget` so the namespace match is honest.
- Trim `src/ota/OTAUpdater.h`: drop the `HTTP_RX_BUFFER` constant and the
  transport-related comment block (moved to `HttpClient.h` /
  `TlsAllocator.h`). Keep all OTA state-machine and task declarations.
- Trim `src/ota/OTAUpdater.cpp`: drop the `HttpClient`, `EspHttpReader`,
  `otaHttpEventHandler`, `redirectLocation`, and mbedTLS allocator
  definitions (≈ 150 lines). Include the new headers and call the
  `OTA::Http::*` symbols instead. No behavioural change.
- Update `test/test_ota_transport/test_ota_transport.cpp` to include
  `ota/http/RedirectScheme.h` instead of `ota/RedirectScheme.h` and use
  the new namespace.
- Update the `ota-updates` spec:
  - Edit the `### Requirement: OTA source files live under src/ota/`
    scenarios so the file list accommodates `RedirectScheme.h` moving to
    `src/ota/http/`.
  - Add a new requirement pinning the file layout under `src/ota/http/`,
    the `OTA::Http` namespace, the `#ifdef ARDUINO` boundary, and
    `RedirectScheme.h`'s continued native-build reachability.

No runtime behaviour changes. No public API changes outside the new
namespace. No new dependencies.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `ota-updates`: the `### Requirement: OTA source files live under src/ota/`
  scenarios change (file-list relaxation), and a new requirement is added
  describing the `src/ota/http/` layout, namespace, and Arduino guard.

## Impact

- Source files touched:
  - `src/ota/OTAUpdater.h` (trim)
  - `src/ota/OTAUpdater.cpp` (trim)
  - `src/ota/RedirectScheme.h` (move + namespace wrap)
  - new: `src/ota/http/HttpClient.h`, `HttpClient.cpp`
  - new: `src/ota/http/HttpReader.h`, `HttpReader.cpp`
  - new: `src/ota/http/HttpEventHandler.h`, `HttpEventHandler.cpp`
  - new: `src/ota/http/TlsAllocator.h`, `TlsAllocator.cpp`
  - `test/test_ota_transport/test_ota_transport.cpp` (include + namespace)
- Build: `[env:adafruit_qtpy_esp32s2]` compiles the new `.cpp`s by default
  (no `build_src_filter` exclusions in that env). `[env:native]` keeps the
  same header-only reachability — none of the new `.cpp`s enter its
  `build_src_filter`, so native tests still build without `<esp_http_client.h>`.
- API surface: `OTA::Http::HttpClient`, `OTA::Http::HttpReader`,
  `OTA::Http::captureRedirectLocation`, `OTA::Http::capturedRedirectPrefix`,
  `OTA::Http::isSecureRedirectTarget`. All new; no callers outside the OTA
  subsystem.
- Existing `OTAUpdater` class: untouched. Stays in the global namespace
  (predates the nested-namespace convention; outside this change's scope).
