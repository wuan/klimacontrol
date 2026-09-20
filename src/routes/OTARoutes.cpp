#include "WebServerManager.h"
#include "routes/RouteHelpers.h"

#include "Config.h"
#include "ota/OTAUpdater.h"
#include "ota/VersionCompare.h"
#include "Constants.h"
#include "ota/OTAConfig.h"

#ifdef ARDUINO
#include <ArduinoJson.h>
#include "Log.h"
#endif

static constexpr const char* const TAG = "ota";

void WebServerManager::setupOTARoutes() {
#ifdef ARDUINO
    // POST /api/ota/check - Start a background check for firmware updates.
    // The TLS round-trip to GitHub blocks, so it runs on a worker task to keep
    // the AsyncTCP event task free; clients poll GET /api/ota/check for the result.
    server.on("/api/ota/check", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!verifyCsrfHeader(request)) {
            return;
        }
        bool started = OTAUpdater::startBackgroundCheck(OTA_GITHUB_OWNER, OTA_GITHUB_REPO);

        JsonDocument doc;
        doc["status"] = started ? "checking" : "busy";

        String response;
        serializeJson(doc, response);
        request->send(started ? 202 : 409, CONTENT_TYPE_JSON, response);
    });

    // GET /api/ota/check - Poll the state of the background check.
    server.on("/api/ota/check", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["current_version"] = FIRMWARE_VERSION;

        FirmwareInfo info;
        switch (OTAUpdater::getCheckResult(info)) {
            case OTAUpdater::CheckState::Idle:
                doc["status"] = "idle";
                break;
            case OTAUpdater::CheckState::InProgress:
                doc["status"] = "checking";
                break;
            case OTAUpdater::CheckState::Done:
                doc["status"] = "done";
                // Ordering comparison, not textual inequality: an untagged
                // developer build reports "v1.2.3-4-gabc1234", which differs
                // from the v1.2.3 release but is not older than it. Comparing
                // with != offered that downgrade as an update.
                doc["update_available"] = OTAUpdater::isUpdateAvailable(info);
                doc["latest_version"] = info.version;
                doc["release_name"] = info.name;
                doc["size_bytes"] = info.size;
                // Semver-equal — the opt-in reinstall path is available. The
                // strict-newer path is covered by update_available above.
                {
                    int cmp = Support::compareVersions(FIRMWARE_VERSION, info.version.c_str());
                    bool semverEqual = (cmp == 0);
                    doc["can_reinstall"] = semverEqual;
                    // When semver-equal but the strings differ, the user is on
                    // a git-describe dev build and the latest is the matching
                    // tagged release. The UI uses this to label the action
                    // honestly (Promote dev build, not Reinstall).
                    doc["is_dev_build_promotion"] = semverEqual && strcmp(FIRMWARE_VERSION, info.version.c_str()) != 0;
                }
                // The download URL is deliberately NOT exposed: the device
                // updates only from its own checked result, so clients never
                // need it and cannot supply one.
                break;
            case OTAUpdater::CheckState::Failed:
                doc["status"] = "error";
                doc["update_available"] = false;
                doc["error"] = info.errorMessage.isEmpty() ? "Failed to check for updates" : info.errorMessage;
                break;
        }

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // POST /api/ota/update - Install the update found by the last check.
    //
    // The request carries no download URL by design: the device flashes only
    // the firmware identified by its own GitHub check (compiled-in owner/repo),
    // so a client cannot point it at an arbitrary binary. A successful check
    // must have run first.
    //
    // The optional JSON body field `allow_reinstall` opts the caller into the
    // semver-equal case (e.g. "Reinstall current version" or "Promote dev
    // build to the tagged release"). Absent or false preserves the default
    // strict-newer-only behaviour.
    //
    // startBackgroundUpdateFromLatestCheck() only spawns a worker (the actual
    // multi-minute download runs there), so it returns quickly and is safe to
    // call inline on the AsyncTCP event task.
    //
    // The three-arg form (onRequest / nullptr for onUpload / onBody) is the
    // ESPAsyncWebServer pattern for routes that need to read a small JSON
    // body: onBody is invoked as the body chunks arrive, and only when there
    // is a body — so the empty-body case keeps the default allow_reinstall=
    // false and matches today's behaviour.
    server.on(
        "/api/ota/update", HTTP_POST, []([[maybe_unused]] AsyncWebServerRequest* request) {}, nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, [[maybe_unused]] size_t total) {
            if (!verifyCsrfHeader(request)) {
                return;
            }
            // Only parse the first chunk — the body is small and we just need
            // the allow_reinstall flag.
            if (index != 0) {
                return;
            }

            bool allowReinstall = false;
            if (len > 0) {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, data, len);
                if (err) {
                    request->send(400, CONTENT_TYPE_JSON, R"({"status":"error","message":"Invalid JSON body"})");
                    return;
                }
                allowReinstall = doc["allow_reinstall"].as<bool>();
            }

            ESP_LOGI(TAG, "OTA update requested (allow_reinstall=%s)", allowReinstall ? "true" : "false");

            if (OTAUpdater::startBackgroundUpdateFromLatestCheck(this->config, allowReinstall)) {
                request->send(200, CONTENT_TYPE_JSON, R"({"status":"starting","message":"OTA update started"})");
            } else {
                ESP_LOGW(TAG,
                         "OTA update not started (no verified update, busy, pending verify, or task creation failed)");
                request->send(
                    409, CONTENT_TYPE_JSON,
                    R"({"status":"error","message":"No verified update available, or update already in progress"})");
            }
        });

    // GET /api/ota/update - Poll the state of a running/finished update.
    //
    // The download takes minutes and POST /api/ota/update returns as soon as
    // the worker is spawned, so this is the only way a client can learn the
    // progress or the outcome. Without it a failed update was invisible to the
    // UI: the browser was told "starting" and then simply waited for a device
    // that was never going to restart.
    server.on("/api/ota/update", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;

        // expected_bytes is the size carried by the last successful check; the
        // UI uses it to render a bytes-written / bytes-total counter alongside
        // the percent. Reported during Downloading, Pending, and Success; 0 on
        // Idle and Failed (the byte counter is hidden in those branches).
        FirmwareInfo checkInfo;
        size_t expectedBytes = 0;
        if (OTAUpdater::getCheckResult(checkInfo) == OTAUpdater::CheckState::Done && checkInfo.isValid) {
            expectedBytes = checkInfo.size;
        }

        int percent = 0;
        size_t bytes = 0;
        String error;
        switch (OTAUpdater::getUpdateProgress(percent, bytes, error)) {
            case OTAUpdater::UpdateState::Idle:
                doc["status"] = "idle";
                doc["expected_bytes"] = 0;
                break;
            case OTAUpdater::UpdateState::Downloading:
                doc["status"] = "downloading";
                doc["percent"] = percent;
                doc["bytes"] = bytes;
                doc["expected_bytes"] = expectedBytes;
                break;
            case OTAUpdater::UpdateState::Pending:
                doc["status"] = "pending";
                doc["percent"] = 100;
                doc["bytes"] = bytes;
                doc["expected_bytes"] = expectedBytes;
                break;
            case OTAUpdater::UpdateState::Success:
                doc["status"] = "success";
                doc["percent"] = 100;
                doc["bytes"] = bytes;
                doc["expected_bytes"] = expectedBytes;
                doc["message"] = "Update installed, device is restarting";
                break;
            case OTAUpdater::UpdateState::Failed:
                doc["status"] = "error";
                doc["percent"] = percent;
                doc["bytes"] = bytes;
                doc["expected_bytes"] = 0;
                doc["error"] = error.isEmpty() ? "Update failed" : error;
                break;
        }

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // GET /api/ota/status - Get OTA status
    server.on("/api/ota/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;

        doc["firmware_version"] = FIRMWARE_VERSION;
        doc["build_date"] = FIRMWARE_BUILD_DATE;
        doc["build_time"] = FIRMWARE_BUILD_TIME;

        // Partition info
        String partitionLabel;
        uint32_t partitionAddress;
        if (OTAUpdater::getRunningPartitionInfo(partitionLabel, partitionAddress)) {
            doc["partition"] = partitionLabel;
            doc["partition_address"] = partitionAddress;
        }

        // Check if running unconfirmed update
        doc["unconfirmed_update"] = OTAUpdater::hasUnconfirmedUpdate();

        // Memory info
        uint32_t freeHeap, minFreeHeap;
        OTAUpdater::getMemoryInfo(freeHeap, minFreeHeap);
        doc["free_heap"] = freeHeap;
        doc["min_free_heap"] = minFreeHeap;
        doc["ota_safe"] = OTAUpdater::hasEnoughMemory();

        // Consolidated check block — mirrors the standalone /api/ota/check
        // response but on the shorter names the consolidated poller needs.
        // The standalone endpoint remains available for callers that want the
        // full shape (current_version, latest_version, etc.).
        {
            JsonObject check = doc["check"].to<JsonObject>();
            FirmwareInfo checkInfo;
            switch (OTAUpdater::getCheckResult(checkInfo)) {
                case OTAUpdater::CheckState::Idle:
                    check["state"] = "idle";
                    break;
                case OTAUpdater::CheckState::InProgress:
                    check["state"] = "in_progress";
                    break;
                case OTAUpdater::CheckState::Done:
                    check["state"] = "done";
                    check["version"] = checkInfo.version;
                    check["size_bytes"] = checkInfo.size;
                    check["update_available"] = OTAUpdater::isUpdateAvailable(checkInfo);
                    {
                        int cmp = Support::compareVersions(FIRMWARE_VERSION, checkInfo.version.c_str());
                        bool semverEqual = (cmp == 0);
                        check["can_reinstall"] = semverEqual;
                        check["is_dev_build_promotion"] =
                            semverEqual && strcmp(FIRMWARE_VERSION, checkInfo.version.c_str()) != 0;
                    }
                    break;
                case OTAUpdater::CheckState::Failed:
                    check["state"] = "failed";
                    check["error"] =
                        checkInfo.errorMessage.isEmpty() ? "Failed to check for updates" : checkInfo.errorMessage;
                    break;
            }
        }

        // Consolidated update block — mirrors the standalone /api/ota/update
        // response (including the new Pending state and expected_bytes field).
        {
            JsonObject update = doc["update"].to<JsonObject>();

            FirmwareInfo checkInfo;
            size_t expectedBytes = 0;
            if (OTAUpdater::getCheckResult(checkInfo) == OTAUpdater::CheckState::Done && checkInfo.isValid) {
                expectedBytes = checkInfo.size;
            }

            int percent = 0;
            size_t bytes = 0;
            String error;
            switch (OTAUpdater::getUpdateProgress(percent, bytes, error)) {
                case OTAUpdater::UpdateState::Idle:
                    update["state"] = "idle";
                    break;
                case OTAUpdater::UpdateState::Downloading:
                    update["state"] = "downloading";
                    update["percent"] = percent;
                    update["bytes"] = bytes;
                    update["expected_bytes"] = expectedBytes;
                    break;
                case OTAUpdater::UpdateState::Pending:
                    update["state"] = "pending";
                    update["percent"] = 100;
                    update["bytes"] = bytes;
                    update["expected_bytes"] = expectedBytes;
                    break;
                case OTAUpdater::UpdateState::Success:
                    update["state"] = "success";
                    update["percent"] = 100;
                    update["bytes"] = bytes;
                    update["expected_bytes"] = expectedBytes;
                    break;
                case OTAUpdater::UpdateState::Failed:
                    update["state"] = "error";
                    update["error"] = error.isEmpty() ? "Update failed" : error;
                    break;
            }
        }

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // POST /api/ota/confirm - Confirm successful boot after OTA.
    //
    // Normally redundant: OTAUpdater::confirmRunningImage() already runs at the
    // end of setup(), so by the time this endpoint is reachable the image is
    // confirmed and GET /api/ota/status reports unconfirmed_update: false. Kept
    // as a manual escape hatch (and because the ota-updates spec requires it).
    server.on("/api/ota/confirm", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!verifyCsrfHeader(request)) {
            return;
        }
        bool success = OTAUpdater::confirmBoot();

        JsonDocument doc;
        doc[JSON_KEY_SUCCESS] = success;
        doc["message"] = success ? "Boot confirmed, rollback disabled" : "Failed to confirm boot";

        String response;
        serializeJson(doc, response);
        request->send(success ? 200 : 500, CONTENT_TYPE_JSON, response);
    });

    // GET /api/ota/rollback - Report the version that would boot after a
    // rollback to the other partition. Reads the image header without booting
    // from it; returns available: false when the header cannot be read
    // (factory-fresh device, failed prior flash, or invalid header).
    server.on("/api/ota/rollback", HTTP_GET, [](AsyncWebServerRequest* request) {
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

    // POST /api/ota/rollback - Switch the boot target to the other partition.
    // No download, no flash: the image on the other partition was already
    // verified when it was originally flashed. Refused while isUpdateInProgress()
    // or hasUnconfirmedUpdate() is true.
    server.on("/api/ota/rollback", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!verifyCsrfHeader(request)) {
            return;
        }
        if (OTAUpdater::rollbackToOtherPartition(this->config)) {
            request->send(200, CONTENT_TYPE_JSON,
                          R"json({"status":"starting","message":"Rolling back, device is restarting"})json");
        } else {
            request->send(
                409, CONTENT_TYPE_JSON,
                R"json({"status":"error","message":"Rollback refused (update in progress, unconfirmed update, or other slot empty)"})json");
        }
    });
#endif
}
