## Context

`OTAUpdater::startBackgroundUpdateFromLatestCheck` is the only entry
point that gates a firmware install (`OTAUpdater.cpp:470`). Its current
gate is a single comparison:

```cpp
if (!isUpdateAvailable(info)) {
    ESP_LOGW(TAG, "Update refused: %s is not newer than running %s",
             info.version.c_str(), FIRMWARE_VERSION);
    return false;
}
```

`isUpdateAvailable` returns true only when
`Support::isNewerVersion(FIRMWARE_VERSION, info.version)` returns 1,
i.e. the latest tag is strictly greater than the running tag under
semver (with suffix ignored). The corresponding requirement in
`openspec/specs/ota-updates/spec.md` makes this load-bearing: the
"Older release is never installed" scenario relies on the strict-greater
gate, as does "Untagged developer build is not offered a downgrade"
(because `v1.2.3-4-gabc1234` and `v1.2.3` compare as equal, so a
strictly-greater gate refuses both the downgrade *and* the silent
dev-build → tagged-release replacement that an opt-in reinstall path
would otherwise enable by default).

The rollback side is even simpler. `OTAUpdater::hasUnconfirmedUpdate()`
already reports whether the running image is still in
`ESP_OTA_IMG_PENDING_VERIFY` (`OTAUpdater.cpp:535`); the bootloader's
pending-verify mechanism switches boot target on the *next* reset
unless `confirmRunningImage()` is called. After confirmation, the only
way back to the other partition is `esp_ota_set_boot_partition()`,
which `OTAUpdater` does not currently expose.

## Goals / Non-Goals

**Goals:**

- Reinstall: a single opt-in flag (`allow_reinstall` on
  `POST /api/ota/update`) permits installing the latest release when
  it is semver-equal to the running version, with a textual
  disambiguation in the UI so dev-build users are not silently moved
  onto the tagged release under a "Reinstall current version" label.
- Reinstall: refuses while `hasUnconfirmedUpdate()` is true (the
  partition being written is the only remaining rollback target).
- Rollback: a new endpoint pair that flips the boot target to the
  *other* partition via `esp_ota_set_boot_partition()` and reports the
  version that would boot after the switch.
- Rollback: refuses while `hasUnconfirmedUpdate()` is true (the
  bootloader is about to do the same switch on the next reset) and
  while `OTAUpdater::isUpdateInProgress()` is true (the other
  partition may be in the middle of being written).
- Reinstall predicate is header-only inline so the native test
  environment reaches it without dragging in `<esp_ota_ops.h>` or
  `<esp_http_client.h>`.
- Spec amendments pin the new behaviour and qualify the existing
  "Untagged developer build is not offered a downgrade" scenario so
  the opt-in path does not contradict it.

**Non-Goals:**

- Listing arbitrary older releases on GitHub for a downgrade UI.
  The current scope is reinstall + boot-target switch; listing
  arbitrary older releases is a separate, larger change that would
  require changing the GitHub API call shape, adding a release-list
  response to `GET /api/ota/check`, and a UI dropdown with a
  per-version warning.
- Letting the client name the asset URL or release tag. The device
  still fetches its own metadata and never trusts a client-supplied
  URL — this is the existing stance (see
  `OTAUpdater::startBackgroundUpdateFromLatestCheck`'s
  `startsWith(OTA_GITHUB_RELEASE_HOST)` defence-in-depth) and is
  preserved.
- Changing the rollback target's *content*. The rollback endpoint
  only flips the boot pointer; it does not download, verify, or
  rewrite the other partition's image.
- Touching the `Activity` claim semantics. Both new endpoints refuse
  while a check or update is in progress, but they do not need to
  claim the activity slot themselves — they don't allocate TLS
  buffers and don't run for minutes.
- Adding OTA source files. No new translation units; the new methods
  live in the existing `OTAUpdater.{h,cpp}`.

## Decisions

### 1. Reinstall is a body field on POST /api/ota/update, not a new endpoint

**Choice.** Add an optional `allow_reinstall` field to the existing
`POST /api/ota/update` JSON body. The route handler reads it via the
same `onBody` callback pattern already used by `SettingsRoutes.cpp`.

**Rationale.** A separate `/api/ota/reinstall` endpoint would do
almost the same thing as `/api/ota/update` — start the same worker
against the same checked result — so a body field is the smaller
diff. It also keeps `OTAUpdater::startBackgroundUpdateFromLatestCheck`
as the single spawn point for a background update: the handler can
gate *which* versions are acceptable, but the spawn lifecycle
(parked-worker claim, `setUpdateState(Downloading)`, restart on
success) stays in one place.

**Alternative considered.** A separate `POST /api/ota/reinstall`
endpoint with no body. Symmetric in name with `/api/ota/update`, but
introduces a second route that does almost the same thing and risks
drift over time. Rejected.

### 2. Reinstall predicate is header-only inline in `VersionCompare.h`

**Choice.** Add `Support::isReinstallOrNewer(const char *current, const
char *available)` as a pure inline function in `src/ota/VersionCompare.h`.

**Rationale.** `VersionCompare.h` is already the home for the
ordering predicates the OTA subsystem consults, and is the only OTA
header that is `#ifdef ARDUINO`-free (it is included by the native
test). Putting the new predicate alongside `compareVersions` and
`isNewerVersion` means the existing native test
(`test_ota_updater.cpp`) gains coverage with a one-line include.

**Alternative considered.** A new header for the predicate. Rejected:
VersionCompare.h is small, the predicate is three lines, and the
existing file already has two other version predicates that share the
same `compareVersions` implementation.

### 3. Dev-build promotion is allowed; UI labels the action honestly

**Choice.** When the running version is a `git describe` build
(`v1.2.3-4-gabc1234`) and the latest release is the matching tagged
release (`v1.2.3`), the reinstall checkbox is presented, the device
performs the install when the user opts in, and the UI labels the
action as "Promote to v1.2.3 (replace dev build v1.2.3-4-gabc1234)"
rather than "Reinstall v1.2.3". The JSON response from
`GET /api/ota/check` distinguishes the two cases via a new
`is_dev_build_promotion` boolean so the UI does not have to repeat
the comparison.

**Rationale.** `compareVersions` already ignores the suffix, so the
spec's "Untagged developer build is not offered a downgrade"
scenario is what made the existing strict-greater gate refuse this
case — there was no separate dev-build guard. With the opt-in
reinstall path, refusing the dev-build case as well would force
developers to wait for the next release to get off a stale
intermediate commit, which is the exact friction the feature is
trying to remove. Letting the user choose, while making the choice
honest in the UI, is the right tradeoff.

**Alternative considered.** Refuse reinstall when running is a
dev build and latest is the matching tag. Cleaner from a "what does
'reinstall' mean" perspective, but punts the dev-build problem to
the user without a good answer (they have to wait for the next
release or hand-flash). Rejected.

### 4. Both endpoints refuse during PENDING_VERIFY

**Choice.** Both `POST /api/ota/update` (with `allow_reinstall=true`)
and `POST /api/ota/rollback` refuse when
`OTAUpdater::hasUnconfirmedUpdate()` returns true. The error message
is the same in both cases ("A pending update has not yet been
confirmed; reboot to roll back automatically, or wait for
confirmation to complete before reinstalling").

**Rationale.** The spec's `Boot rollback` requirement pins the
pending-verify mechanism precisely because it is the only thing
standing between a freshly flashed image and the bootloader
auto-reverting on the next reset. Overwriting the *other* partition
during that window would defeat the protection: the bootloader would
still revert on the next reset, but now to a partition holding
whichever firmware we just wrote, not the previously known-good one.
For rollback specifically, the bootloader is about to do the same
switch on the next reset anyway, so a manual call would race the
bootloader's decision. Refusing in both cases is the conservative
choice that keeps the existing safety guarantees intact.

**Alternative considered.** Allow reinstall during pending verify
(it's the user's call). Rejected: the protection is load-bearing
(see `OTAUpdater::confirmRunningImage`'s docstring for the rationale)
and the user can always let the device reboot once and retry.

### 5. Rollback methods on OTAUpdater do not claim the Activity slot

**Choice.** `rollbackToOtherPartition()` performs
`esp_ota_set_boot_partition()` and calls `config.requestRestart(1000)`
inline; it does not claim `activity`, does not run on a worker task,
and does not write to `pendingUpdate`. The route handler refuses if
`isUpdateInProgress()` returns true (i.e. another check or update
holds the slot) but does not need its own claim.

**Rationale.** The rollback action does not allocate TLS buffers and
does not run for minutes — it is a single SDK call plus a deferred
restart. The `Activity` claim's only purpose is to gate TLS-heavy
work (the spec's `Mutual exclusion of OTA activities` requirement
calls this out explicitly: the claim suppresses the network task's
low-heap restart guard). The rollback doesn't need that suppression
because it commits to a restart in 1 second anyway. Refusing when
*another* activity holds the slot is still right (writing the
partition while a download is in progress is unsafe), but the
rollback itself does not need to claim.

**Alternative considered.** Have rollback claim the slot too, with a
new `Activity::RollingBack` value. Rejected: the slot exists to gate
TLS-heavy work that takes minutes, and rollback does neither. Adding
a third value would force every `isUpdateInProgress()`-guarded
restart path to consult three cases instead of two for no benefit.

### 6. Rollback version discovery uses `esp_ota_get_partition_description`

**Choice.** `getOtherPartitionVersion(String &versionOut)` calls
`esp_ota_get_running_partition()` and
`esp_ota_get_next_update_partition()` to find the other partition,
then `esp_ota_get_partition_description(other, &desc)` to read its
image header. Returns false (and an empty string) when the header
cannot be read.

**Rationale.** `esp_ota_get_partition_description` is the
ESP-IDF-native way to read an image header from a specific partition
without booting from it. The `esp_app_desc_t.version` field is the
same string baked into the firmware at build time, so the value
returned to the UI is exactly what `FIRMWARE_VERSION` would have been
on that image. The error case (factory-fresh device, failed prior
flash, corrupted header) is reported as `available: false` rather
than a stringly-typed error code, which matches the existing
`GET /api/ota/check` style of "the field is missing when the
condition does not hold".

**Alternative considered.** Parse the image header manually.
Rejected: `esp_ota_get_partition_description` already validates the
header magic and checksum; a hand-rolled parser would either re-
implement that validation or quietly read garbage from a corrupt
partition.

## Risks / Trade-offs

- **Loss of rollback target on reinstall.** Reinstalling a
  release-equivalent version writes the same content to the *other*
  partition, which means both slots end up holding the same image.
  Until the next release, there is no rollback target. The UI
  warning box ("overwrites the other slot — no rollback target until
  next release") is the mitigation; the spec scenario pins the
  behaviour so the warning cannot be silently regressed away.

- **Dev-build promotion is one-way-ish.** Promoting `v1.2.3-4-gabc1234`
  to `v1.2.3` by reinstall puts the device on a release version that
  no longer carries the dev commits. If the user actually wanted
  `v1.2.3-5-gdef5678` (the next dev build), they would have to wait
  for it to be built or flash over USB. This is consistent with how
  every other "install from GitHub" path works, but worth being
  explicit about.

- **`esp_ota_get_partition_description` on a corrupt partition.**
  The function returns `ESP_ERR_INVALID_ARG` or
  `ESP_ERR_NOT_FOUND` for headers that fail validation; the wrapper
  translates that to `available: false` and the UI hides the
  rollback button. If both partitions end up holding the same
  release (post-reinstall), the other slot is still a valid image
  and rollback succeeds — but boots into the *same* firmware the
  user is already running, which is functionally a no-op. The UI
  should label the rollback button "Roll back to vX.Y.Z" rather than
  "Restore previous version" so this case is honest.

- **Rollback while a check is running.** The check itself does not
  touch the partition, but it does consume internal heap for TLS
  buffers. If the rollback fires `config.requestRestart(1000)` while
  a check is mid-handshake, the check is interrupted mid-flight. The
  route handler refuses on `isUpdateInProgress()`, so this case is
  prevented at the API surface; the worker just loses its in-flight
  TLS session when the device restarts, which is harmless.

- **POST /api/ota/update body parsing.** Today's handler has no
  body callback. Adding one means the route registration changes
  from a two-arg form to the three-arg form (`onRequest` for CSRF
  early-out, `onUpload` unused, `onBody` for parsing) that the
  other `SettingsRoutes` already use. The empty-body case still
  works: `onBody` is only called when there is a body, so the
  default `allow_reinstall=false` is correctly applied.

- **Spec amendment of an existing scenario.** The existing
  "Untagged developer build is not offered a downgrade" scenario
  currently asserts that `POST /api/ota/update` refuses when running
  is a dev build and latest is the matching tag. With the new
  opt-in, that assertion is only true *without* `allow_reinstall`.
  The scenario is qualified rather than deleted, so the existing
  default behaviour is still pinned; the new behaviour lives in a
  sibling scenario.

## Migration Plan

This is an additive change; no migration of persistent state is
involved.

1. Land the change in one commit. The Arduino and native builds
   both run from the same source tree, so a partial state during
   the change would not link.
2. Rollback is a single `git revert`; the change does not touch
   partition layout, persistent config, or build flags.
3. Existing clients (browsers with cached `settings.html`, scripts
   that call the API) see no change: `allow_reinstall` defaults to
   false, the new `can_reinstall` / `is_dev_build_promotion` fields
   are additive, and the new `/api/ota/rollback` endpoints are
   optional.

## Open Questions

(none — every decision above is locked in.)
