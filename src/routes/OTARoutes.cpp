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
    // GET /api/ota/check - Check for firmware updates
    server.on("/api/ota/check", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;

        FirmwareInfo info;
        bool releaseFound = OTAUpdater::checkForUpdate(OTA_GITHUB_OWNER, OTA_GITHUB_REPO, info);
        bool updateAvailable = releaseFound && (info.version != FIRMWARE_VERSION);

        if (releaseFound) {
            doc["update_available"] = updateAvailable;
            doc["current_version"] = FIRMWARE_VERSION;
            doc["latest_version"] = info.version;
            doc["release_name"] = info.name;
            doc["size_bytes"] = info.size;
            doc["download_url"] = info.downloadUrl;
        } else {
            doc["update_available"] = false;
            doc["current_version"] = FIRMWARE_VERSION;
            doc["error"] = info.errorMessage.isEmpty() ? "Failed to check for updates" : info.errorMessage;
        }

        String response;
        serializeJson(doc, response);
        request->send(200, CONTENT_TYPE_JSON, response);
    });

    // POST /api/ota/update - Perform OTA update
    server.on("/api/ota/update", HTTP_POST,
              [](AsyncWebServerRequest *request) {
                  // Send immediate response before starting update
                  request->send(200, CONTENT_TYPE_JSON,
                                R"({"status":"starting","message":"OTA update started"})");
              },
              nullptr,
              [this]([[maybe_unused]] AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                  if (index == 0) {
                      ESP_LOGI(TAG, "OTA update requested");
                  }

                  if (index + len == total) {
                      JsonDocument doc;
                      deserializeJson(doc, data, len);

                      String downloadUrl = doc["download_url"] | "";
                      size_t size = doc["size"] | 0;

                      if (downloadUrl.isEmpty() || size == 0) {
                          return;
                      }

                      ESP_LOGI(TAG, "Starting OTA: %s (%zu bytes)", downloadUrl.c_str(), size);

                      // Run the (multi-minute, blocking) download on a dedicated
                      // worker task so we don't stall the AsyncTCP event task.
                      // The "starting" response was already queued in the request
                      // handler above and flushes as soon as this callback returns.
                      if (!OTAUpdater::startBackgroundUpdate(downloadUrl, size, this->config)) {
                          ESP_LOGW(TAG, "OTA update not started (busy or task creation failed)");
                      }
                  }
              }
    );

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

    // POST /api/ota/confirm - Confirm successful boot after OTA
    server.on("/api/ota/confirm", HTTP_POST, [](AsyncWebServerRequest *request) {
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
