# ota-updates Specification

## Purpose
TBD - created by archiving change baseline-capabilities. Update Purpose after archive.
## Requirements
### Requirement: GitHub-release-based update discovery

The firmware SHALL discover firmware updates by querying the GitHub REST API for the latest release of the configured `OTA_GITHUB_OWNER` / `OTA_GITHUB_REPO`. The current firmware version SHALL be exposed as `FIRMWARE_VERSION`.

The comparison against the latest release tag SHALL be an ordering comparison of the `vMAJOR.MINOR.PATCH` components, not a textual inequality, and SHALL ignore any trailing suffix. An update SHALL be considered available only when the release is *strictly newer* than the running version. The same predicate SHALL gate both the reported `update_available` flag and the decision to flash.

The release asset holding the application image SHALL be identified by its exact name (`OTA_FIRMWARE_ASSET`, `firmware.bin`). The firmware SHALL NOT select an asset by `.bin` suffix.

#### Scenario: Newer release available

- **WHEN** the latest GitHub release tag is `v0.0.74` and the running firmware reports `v0.0.73`
- **THEN** `GET /api/ota/check` SHALL respond with `update_available: true` and `latest_version: "v0.0.74"`

#### Scenario: Already up to date

- **WHEN** the latest GitHub release tag matches the running `FIRMWARE_VERSION`
- **THEN** `GET /api/ota/check` SHALL respond with `update_available: false`

#### Scenario: Untagged developer build is not offered a downgrade

- **WHEN** the running firmware reports `v1.2.3-4-gabc1234` (a `git describe` build four commits past the v1.2.3 tag) and the latest release is `v1.2.3`
- **THEN** `GET /api/ota/check` SHALL respond with `update_available: false`, and `POST /api/ota/update` SHALL refuse the update

#### Scenario: Older release is never installed

- **WHEN** the latest release tag is older than the running `FIRMWARE_VERSION`
- **THEN** `POST /api/ota/update` SHALL refuse the update and SHALL NOT write to any partition

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
