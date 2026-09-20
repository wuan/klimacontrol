## MODIFIED Requirements

### Requirement: OTA source files live under src/ota/

Headers and implementation files that are exclusively consumed by the OTA subsystem SHALL be co-located under `src/ota/` (at any depth) rather than living under `src/` top-level or under `src/support/`, so that all OTA implementation lives in one obvious place. The HTTP/TLS transport layer is itself an OTA concern and lives at `src/ota/http/`; see the dedicated requirement for that layout.

#### Scenario: Listing OTA files

- **WHEN** a reader enumerates the files under `src/ota/` (recursively)
- **THEN** the directory SHALL contain `OTAUpdater.h`, `OTAUpdater.cpp`, `OTAConfig.h`, and `VersionCompare.h` directly under `src/ota/`, and SHALL contain `RedirectScheme.h`, `HttpClient.h`, `HttpClient.cpp`, `HttpReader.h`, `HttpReader.cpp`, `HttpEventHandler.h`, `HttpEventHandler.cpp`, `TlsAllocator.h`, and `TlsAllocator.cpp` under `src/ota/http/`, and SHALL NOT be missing any of them

#### Scenario: No OTA files at src/ top level

- **WHEN** a reader enumerates the files directly under `src/`
- **THEN** no file SHALL be named `OTAUpdater.h`, `OTAUpdater.cpp`, `OTAConfig.h`, `RedirectScheme.h`, `HttpClient.h`, `HttpClient.cpp`, `HttpReader.h`, `HttpReader.cpp`, `HttpEventHandler.h`, `HttpEventHandler.cpp`, `TlsAllocator.h`, `TlsAllocator.cpp`, or `VersionCompare.h` (i.e. none of the OTA sources are at `src/` root)

#### Scenario: No OTA files under src/support/

- **WHEN** a reader enumerates the files directly under `src/support/`
- **THEN** no file SHALL be named `RedirectScheme.h`, `HttpClient.h`, `HttpClient.cpp`, `HttpReader.h`, `HttpReader.cpp`, `HttpEventHandler.h`, `HttpEventHandler.cpp`, `TlsAllocator.h`, or `TlsAllocator.cpp` (i.e. none of the OTA-only support headers remain there)

## ADDED Requirements

### Requirement: OTA HTTP and TLS transport lives under src/ota/http/

The HTTP and TLS code that backs OTA's GitHub release check and firmware download SHALL live exclusively under `src/ota/http/`. All such code SHALL be in the nested namespace `OTA::Http` (declared as `namespace OTA::Http { ... }`), distinct from the global namespace in which `OTAUpdater` lives.

#### Scenario: Transport files live under src/ota/http/

- **WHEN** a reader enumerates the files under `src/ota/http/`
- **THEN** the directory SHALL contain `HttpClient.h`, `HttpClient.cpp`, `HttpReader.h`, `HttpReader.cpp`, `HttpEventHandler.h`, `HttpEventHandler.cpp`, `TlsAllocator.h`, `TlsAllocator.cpp`, and `RedirectScheme.h`, and SHALL NOT be missing any of them

#### Scenario: No transport files directly under src/ota/

- **WHEN** a reader enumerates the files directly under `src/ota/`
- **THEN** no file SHALL be named `HttpClient.h`, `HttpClient.cpp`, `HttpReader.h`, `HttpReader.cpp`, `HttpEventHandler.h`, `HttpEventHandler.cpp`, `TlsAllocator.h`, or `TlsAllocator.cpp`

#### Scenario: Transport code lives in namespace OTA::Http

- **WHEN** the OTA HTTP/TLS source files are read
- **THEN** every public symbol declared in `HttpClient.h`, `HttpReader.h`, `HttpEventHandler.h`, and `RedirectScheme.h` SHALL be reachable as `OTA::Http::<symbol>` (i.e. wrapped in `namespace OTA::Http`)

#### Scenario: HTTP/TLS implementation files are gated by #ifdef ARDUINO

- **WHEN** a reader inspects `HttpClient.cpp`, `HttpReader.cpp`, `HttpEventHandler.cpp`, or `TlsAllocator.cpp`
- **THEN** each file's body SHALL be wrapped in `#ifdef ARDUINO` / `#endif` (or otherwise excluded under the `native` PlatformIO environment), so that `<esp_http_client.h>` and `<esp_heap_caps.h>` are never required to build the `native` test target

#### Scenario: RedirectScheme predicate remains reachable from native tests

- **WHEN** the native test suite includes `ota/http/RedirectScheme.h`
- **THEN** `OTA::Http::isSecureRedirectPrefix` SHALL be callable without dragging in `<esp_http_client.h>` and SHALL continue to accept absolute `https://` URLs, refuse absolute `http://` URLs, and accept relative paths (i.e. the predicate's behaviour is unchanged by the relocation)

#### Scenario: HttpEventHandler owns the redirect-location buffer

- **WHEN** `HttpEventHandler.cpp` captures a `Location` header during an HTTP event
- **THEN** the captured prefix SHALL be written to a buffer declared `extern` in `HttpEventHandler.h` and defined once in `HttpEventHandler.cpp`, and SHALL be readable by `HttpClient::openWithRedirects` through an accessor declared in `HttpEventHandler.h`

#### Scenario: mbedTLS allocator override lives in TlsAllocator.cpp

- **WHEN** the Arduino firmware links
- **THEN** the symbols `esp_mbedtls_mem_calloc` and `esp_mbedtls_mem_free` SHALL be provided by `TlsAllocator.cpp` and SHALL route allocations larger than 4096 bytes to PSRAM first (falling back to internal SRAM), matching the routing rule described in the file's comment

#### Scenario: HttpClient exposes a raw handle accessor for the reader

- **WHEN** `OTAUpdater` streams a JSON response body from an open `HttpClient`
- **THEN** it SHALL construct an `OTA::Http::HttpReader` from the client's `esp_http_client_handle_t` (obtained via an accessor on `HttpClient`) and pass that reader to `deserializeJson`
