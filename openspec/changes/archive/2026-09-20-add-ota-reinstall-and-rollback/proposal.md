## Why

The OTA subsystem today only moves the device *forward*: `isUpdateAvailable`
requires a strictly newer release (`>` not `>=`), the existing spec
requirement `Older release is never installed` is enforced by both the API
and the OTA worker, and the only out-of-band escape hatch is the
bootloader's pending-verify rollback — which is spent the moment the user
confirms the new image. Two real situations cannot be addressed from the
UI today:

1. **Refresh the other slot.** The factory-fresh device shipped with
   `app0` flashed and `app1` empty (or, after a USB flash, with whatever
   the developer was last running). The user has no way to populate
   `app1` with the *same* version currently on `app0`, so the rollback
   target stays absent or stale until the next release.
2. **Roll back without rebooting.** Once the user has confirmed the new
   image, the pending-verify rollback mechanism is spent. If the new
   image turns out to have a problem the user didn't notice for a few
   days, there is no way to switch boot target back to the partition
   that's already on the device — they have to wait for the next release
   and flash forward over the bad image.

Both fixes are small, both reuse existing transport and worker
infrastructure, and both touch the same four files
(`OTAUpdater.{h,cpp}`, `routes/OTARoutes.cpp`, `data/settings.html`,
`openspec/specs/ota-updates/spec.md`). Bundling them avoids two
proposals with overlapping scope.

## What Changes

- **Reinstall current version.** A new `allow_reinstall` field on
  `POST /api/ota/update` opts in to installing the latest release when
  it is *semver-equal* (not strictly newer) to the running version.
  `compareVersions` ignores the `git describe` suffix, so this path
  also covers the developer-build case: running `v1.2.3-4-gabc1234`
  against a latest release of `v1.2.3` is a valid reinstall with the
  checkbox, and the UI labels the action as "promote to the tagged
  release" so the user is not misled. Reinstall is refused while
  `hasUnconfirmedUpdate()` is true: the partition being written is the
  only remaining rollback target, and overwriting it would defeat the
  existing spec's pending-verify protection.
- **Rollback to previous partition.** A new endpoint pair
  (`GET /api/ota/rollback` reports the version on the other slot;
  `POST /api/ota/rollback` performs the switch via
  `esp_ota_set_boot_partition()` and schedules a restart). No network,
  no download. Refused while `hasUnconfirmedUpdate()` is true (the
  bootloader is about to do the same switch on the next reset) and
  while `OTAUpdater::isUpdateInProgress()` is true (the other partition
  may be being written).
- **Spec deltas.** Amend `ota-updates`'s existing `GitHub-release-based
  update discovery` requirement and the `Untagged developer build is
  not offered a downgrade` scenario to acknowledge the new opt-in path;
  add three new requirements covering reinstall, the reinstall-during-
  pending-verify refusal, and rollback-to-previous-partition.
- **Web UI.** The settings page gains an "Advanced" section under the
  existing Firmware Updates card, with a "Roll back to previous"
  button (always visible) and a "Reinstall" / "Promote dev build"
  checkbox (visible only when the latest release is semver-equal to
  the running version).

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `ota-updates`: existing `GitHub-release-based update discovery`
  requirement amended to acknowledge the opt-in reinstall path; the
  existing `Untagged developer build is not offered a downgrade`
  scenario is qualified so the opt-in path does not violate it; three
  new requirements added (`Reinstall current version`,
  `Reinstall refuses during pending verify`,
  `Rollback to previous partition`).

## Impact

- **Source files touched:**
  - `src/ota/VersionCompare.h` — add an `isReinstallOrNewer` predicate
    so the gate is reachable from native tests.
  - `src/ota/OTAUpdater.h` — declare `getOtherPartitionVersion()`,
    `rollbackToOtherPartition()`, and the new
    `allowReinstall` parameter on
    `startBackgroundUpdateFromLatestCheck`.
  - `src/ota/OTAUpdater.cpp` — relax the gate, implement the two new
    methods, refuse both reinstall and rollback while
    `isUpdateInProgress()` or `hasUnconfirmedUpdate()` is true.
  - `src/routes/OTARoutes.cpp` — `POST /api/ota/update` gains an
    `onBody` callback that parses `{ "allow_reinstall": true }`;
    `GET /api/ota/check` adds `can_reinstall` and
    `is_dev_build_promotion` fields; new `GET` and
    `POST /api/ota/rollback` routes.
  - `data/settings.html` — Advanced section with the reinstall
    checkbox and the rollback button; updated `checkForUpdates` /
    `performUpdate` JS to read the new fields and POST the new flag.
- **Build:** no new dependencies; both environments (`adafruit_qtpy_esp32s2`,
  `native`) pick up the changes from existing source. The new predicate
  is header-only inline, so `native` covers it without dragging in
  `<esp_http_client.h>` or `<esp_ota_ops.h>`.
- **API surface additions:**
  - `POST /api/ota/update` body field: `{ "allow_reinstall": bool }`
    (optional; absent means "off", matching today's behaviour).
  - `GET /api/ota/check` adds `can_reinstall` and
    `is_dev_build_promotion` fields when the latest release equals
    the running version by semver.
  - `GET /api/ota/rollback` (new) returns
    `{ available, version, partition }`.
  - `POST /api/ota/rollback` (new) triggers the boot-target switch.
- **No behavioural change for existing clients.** A client that does
  not send a body, or sends a body without `allow_reinstall`, sees
  exactly today's behaviour (strict-newer-only).
- **No partition-table change.** The change writes to the same
  inactive partition the existing worker targets; rollback only flips
  the `otadata` boot pointer.
