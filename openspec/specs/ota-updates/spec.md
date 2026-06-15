# ota-updates Specification

## Purpose
TBD - created by archiving change baseline-capabilities. Update Purpose after archive.
## Requirements
### Requirement: GitHub-release-based update discovery

The firmware SHALL discover firmware updates by querying the GitHub REST API for the latest release of the configured `OTA_GITHUB_OWNER` / `OTA_GITHUB_REPO`. The current firmware version SHALL be exposed as `FIRMWARE_VERSION` and SHALL be compared against the latest release tag to determine whether an update is available.

#### Scenario: Newer release available

- **WHEN** the latest GitHub release tag is `v0.0.74` and the running firmware reports `v0.0.73`
- **THEN** `GET /api/ota/check` SHALL respond with `update_available: true`, `latest_version: "v0.0.74"`, and the asset download URL

#### Scenario: Already up to date

- **WHEN** the latest GitHub release tag matches the running `FIRMWARE_VERSION`
- **THEN** `GET /api/ota/check` SHALL respond with `update_available: false`

### Requirement: Memory safety guard

The firmware SHALL refuse to start an OTA update when free heap is below a safety threshold. `OTAUpdater::hasEnoughMemory()` SHALL encapsulate this check and SHALL require at least 64 KB of free heap before allowing a download.

#### Scenario: Insufficient memory

- **WHEN** `POST /api/ota/update` is called and free heap is below the 64 KB threshold
- **THEN** the firmware SHALL refuse the update with an HTTP 4xx response and SHALL NOT enter the OTA download path

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

The OTA system SHALL support automatic rollback via the `otadata` partition. After flashing, the new firmware SHALL boot in an unconfirmed state. The firmware SHALL expose `hasUnconfirmedUpdate()` and `confirmBoot()`. Until `confirmBoot()` is called, an unexpected reset SHALL cause the bootloader to roll back to the previous partition.

#### Scenario: Confirming a successful boot

- **WHEN** the new firmware reaches steady-state and `POST /api/ota/confirm` is sent
- **THEN** `confirmBoot()` SHALL be called and rollback SHALL be disabled for the current partition

#### Scenario: Crash before confirmation

- **WHEN** the unconfirmed firmware crashes or fails to boot cleanly
- **THEN** the next boot SHALL revert to the previously running partition

### Requirement: OTA task stack isolation

The firmware SHALL give the background check task (`otaCheckTask`) and the update worker task (`otaWorkerTask`) one statically-reserved FreeRTOS stack and one Task Control Block (TCB) each. The two tasks SHALL release their own buffers on exit, and the firmware SHALL NOT share a single static stack/TCB pair between them. The `updateInProgress` atomic flag SHALL still gate concurrent update workers, and the `xTaskCreateStatic` calls SHALL still use statically-reserved buffers (linker BSS) so that task creation cannot fail on a fragmented runtime internal-SRAM heap.

#### Scenario: Check task is created with its own stack and TCB

- **WHEN** `OTAUpdater::startBackgroundCheck()` calls `xTaskCreateStatic` to spawn the background check task
- **THEN** the call SHALL pass a buffer pair (stack + TCB) that is dedicated to the check task and is not shared with the update worker task

#### Scenario: Update worker is created with its own stack and TCB

- **WHEN** `OTAUpdater::startBackgroundUpdate()` calls `xTaskCreateStatic` to spawn the update worker task
- **THEN** the call SHALL pass a buffer pair (stack + TCB) that is dedicated to the update worker and is not shared with the check task

#### Scenario: Each task self-deletes on exit

- **WHEN** either the check task or the update worker task finishes its work (success or failure)
- **THEN** the task SHALL release its own stack/TCB by calling `vTaskDelete(nullptr)` as its final action, and no other task SHALL be responsible for reaping it

#### Scenario: No shared static stack/TCB remains in the firmware

- **WHEN** the source is read
- **THEN** there SHALL be no declaration of a single shared `otaTaskStack` array or `otaTaskTCB` `StaticTask_t` used by both task bodies, and the firmware SHALL declare a separate stack array and TCB per task

