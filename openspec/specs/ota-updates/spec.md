# ota-updates Specification

## Purpose
TBD - created by archiving change baseline-capabilities. Update Purpose after archive.
## Requirements
### Requirement: GitHub-release-based update discovery

The firmware SHALL discover firmware updates by querying the GitHub REST API for the latest release of the configured `OTA_GITHUB_OWNER` / `OTA_GITHUB_REPO`. The current firmware version SHALL be exposed as `FIRMWARE_VERSION`.

The comparison against the latest release tag SHALL be an ordering comparison of the `vMAJOR.MINOR.PATCH` components, not a textual inequality, and SHALL ignore any trailing suffix. An update SHALL be considered available only when the release is *strictly newer* than the running version; the same predicate SHALL gate the reported `update_available` flag. The decision to flash SHALL additionally allow the *semver-equal* case when the request carries `allow_reinstall=true`; the corresponding flag on the check response is `can_reinstall` (true when `compareVersions` returns 0 and the running firmware is not a confirmed update). When `can_reinstall` is true because the running firmware is a `git describe` dev build and the latest is the matching tagged release, the response SHALL also carry `is_dev_build_promotion: true` so the UI can label the action honestly.

The release asset holding the application image SHALL be identified by its exact name (`OTA_FIRMWARE_ASSET`, `firmware.bin`). The firmware SHALL NOT select an asset by `.bin` suffix.

#### Scenario: Newer release available

- **WHEN** the latest GitHub release tag is `v0.0.74` and the running firmware reports `v0.0.73`
- **THEN** `GET /api/ota/check` SHALL respond with `update_available: true` and `latest_version: "v0.0.74"`

#### Scenario: Already up to date

- **WHEN** the latest GitHub release tag matches the running `FIRMWARE_VERSION`
- **THEN** `GET /api/ota/check` SHALL respond with `update_available: false` and `can_reinstall: true`

#### Scenario: Untagged developer build is not offered a downgrade

- **WHEN** the running firmware reports `v1.2.3-4-gabc1234` (a `git describe` build four commits past the v1.2.3 tag) and the latest release is `v1.2.3`
- **THEN** `GET /api/ota/check` SHALL respond with `update_available: false` and `is_dev_build_promotion: true`, AND `POST /api/ota/update` SHALL refuse the update unless the body carries `allow_reinstall: true` (see "Reinstall current version")

#### Scenario: Older release is never installed

- **WHEN** the latest release tag is older than the running `FIRMWARE_VERSION`
- **THEN** `POST /api/ota/update` SHALL refuse the update and SHALL NOT write to any partition, including when `allow_reinstall: true` is set (semver-equal is the only case the flag unlocks)

#### Scenario: Release without the expected asset

- **WHEN** the latest release contains `littlefs.bin` and `bootloader.bin` but no `firmware.bin`
- **THEN** the check SHALL fail with an error naming the missing asset, and no download SHALL be attempted

### Requirement: Memory safety guard

The firmware SHALL refuse to start an OTA update when free memory is below a safety threshold. `OTAUpdater::hasEnoughMemory()` SHALL encapsulate this check.

The check SHALL be made against *internal* SRAM only (`MALLOC_CAP_INTERNAL`), and SHALL require both a minimum total free internal heap and a minimum largest contiguous free internal block. It SHALL NOT use `esp_get_free_heap_size()`: with `CONFIG_SPIRAM_USE_MALLOC=y` and `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=0` that figure includes PSRAM, so a threshold expressed against it passes essentially unconditionally while the allocations OTA actually needs — the mbedTLS working set, lwIP/socket structures, DMA buffers, task stacks — are internal-only.

The network task's low-heap restart guard SHALL likewise measure internal SRAM.

#### Scenario: Insufficient internal memory

- **WHEN** an OTA download is about to start and free internal heap is below the threshold, or the largest free internal block is too small
- **THEN** the firmware SHALL refuse the update, SHALL log both figures, and SHALL NOT enter the OTA download path

#### Scenario: PSRAM does not mask internal exhaustion

- **WHEN** internal SRAM is nearly exhausted but megabytes of PSRAM remain free
- **THEN** `hasEnoughMemory()` SHALL return false

### Requirement: Streamed download with TLS verification

The firmware SHALL download firmware binaries over HTTPS using the bundled root certificates (`esp_crt_bundle`). The download SHALL be streamed in chunks of 4 KB into the inactive OTA partition; the firmware SHALL verify that the downloaded byte count matches the expected size before finalizing the partition. A progress callback SHALL be invoked during the download.

#### Scenario: Size mismatch

- **WHEN** the download completes but the byte count differs from the expected size
- **THEN** the OTA write SHALL be aborted, the inactive partition SHALL NOT be marked bootable, and an error SHALL be logged

#### Scenario: TLS handshake failure

- **WHEN** the HTTPS connection fails certificate validation
- **THEN** the download SHALL be aborted without any partition write

### Requirement: Partition management

The firmware SHALL use the ESP32 OTA partition API to identify the running and inactive partitions. The partition layout SHALL allocate two app partitions of 1856 KB each (`app0`, `app1`) plus an 8 KB `otadata` partition that stores the OTA state.

#### Scenario: Partition selection

- **WHEN** an OTA write begins
- **THEN** the write SHALL target the partition identified as inactive by `esp_ota_get_next_update_partition`

### Requirement: Boot rollback

The OTA system SHALL support automatic rollback via the `otadata` partition. After flashing, the new firmware SHALL boot in an unconfirmed state. The firmware SHALL expose `hasUnconfirmedUpdate()`, `confirmBoot()`, and `confirmRunningImage()`.

`hasUnconfirmedUpdate()` SHALL test for `ESP_OTA_IMG_PENDING_VERIFY`, not `ESP_OTA_IMG_NEW`. `ESP_OTA_IMG_NEW` exists only between `esp_ota_set_boot_partition()` and the next boot — the bootloader promotes it to `ESP_OTA_IMG_PENDING_VERIFY` before handing control to the application — so a running image can never observe itself as `NEW`.

The firmware SHALL confirm its own image automatically. `confirmRunningImage()` SHALL be called at the *end* of `setup()`, after the network and sensor tasks have started: an image that crashes during initialization must still be rolled back (which requires it to remain unconfirmed), while every later restart must not roll back. Confirmation SHALL NOT depend on a client request, because the device restarts itself on several paths (low-heap guard, WiFi force-restart, watchdog panic, power cycle) and any one of them would otherwise revert a working update.

#### Scenario: Successful update survives a later restart

- **WHEN** an update is installed, the device boots the new image successfully, and the device later restarts for any reason
- **THEN** the device SHALL still be running the new firmware

#### Scenario: Crash before confirmation

- **WHEN** the newly flashed firmware crashes during `setup()`, before `confirmRunningImage()` runs
- **THEN** the next boot SHALL revert to the previously running partition

#### Scenario: Manual confirmation remains available

- **WHEN** `POST /api/ota/confirm` is sent
- **THEN** `confirmBoot()` SHALL be called and SHALL succeed, whether or not the image was already confirmed

### Requirement: Reinstall current version

When `GET /api/ota/check` reports `can_reinstall: true`, `POST /api/ota/update` MAY install the same release again when the request body carries `allow_reinstall: true`. The release SHALL be fetched by the device itself from its own GitHub check result — never from a client-supplied URL — and SHALL be flashed to the inactive OTA partition.

The reinstall path is gated by `Support::isReinstallOrNewer`, which returns true when `compareVersions(running, available) > 0` (strictly newer — proceeds regardless of the flag), false when `< 0` (strictly older — proceeds regardless of the flag), and equal to `allow_reinstall` when the comparison returns 0 (semver-equal — gated by the flag). `compareVersions` already ignores the `git describe` suffix, so a running `v1.2.3-4-gabc1234` against a latest `v1.2.3` is a semver-equal case and reinstall is permitted with the flag.

#### Scenario: Reinstall with strict text match

- **WHEN** the running firmware is `v0.0.74` and the latest release is `v0.0.74` (strict string equality), and `POST /api/ota/update` is sent with body `{"allow_reinstall": true}`
- **THEN** the update SHALL proceed and SHALL flash the `v0.0.74` asset to the inactive partition

#### Scenario: Reinstall promotes dev build to tagged release

- **WHEN** the running firmware is `v1.2.3-4-gabc1234` (a `git describe` build) and the latest release is `v1.2.3`, and `POST /api/ota/update` is sent with body `{"allow_reinstall": true}`
- **THEN** the update SHALL proceed, the running partition SHALL be replaced with the tagged `v1.2.3` release on the next boot, and the UI SHALL have labelled the action as a dev-build promotion rather than a reinstall (because `is_dev_build_promotion` was true on the check response)

#### Scenario: Reinstall refused without opt-in

- **WHEN** the latest release equals the running version by semver and `POST /api/ota/update` is sent with no body, or with `{"allow_reinstall": false}`
- **THEN** the firmware SHALL refuse the update, SHALL NOT write to any partition, and SHALL log that `allow_reinstall` was not set

#### Scenario: Strictly older release is never reinstalled

- **WHEN** the running firmware is `v0.0.74` and the latest release is `v0.0.73`, and `POST /api/ota/update` is sent with body `{"allow_reinstall": true}`
- **THEN** the firmware SHALL refuse the update — the `allow_reinstall` flag unlocks only the semver-equal case

### Requirement: Reinstall refuses during pending verify

The firmware SHALL refuse `POST /api/ota/update` with `allow_reinstall: true` while `OTAUpdater::hasUnconfirmedUpdate()` is true. The partition being written by the reinstall is the only remaining rollback target the spec's `Boot rollback` requirement preserves — overwriting it would defeat the pending-verify protection and could leave the device with both partitions pointing at the same (now-confirmed) image and no path back to a known-good boot.

#### Scenario: Reinstall refused during pending verify

- **WHEN** `POST /api/ota/update` arrives with body `{"allow_reinstall": true}` while the running firmware is still `ESP_OTA_IMG_PENDING_VERIFY`
- **THEN** the firmware SHALL refuse the update and SHALL NOT write to any partition

### Requirement: Rollback to previous partition

The firmware SHALL expose a rollback action that flips the boot target to the non-running partition via `esp_ota_set_boot_partition()` and schedules a restart. The action SHALL NOT download any firmware — the image on the non-running partition was already verified when it was originally flashed, so the action is a pure boot-target switch.

`GET /api/ota/rollback` SHALL report the version found in the other partition's image header by calling `esp_ota_get_partition_description()` on the partition returned by `esp_ota_get_next_update_partition()`. When the header cannot be read (factory-fresh device with an empty slot, failed prior flash, or a header that fails the SDK's validation), the response SHALL be `available: false` with no `version` field.

`POST /api/ota/rollback` SHALL perform the switch and SHALL be refused while `OTAUpdater::isUpdateInProgress()` is true (the other partition may be in the middle of being written) and while `OTAUpdater::hasUnconfirmedUpdate()` is true (the bootloader is about to perform the same switch on the next reset, and a manual call would race the bootloader's decision).

#### Scenario: GET reports the version on the other partition

- **WHEN** the running firmware is `v0.0.74` and the other partition's image header reports `v0.0.73`
- **THEN** `GET /api/ota/rollback` SHALL respond with `available: true` and `version: "v0.0.73"`

#### Scenario: GET reports unavailable when the other slot is empty

- **WHEN** the non-running partition has no readable image header (factory-fresh device, or a failed prior flash)
- **THEN** `GET /api/ota/rollback` SHALL respond with `available: false`

#### Scenario: POST switches the boot target

- **WHEN** the running firmware is confirmed (`hasUnconfirmedUpdate()` is false), no check or update is running (`isUpdateInProgress()` is false), and the other partition has a readable image header
- **THEN** `POST /api/ota/rollback` SHALL call `esp_ota_set_boot_partition()` on the other partition, SHALL schedule a restart, and SHALL respond with `status: "starting"`

#### Scenario: Rollback refused during pending verify

- **WHEN** `POST /api/ota/rollback` arrives while `OTAUpdater::hasUnconfirmedUpdate()` is true
- **THEN** the firmware SHALL refuse the switch and SHALL respond with a 409

#### Scenario: Rollback refused during an in-progress check or update

- **WHEN** `POST /api/ota/rollback` arrives while `OTAUpdater::isUpdateInProgress()` is true
- **THEN** the firmware SHALL refuse the switch and SHALL respond with a 409

### Requirement: OTA task lifecycle

The firmware SHALL create the background check task (`otaCheckTask`) and the update worker task (`otaWorkerTask`) exactly once, from `OTAUpdater::begin()` during `setup()` and before the web server can accept requests. Each task SHALL have its own statically-reserved FreeRTOS stack and Task Control Block (TCB) in linker BSS, created via `xTaskCreateStatic` so that task creation cannot fail on a fragmented runtime internal-SRAM heap. The firmware SHALL NOT share a single static stack/TCB pair between the two tasks.

Both tasks SHALL then block indefinitely on a task notification and SHALL NOT be deleted. The firmware SHALL NOT call `vTaskDelete()` on either task, and SHALL NOT recreate a task on a stack/TCB pair that a previous incarnation used: `vTaskDelete()` only queues a task for reclamation by the idle task, so reinitializing its `StaticTask_t` before idle has run inserts a TCB into the ready list while it is still linked into `xTasksWaitingTermination`, corrupting the scheduler's lists.

Work SHALL be dispatched to a parked task by writing a job payload and then signalling the task with `xTaskNotifyGive`.

#### Scenario: Each task is created once with its own stack and TCB

- **WHEN** `OTAUpdater::begin()` runs
- **THEN** it SHALL make one `xTaskCreateStatic` call per task, each passing a buffer pair (stack + TCB) dedicated to that task, and subsequent `/api/ota/*` requests SHALL create no further tasks

#### Scenario: Repeated requests reuse the parked tasks

- **WHEN** a check or update completes and another is requested immediately afterwards
- **THEN** the request SHALL be dispatched to the existing parked task by notification, and no task SHALL be created or deleted

#### Scenario: Retry after a failed update is safe

- **WHEN** an update fails and a client immediately retries `POST /api/ota/update`
- **THEN** the retry SHALL either be accepted by the parked worker or refused as busy, and SHALL NOT corrupt FreeRTOS task state

#### Scenario: No task deletion remains in the firmware

- **WHEN** the source is read
- **THEN** there SHALL be no `vTaskDelete()` call in the OTA implementation, and no declaration of a shared `otaTaskStack` array or `otaTaskTCB` `StaticTask_t` used by both task bodies

### Requirement: Mutual exclusion of OTA activities

A background check and a background update SHALL be mutually exclusive, and SHALL be made so by a single atomic state variable. Every transition out of the idle state SHALL be a single compare-exchange, so that a check and an update cannot both pass their guard.

The firmware SHALL NOT gate the two activities on separate flags held in different synchronization domains: a check that read an update flag under a mutex while the update claimed it with a compare-exchange allowed both to start, after which the check's exit cleared the update's claim — re-arming the network task's low-heap restart guard mid-flash and allowing a third request to spawn a concurrent worker.

#### Scenario: Update requested while a check is running

- **WHEN** `POST /api/ota/update` arrives while a background check is in progress
- **THEN** the update SHALL be refused as busy, and the running check SHALL be unaffected

#### Scenario: Check requested while an update is running

- **WHEN** `POST /api/ota/check` arrives while a background update is in progress
- **THEN** the check SHALL be refused as busy, and the update's claim SHALL remain held for the whole download

#### Scenario: Low-heap guard stays suppressed for the whole update

- **WHEN** an update is downloading
- **THEN** `isUpdateInProgress()` SHALL report true continuously until the worker finishes, so the network task's low-heap guard cannot restart the device mid-flash

### Requirement: Update progress and outcome reporting

The firmware SHALL expose the state of a background update so that a client can observe progress and learn the outcome. `POST /api/ota/update` returns as soon as the worker is dispatched, so the outcome SHALL NOT be reported only to the log.

`GET /api/ota/update` SHALL report a status of `idle`, `downloading`, `success`, or `error`; SHALL include `percent` and `bytes` while downloading and on success; and SHALL include a human-readable `error` message on failure.

#### Scenario: Progress during download

- **WHEN** a download is in progress
- **THEN** `GET /api/ota/update` SHALL respond with `status: "downloading"` and a `percent` value that advances

#### Scenario: Failure is reported to the client

- **WHEN** the download aborts (connection lost, size mismatch, flash write failure, or insufficient memory)
- **THEN** `GET /api/ota/update` SHALL respond with `status: "error"` and an `error` message describing the cause, and the device SHALL continue running the previous firmware

#### Scenario: Success is reported before the restart

- **WHEN** the flash completes successfully
- **THEN** `GET /api/ota/update` SHALL respond with `status: "success"` before the scheduled restart takes effect

### Requirement: Redirect transport enforcement

The firmware SHALL follow HTTP redirects during a check or download only when the target preserves TLS. An absolute `Location` header SHALL be required to use the `https` scheme; a relative `Location` SHALL be accepted because it inherits the current request's scheme. The compiled-in host allowlist covers only the first hop, so the transport check SHALL apply to every subsequent hop — including the CDN hop that carries the firmware image.

#### Scenario: Cleartext redirect is refused

- **WHEN** a redirect response carries `Location: http://...`
- **THEN** the redirect SHALL NOT be followed and the operation SHALL fail

#### Scenario: GitHub CDN redirect is followed

- **WHEN** `github.com` responds 302 with an `https://` release-assets CDN URL
- **THEN** the redirect SHALL be followed and the download SHALL proceed

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

### Requirement: Transport predicates are covered by native tests

The deterministic predicates that gate the OTA transport's observable behavior — version comparison, redirect-scheme classification, GitHub URL composition, GitHub host-allowlist prefix match, and asset-name strict match — SHALL each be exercised by at least one test case in the native PlatformIO test environment (`pio test -e native`).

Each predicate SHALL be reachable from a native test without dragging in `<esp_http_client.h>`. Concretely, predicates that are not currently exposed outside the `#ifdef ARDUINO`-guarded transport code SHALL be moved into a header-only `inline` function in `src/support/` (or a comparable native-buildable location) so they can be included by the test.

#### Scenario: Version comparison rejects downgrades

- **WHEN** the native test suite runs with a running version of `v0.0.74` and an available tag of `v0.0.73`
- **THEN** `Support::isNewerVersion` SHALL return false

#### Scenario: Version comparison accepts unsuffixed dev builds as equal

- **WHEN** the native test suite runs with a running version of `v1.2.3-4-gabc1234` (a `git describe` build four commits past the v1.2.3 tag) and an available tag of `v1.2.3`
- **THEN** `Support::isNewerVersion` SHALL return false

#### Scenario: Version comparison returns 0 on unparseable input

- **WHEN** the native test suite runs with either version not matching `v%d.%d.%d`
- **THEN** `Support::compareVersions` SHALL return 0 (so the caller refuses the update)

#### Scenario: Redirect to absolute https is accepted

- **WHEN** the native test suite runs with a captured `Location` header of `https://release-assets.githubusercontent.com/...`
- **THEN** the redirect-scheme classifier SHALL return true

#### Scenario: Redirect to absolute http is refused

- **WHEN** the native test suite runs with a captured `Location` header of `http://example.com/firmware.bin`
- **THEN** the redirect-scheme classifier SHALL return false

#### Scenario: Relative redirect is accepted

- **WHEN** the native test suite runs with a captured `Location` header of `/path/to/firmware.bin` (relative, inherits the request's scheme)
- **THEN** the redirect-scheme classifier SHALL return true

#### Scenario: GitHub API URL is composed correctly

- **WHEN** the native test suite runs with an owner of `wuan` and a repo of `klimacontrol`
- **THEN** the composed check URL SHALL equal `https://api.github.com/repos/wuan/klimacontrol/releases/latest`

#### Scenario: GitHub host allowlist accepts github.com downloads

- **WHEN** the native test suite runs with a download URL of `https://github.com/wuan/klimacontrol/releases/download/v0.0.74/firmware.bin`
- **THEN** the host-allowlist prefix check SHALL return true

#### Scenario: GitHub host allowlist rejects non-github.com downloads

- **WHEN** the native test suite runs with a download URL of `https://example.com/firmware.bin`
- **THEN** the host-allowlist prefix check SHALL return false

#### Scenario: Asset-name strict match accepts firmware.bin

- **WHEN** the native test suite runs with an asset name of `firmware.bin`
- **THEN** the asset-name strict match SHALL return true

#### Scenario: Asset-name strict match rejects similarly-named assets

- **WHEN** the native test suite runs with an asset name of `littlefs.bin`, `bootloader.bin`, `ota-firmware.bin`, or `firmware.bin.sha256`
- **THEN** the asset-name strict match SHALL return false

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

