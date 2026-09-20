## MODIFIED Requirements

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

## ADDED Requirements

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
