## 1. Add the reinstall predicate to VersionCompare.h

- [x] 1.1 In `src/ota/VersionCompare.h`, add a header comment block
  that explains `isReinstallOrNewer` is the gate behind both the
  default strict-newer behaviour and the opt-in reinstall path. Add
  the function as a pure inline in `namespace Support`:

  ```cpp
  inline bool isReinstallOrNewer(const char *current,
                                  const char *available,
                                  bool allowReinstall) {
      int cmp = compareVersions(current, available);
      if (cmp > 0)  return false;          // strictly older
      if (cmp < 0)  return true;           // strictly newer
      return allowReinstall;               // semver-equal — gated by flag
  }
  ```

  The function lives in the same header as `compareVersions` /
  `isNewerVersion`, behind no `#ifdef ARDUINO`, so it is reachable
  from `native` tests without dragging in OTA headers.

## 2. Update OTAUpdater to use the new predicate and expose rollback

- [x] 2.1 In `src/ota/OTAUpdater.h`, add the new parameter to the
  public signature:

  ```cpp
  static bool startBackgroundUpdateFromLatestCheck(
      Config::ConfigManager &config,
      bool allowReinstall = false);
  ```

- [x] 2.2 In `src/ota/OTAUpdater.h`, declare two new public methods
  inside the `#ifdef ARDUINO` block (both need `<esp_ota_ops.h>`):

  ```cpp
  // Returns true when the other partition has a readable image
  // header. On success, `versionOut` is set to the header's
  // esp_app_desc_t::version string (e.g., "v0.0.73" or
  // "v0.0.73-4-gabc1234"). On failure, `versionOut` is empty.
  static bool getOtherPartitionVersion(String &versionOut);

  // Calls esp_ota_set_boot_partition() on the other partition and
  // schedules a restart. No-op on the network path. Returns false
  // when the other partition has no readable image header, when
  // OTAUpdater::isUpdateInProgress() is true, or when the SDK call
  // fails.
  static bool rollbackToOtherPartition(Config::ConfigManager &config);
  ```

- [x] 2.3 In `src/ota/OTAUpdater.cpp`, replace the existing gate in
  `startBackgroundUpdateFromLatestCheck`:

  ```cpp
  // Strict-greater default; semver-equal requires allow_reinstall.
  if (!Support::isReinstallOrNewer(FIRMWARE_VERSION,
                                   info.version.c_str(),
                                   allowReinstall)) {
      ESP_LOGW(TAG, "Update refused: %s is not newer than running %s",
               info.version.c_str(), FIRMWARE_VERSION);
      return false;
  }

  // Protect the rollback target the spec already preserves: an
  // unconfirmed update means the partition we'd overwrite is the
  // only one still pointing at the previously known-good image.
  if (hasUnconfirmedUpdate()) {
      ESP_LOGW(TAG, "Reinstall refused: a pending update has not "
                    "yet been confirmed");
      return false;
  }
  ```

- [x] 2.4 In `src/ota/OTAUpdater.cpp`, implement
  `getOtherPartitionVersion`:

  ```cpp
  bool OTAUpdater::getOtherPartitionVersion(String &versionOut) {
      versionOut = "";
      const esp_partition_t *running = esp_ota_get_running_partition();
      if (running == nullptr) return false;
      const esp_partition_t *other =
          esp_ota_get_next_update_partition(running);
      if (other == nullptr) return false;
      esp_app_desc_t desc;
      if (esp_ota_get_partition_description(other, &desc) != ESP_OK) {
          return false;
      }
      versionOut = String(desc.version);
      return true;
  }
  ```

- [x] 2.5 In `src/ota/OTAUpdater.cpp`, implement
  `rollbackToOtherPartition`:

  ```cpp
  bool OTAUpdater::rollbackToOtherPartition(Config::ConfigManager &config) {
      if (isUpdateInProgress()) {
          ESP_LOGW(TAG, "Rollback refused: an OTA check or update "
                        "is already running");
          return false;
      }
      if (hasUnconfirmedUpdate()) {
          ESP_LOGW(TAG, "Rollback refused: a pending update has not "
                        "yet been confirmed");
          return false;
      }
      const esp_partition_t *running = esp_ota_get_running_partition();
      if (running == nullptr) return false;
      const esp_partition_t *other =
          esp_ota_get_next_update_partition(running);
      if (other == nullptr) return false;
      esp_app_desc_t desc;
      if (esp_ota_get_partition_description(other, &desc) != ESP_OK) {
          ESP_LOGW(TAG, "Rollback refused: the other partition has no "
                        "readable image header");
          return false;
      }
      esp_err_t err = esp_ota_set_boot_partition(other);
      if (err != ESP_OK) {
          ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s",
                   esp_err_to_name(err));
          return false;
      }
      ESP_LOGI(TAG, "Rollback to %s (%s) scheduled; restarting",
               other->label, desc.version);
      config.requestRestart(1000);
      return true;
  }
  ```

## 3. Update OTARoutes to parse the body and add the rollback endpoints

- [x] 3.1 In `src/routes/OTARoutes.cpp`, change the
  `POST /api/ota/update` registration from the two-arg form
  (`onRequest`, no body) to the three-arg form (`onRequest`,
  `nullptr` for `onUpload`, body-callback for `onBody`). Match the
  pattern in `SettingsRoutes.cpp:20`. The body callback:

  - parses a `JsonDocument` with `deserializeJson`;
  - reads `doc["allow_reinstall"].as<bool>()` (default false);
  - calls `OTAUpdater::startBackgroundUpdateFromLatestCheck(this->config, allowReinstall)`;
  - sends the existing 200 / 409 response unchanged.

- [x] 3.2 In `src/routes/OTARoutes.cpp`, in the `GET /api/ota/check`
  `case OTAUpdater::CheckState::Done:` branch, add two new fields:

  ```cpp
  // Semver-equal — the opt-in reinstall path is available.
  int cmp = Support::compareVersions(FIRMWARE_VERSION,
                                     info.version.c_str());
  bool semverEqual = (cmp == 0);
  doc["can_reinstall"] = semverEqual;
  // When semver-equal but the strings differ, the user is on a
  // git-describe dev build and the latest is the matching tagged
  // release. The UI uses this to label the action honestly.
  doc["is_dev_build_promotion"] =
      semverEqual &&
      strcmp(FIRMWARE_VERSION, info.version.c_str()) != 0;
  ```

  Place the comparison after `doc["update_available"]` is set so the
  existing scenario ordering is preserved.

- [x] 3.3 In `src/routes/OTARoutes.cpp`, register
  `GET /api/ota/rollback`:

  ```cpp
  server.on("/api/ota/rollback", HTTP_GET, [](AsyncWebServerRequest *request) {
      JsonDocument doc;
      String version;
      if (OTAUpdater::getOtherPartitionVersion(version)) {
          doc["available"] = true;
          doc["version"] = version;
      } else {
          doc["available"] = false;
      }
      String response;
      serializeJson(doc, response);
      request->send(200, CONTENT_TYPE_JSON, response);
  });
  ```

- [x] 3.4 In `src/routes/OTARoutes.cpp`, register
  `POST /api/ota/rollback`:

  ```cpp
  server.on("/api/ota/rollback", HTTP_POST, [this](AsyncWebServerRequest *request) {
      if (!verifyCsrfHeader(request)) {
          return;
      }
      if (OTAUpdater::rollbackToOtherPartition(this->config)) {
          request->send(200, CONTENT_TYPE_JSON,
                        R"({"status":"starting","message":"Rolling back, device is restarting"})");
      } else {
          request->send(409, CONTENT_TYPE_JSON,
                        R"({"status":"error","message":"Rollback refused (update in progress, unconfirmed update, or other slot empty)"})");
      }
  });
  ```

  The handler is a capture-lambda that needs `this`, mirroring the
  existing `POST /api/ota/update` handler.

## 4. Update the settings page UI

- [x] 4.1 In `data/settings.html`, replace the existing
  `id="updateAvailable"` block (the box that appears when
  `update_available: true`) with a version that handles three
  states:

  - "Update Available!" + Install button (existing behaviour);
  - "Reinstall" / "Promote dev build" checkbox + button (only when
    `can_reinstall: true`);
  - hidden when neither applies.

  The label for the checkbox is selected on the client side from
  `data.is_dev_build_promotion`: "Reinstall firmware vX.Y.Z
  (overwrites the other slot — no rollback target until next
  release)" or "Promote dev build vA.B.C-d-gXXX to vX.Y.Z
  (overwrites the other slot)".

- [x] 4.2 In `data/settings.html`, add an "Advanced" subsection
  inside the Firmware Updates card, below the existing update flow.
  It contains:

  - a `<div id="rollbackSection">` populated by
    `GET /api/ota/rollback` showing "Other partition: vX.Y.Z
    (app1)" when available, hidden otherwise;
  - a `[Roll back to previous]` button that POSTs to
    `/api/ota/rollback` after a `confirm()` prompt;
  - the reinstall checkbox (above) when applicable;
  - a warning box that explains the loss-of-rollback-target
    consequence, present whenever either action is shown.

- [x] 4.3 In `data/settings.html`, update `checkForUpdates()` to
  call `loadRollbackInfo()` (which populates the Advanced section)
  on success, and update `performUpdate()` to accept an
  `options` object so the reinstall path can POST
  `{ allow_reinstall: true }` instead of an empty body.

## 5. Update native tests

- [x] 5.1 In `test/test_ota_updater/test_ota_updater.cpp`, add
  scenarios for `Support::isReinstallOrNewer`:

  - strict-newer with `allowReinstall=false` → true;
  - strict-newer with `allowReinstall=true` → true;
  - strict-older with `allowReinstall=false` → false;
  - strict-older with `allowReinstall=true` → false;
  - semver-equal (clean release == clean release) with
    `allowReinstall=false` → false;
  - semver-equal (clean release == clean release) with
    `allowReinstall=true` → true;
  - dev build vs tagged release (`v1.2.3-4-gabc1234` vs `v1.2.3`)
    with `allowReinstall=false` → false;
  - dev build vs tagged release with `allowReinstall=true` → true.

  Each scenario is a `TEST_CASE` with `SECTION`s, matching the
  existing test layout.

## 6. Update the ota-updates spec

- [x] 6.1 In `openspec/changes/add-ota-reinstall-and-rollback/specs/ota-updates/spec.md`,
  under `## MODIFIED Requirements`, amend the existing
  `### Requirement: GitHub-release-based update discovery` body so
  that the gate sentence becomes:

  > An update SHALL be considered available only when the release is
  > *strictly newer* than the running version. The same predicate
  > SHALL gate the reported `update_available` flag. The decision to
  > flash SHALL additionally allow the *semver-equal* case when the
  > request carries `allow_reinstall=true`; the corresponding flag on
  > the check response is `can_reinstall`.

- [x] 6.2 In the same spec delta, under `## MODIFIED Requirements`,
  add a qualifier to the existing
  `#### Scenario: Untagged developer build is not offered a downgrade`:

  > - **THEN** `GET /api/ota/check` SHALL respond with
  >   `update_available: false`, AND `POST /api/ota/update` SHALL
  >   refuse the update (a body carrying `allow_reinstall: true` MAY
  >   promote the dev build to the tagged release; see "Reinstall
  >   current version")

- [x] 6.3 Under `## ADDED Requirements`, add three new requirements
  with their scenarios:

  - `### Requirement: Reinstall current version` — covers the
    semver-equal case with `allow_reinstall=true`, the dev-build
    promotion case, the strict-older refusal, and the
    string-distinct-but-semver-equal case (e.g.,
    `v1.2.3-4-gabc1234` vs `v1.2.3`).
  - `### Requirement: Reinstall refuses during pending verify` —
    covers the `hasUnconfirmedUpdate()` refusal for the reinstall
    path.
  - `### Requirement: Rollback to previous partition` — covers
    `GET /api/ota/rollback` reporting the version on the other
    partition, `POST /api/ota/rollback` performing the switch,
    refusal during `isUpdateInProgress()` or
    `hasUnconfirmedUpdate()`, and the empty/invalid-header case
    where `available: false` is returned.

## 7. Verify

- [x] 7.1 Run `openspec validate --all --strict` (or
  `scripts/validate-openspec.sh`) and confirm a clean validation
  of the new change and the modified `ota-updates` spec.
- [x] 7.2 Run `pio run -e adafruit_qtpy_esp32s2` and confirm a
  clean build.
- [x] 7.3 Run `pio test -e native` and confirm the new
  `isReinstallOrNewer` scenarios pass alongside the existing
  `test_ota_updater` and `test_ota_transport` cases.
- [x] 7.4 Spot-check that no `git grep -n 'POST /api/ota/update.*URL\|client.*downloadUrl'`
  matches remain in the source tree — the existing "no client URL"
  stance must still hold.
