#include "WebServerManager.h"
#include "routes/RouteHelpers.h"

#include "Config.h"
#include "OTAUpdater.h"
#include "Constants.h"
#include "OTAConfig.h"

#ifdef ARDUINO
#include <ArduinoJson.h>
#include "Log.h"
#endif

static const char* TAG = "ota";

void WebServerManager::setupOTARoutes() {
#ifdef ARDUINO
    // POST /api/ota/check - Start a background check for firmware updates.
    // The TLS round-trip to GitHub blocks, so it runs on a worker task to keep
    // the AsyncTCP event task free; clients poll GET /api/ota/check for the result.
    server.on("/api/ota/check", HTTP_POST, [](AsyncWebServerRequest *request) {
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
    server.on("/api/ota/check", HTTP_GET, [](AsyncWebServerRequest *request) {
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
    // startBackgroundUpdateFromLatestCheck() only spawns a worker (the actual
    // multi-minute download runs there), so it returns quickly and is safe to
    // call inline on the AsyncTCP event task.
    server.on("/api/ota/update", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!verifyCsrfHeader(request)) {
            return;
        }
        ESP_LOGI(TAG, "OTA update requested");

        if (OTAUpdater::startBackgroundUpdateFromLatestCheck(this->config)) {
            request->send(200, CONTENT_TYPE_JSON,
                          R"({"status":"starting","message":"OTA update started"})");
        } else {
            ESP_LOGW(TAG, "OTA update not started (no verified update, busy, or task creation failed)");
            request->send(409, CONTENT_TYPE_JSON,
                          R"({"status":"error","message":"No verified update available or update already in progress"})");
        }
    });

    // GET /api/ota/update - Poll the state of a running/finished update.
    //
    // The download takes minutes and POST /api/ota/update returns as soon as
    // the worker is spawned, so this is the only way a client can learn the
    // progress or the outcome. Without it a failed update was invisible to the
    // UI: the browser was told "starting" and then simply waited for a device
    // that was never going to restart.
    server.on("/api/ota/update", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;

        int percent = 0;
        size_t bytes = 0;
        String error;
        switch (OTAUpdater::getUpdateProgress(percent, bytes, error)) {
            case OTAUpdater::UpdateState::Idle:
                doc["status"] = "idle";
                break;
            case OTAUpdater::UpdateState::Downloading:
                doc["status"] = "downloading";
                doc["percent"] = percent;
                doc["bytes"] = bytes;
                break;
            case OTAUpdater::UpdateState::Success:
                doc["status"] = "success";
                doc["percent"] = 100;
                doc["bytes"] = bytes;
                doc["message"] = "Update installed, device is restarting";
                break;
            case OTAUpdater::UpdateState::Failed:
                doc["status"] = "error";
                doc["percent"] = percent;
                doc["bytes"] = bytes;
                doc["error"] = error.isEmpty() ? "Update failed" : error;
                break;
        }

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // GET /api/ota/status - Get OTA status
    server.on("/api/ota/status", HTTP_GET, [](AsyncWebServerRequest *request) {
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
    server.on("/api/ota/confirm", HTTP_POST, [](AsyncWebServerRequest *request) {
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
#endif
}
