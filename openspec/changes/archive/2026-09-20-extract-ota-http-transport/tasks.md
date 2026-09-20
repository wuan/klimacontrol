## 1. Create transport headers and impls under src/ota/http/

- [x] 1.1 Create `src/ota/http/TlsAllocator.h` declaring `OTA::Http::kEspMbedtlsCallocThreshold = 4096` and the two `extern "C"` allocator signatures (`esp_mbedtls_mem_calloc`, `esp_mbedtls_mem_free`), with a comment explaining the PSRAM-first routing rule for allocations above the threshold.
- [x] 1.2 Create `src/ota/http/TlsAllocator.cpp` wrapped in `#ifdef ARDUINO`. Provide the `extern "C" void *esp_mbedtls_mem_calloc(size_t n, size_t size)` and `extern "C" void esp_mbedtls_mem_free(void *ptr)` definitions, and the `extern "C" esp_err_t esp_crt_bundle_attach(void *conf);` declaration. Body is `#ifdef ARDUINO`-guarded and includes `<esp_heap_caps.h>`.
- [x] 1.3 Create `src/ota/http/HttpEventHandler.h` in `namespace OTA::Http`. Declare the `extern char g_redirectLocation[32]` buffer, `esp_err_t captureRedirectLocation(esp_http_client_event_t *evt)`, and `const char *capturedRedirectPrefix()`. Forward-declare `esp_http_client_event_t` so the header does not need `<esp_http_client.h>`.
- [x] 1.4 Create `src/ota/http/HttpEventHandler.cpp` wrapped in `#ifdef ARDUINO`. Define `g_redirectLocation[32]` once, implement `captureRedirectLocation` to `strlcpy` the `Location` header value into `g_redirectLocation` (truncation intentional), and `capturedRedirectPrefix` to return `g_redirectLocation`.
- [x] 1.5 Create `src/ota/http/HttpClient.h` in `namespace OTA::Http`. Declare `class HttpClient` with the existing `HttpClient` shape: explicit constructor taking `const esp_http_client_config_t &`, destructor, deleted copy/assign, `explicit operator bool()`, `int openWithRedirects(int maxRedirects = 5)`, and `esp_http_client_handle_t raw()` accessor. Include `<esp_http_client.h>` for the handle type. Add `constexpr int kHttpRxBuffer = 8192;` here (moved from `OTAUpdater.h`).
- [x] 1.6 Create `src/ota/http/HttpClient.cpp` wrapped in `#ifdef ARDUINO`. Move the existing `HttpClient` struct body verbatim, but route the redirect-scheme check through `OTA::Http::isSecureRedirectPrefix(capturedRedirectPrefix())` (added to `RedirectScheme.h`; see 2.1) and reset `g_redirectLocation[0] = '\0'` between redirect iterations. Include `"HttpEventHandler.h"` and `"RedirectScheme.h"`.
- [x] 1.7 Create `src/ota/http/HttpReader.h` in `namespace OTA::Http`. Declare `struct HttpReader { esp_http_client_handle_t client; int read(); size_t readBytes(char *buffer, size_t length); };`. Include `<esp_http_client.h>` for the handle type.
- [x] 1.8 Create `src/ota/http/HttpReader.cpp` wrapped in `#ifdef ARDUINO`. Move the existing `EspHttpReader` struct body verbatim into `OTA::Http::HttpReader`.

## 2. Move RedirectScheme.h

- [x] 2.1 Move `src/ota/RedirectScheme.h` to `src/ota/http/RedirectScheme.h`. Change `namespace Support {` to `namespace OTA::Http {`, rename `isSecureRedirectTarget` to `isSecureRedirectPrefix` (so the call site in `HttpClient.cpp` reads naturally as "is this captured prefix a secure redirect target?"), keep the function body and its inline `[[deprecated]]`-free form, keep the `#include <cstring>` and the `namespace ... { } // namespace` close.
- [x] 2.2 Delete the old `src/ota/RedirectScheme.h`.

## 3. Trim OTAUpdater.h and OTAUpdater.cpp

- [x] 3.1 In `src/ota/OTAUpdater.h`, remove the `HTTP_RX_BUFFER = 8192` constant and the multi-paragraph comment block that explains the mbedTLS record-framing rationale and the esp_mbedtls_mem_calloc offload (lines ~209–227). Replace with a short one-line forward reference to `OTA::Http::kHttpRxBuffer` in `HttpClient.h`.
- [x] 3.2 In `src/ota/OTAUpdater.cpp`, delete the `esp_crt_bundle_attach` extern declaration, the `esp_mbedtls_mem_calloc/free` `extern "C"` definitions, the `redirectLocation` file-static, the `otaHttpEventHandler` function, the `EspHttpReader` struct, and the `HttpClient` struct (lines 21–173). Keep everything from line 175 onward (the Activity claim through the end of the file).
- [x] 3.3 In `src/ota/OTAUpdater.cpp`, replace the `#include <esp_http_client.h>` with `#include "ota/http/HttpClient.h"`, `#include "ota/http/HttpReader.h"`, `#include "ota/http/HttpEventHandler.h"`, and `#include "ota/http/TlsAllocator.h"`. Drop `#include <WiFi.h>` if it is only there for the WiFi-side helper (it is currently included but never referenced; verify before removing).
- [x] 3.4 In `src/ota/OTAUpdater.cpp`'s `checkForUpdate`, set `config.event_handler = OTA::Http::captureRedirectLocation` instead of `otaHttpEventHandler`, and set `config.buffer_size = OTA::Http::kHttpRxBuffer` instead of `HTTP_RX_BUFFER`. Construct the client as `OTA::Http::HttpClient client(config);`. Pass `client.raw()` into the `OTA::Http::HttpReader` instead of `client.handle`. Call `OTA::Http::isSecureRedirectPrefix(...)` if any redirect handling reaches into the scheme classifier from this function (today it does not — `openWithRedirects` does that internally).
- [x] 3.5 In `src/ota/OTAUpdater.cpp`'s `performUpdate`, apply the same substitutions as 3.4: `config.event_handler = OTA::Http::captureRedirectLocation`, `config.buffer_size = OTA::Http::kHttpRxBuffer`, `OTA::Http::HttpClient client(config)`, `OTA::Http::HttpReader reader{client.raw()}`.

## 4. Update native test

- [x] 4.1 In `test/test_ota_transport/test_ota_transport.cpp`, change `#include "ota/RedirectScheme.h"` to `#include "ota/http/RedirectScheme.h"`.
- [x] 4.2 In the same file, change `using Support::isSecureRedirectTarget;` to `using OTA::Http::isSecureRedirectPrefix;` and update every test body that calls the function by that name (8 call sites in scenarios "absolute https / absolute http / relative / null / empty / truncated-https / truncated-http").

## 5. Verify

- [x] 5.1 Run `pio run -e adafruit_qtpy_esp32s2` and confirm a clean build.
- [x] 5.2 Run `pio test -e native` and confirm all OTA-related tests pass (`test_ota_updater`, `test_ota_transport`).
- [x] 5.3 Run `openspec validate --all --strict` (or `scripts/validate-openspec.sh`) and confirm a clean validation of the new change and the modified `ota-updates` spec.
- [x] 5.4 Spot-check that no `git grep -n Support::isSecureRedirectTarget` matches remain in the source tree.
