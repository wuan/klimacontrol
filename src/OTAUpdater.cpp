//
// OTA Firmware Update Manager Implementation
//

#include "OTAUpdater.h"
#include "Config.h"

#ifdef ARDUINO
#include <esp_task_wdt.h>  // For watchdog timer control

// ============================================================================
// Public Methods
// ============================================================================

bool OTAUpdater::checkForUpdate(const char *owner, const char *repo, FirmwareInfo &info) {
    info.isValid = false;

    WiFiClientSecure client = createSecureClient();

    HTTPClient http;
    String apiUrl = String("https://api.github.com/repos/") + owner + "/" + repo + "/releases/latest";

    Serial.printf("[OTA] Checking for updates: %s\r\n", apiUrl.c_str());

    if (!http.begin(client, apiUrl)) {
        Serial.println("[OTA] HTTP initialization failed");
        return false;
    }

    // Configure HTTP request
    http.addHeader("Accept", "application/vnd.github.v3+json");
    http.addHeader("User-Agent", "ESP32-OTA/1.0");
    http.setTimeout(TIMEOUT_MS);
    http.useHTTP10(true); // Use HTTP/1.0 to reduce overhead

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[OTA] HTTP GET failed: %s (free heap: %u bytes)\r\n", HTTPClient::errorToString(httpCode).c_str(), ESP.getFreeHeap());
        http.end();
        return false;
    }

    // Log memory before parsing
    uint32_t freeBefore = ESP.getFreeHeap();
    Serial.printf("[OTA] Free heap before JSON parse: %u bytes\r\n", freeBefore);

    // Use JSON filter to only parse fields we need (saves memory)
    JsonDocument filter;
    filter["tag_name"] = true;
    filter["name"] = true;
    filter["body"] = true;
    // Include ALL assets (not just first one) - firmware.bin might not be first
    filter["assets"] = true;

    // Parse JSON response with filter
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));

    uint32_t freeAfter = ESP.getFreeHeap();
    Serial.printf("[OTA] Free heap after JSON parse: %u bytes (used: %d bytes)\r\n", freeAfter, freeBefore - freeAfter);

    http.end();

    if (error) {
        Serial.printf("[OTA] JSON parse error: %s\r\n", error.c_str());
        Serial.printf("[OTA] Free heap: %u bytes, Required: ~8KB\r\n", ESP.getFreeHeap());
        return false;
    }

    // Extract basic release info
    info.version = doc["tag_name"].as<String>();
    info.name = doc["name"].as<String>();
    info.releaseNotes = doc["body"].as<String>();

    if (info.version.isEmpty()) {
        Serial.println("[OTA] No tag_name in release");
        return false;
    }

    // Find .bin file in assets
    JsonArray assets = doc["assets"];
    bool foundBin = false;

    Serial.printf("[OTA] Found %d assets in release\r\n", assets.size());

    for (JsonObject asset: assets) {
        auto assetName = asset["name"].as<String>();
        Serial.printf("[OTA] Asset: %s\r\n", assetName.c_str());

        if (assetName.endsWith(".bin")) {
            info.downloadUrl = asset["browser_download_url"].as<String>();
            info.size = asset["size"].as<size_t>();
            foundBin = true;
            Serial.printf("[OTA] Found firmware: %s (%zu bytes)\r\n", assetName.c_str(), info.size);
            break;
        }
    }

    if (!foundBin) {
        Serial.println("[OTA] No .bin file found in release assets");
        Serial.println("[OTA] Make sure the GitHub release has a firmware.bin file attached");
        return false;
    }

    info.isValid = true;

    Serial.printf("[OTA] Release found: %s (%s)\r\n", info.name.c_str(), info.version.c_str());
    Serial.printf("[OTA] Firmware size: %zu bytes\r\n", info.size);

    return true;
}

bool OTAUpdater::performUpdate(
    const String &downloadUrl,
    size_t expectedSize,
    const std::function<void(int, size_t)> &onProgress
) {
    Serial.printf("[OTA] Starting firmware download\r\n");
    Serial.printf("[OTA] URL: %s\r\n", downloadUrl.c_str());
    Serial.printf("[OTA] Expected size: %zu bytes\r\n", expectedSize);

    // Reset watchdog before starting long operation
    esp_task_wdt_reset();

    // Check memory
    if (!hasEnoughMemory()) {
        Serial.println("[OTA] Insufficient memory for OTA");
        return false;
    }

    // Check flash space
    const esp_partition_t *nextPartition = esp_ota_get_next_update_partition(nullptr);
    if (nextPartition == nullptr) {
        Serial.println("[OTA] No OTA partition available");
        return false;
    }

    Serial.printf("[OTA] Update target: %s (offset 0x%x)\r\n", nextPartition->label, nextPartition->address);

    WiFiClientSecure client = createSecureClient();

    HTTPClient http;
    if (!http.begin(client, downloadUrl)) {
        Serial.println("[OTA] HTTP initialization failed");
        return false;
    }

    http.setTimeout(TIMEOUT_MS);
    http.useHTTP10(true);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // Follow GitHub redirects

    int httpCode = http.GET();

    // Handle redirects manually if needed
    if (httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND) {
        String redirectUrl = http.getLocation();
        Serial.printf("[OTA] Following redirect to: %s\r\n", redirectUrl.c_str());
        http.end();

        // Follow redirect
        if (!http.begin(client, redirectUrl)) {
            Serial.println("[OTA] Failed to follow redirect");
            return false;
        }
        http.setTimeout(TIMEOUT_MS);
        httpCode = http.GET();
    }

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[OTA] HTTP error: %s (free heap: %u bytes)\r\n", HTTPClient::errorToString(httpCode).c_str(), ESP.getFreeHeap());
        http.end();
        return false;
    }

    // Verify content size
    int contentSize = http.getSize();
    if (contentSize != expectedSize) {
        Serial.printf("[OTA] Size mismatch! Expected: %zu, Got: %d\r\n", expectedSize, contentSize);
        http.end();
        return false;
    }

    // Begin OTA write
    if (!Update.begin(expectedSize, U_FLASH)) {
        Serial.printf("[OTA] Update.begin() failed: %s\r\n", Update.errorString());
        http.end();
        return false;
    }

    Serial.println("[OTA] Flashing firmware...");

    // Stream data from HTTP to flash
    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[CHUNK_SIZE];
    size_t totalRead = 0;
    unsigned long lastDataReceived = millis();
    unsigned long lastProgressLog = millis();

    while (totalRead < expectedSize) {
        size_t available = stream->available();

        if (available > 0) {
            // Read chunk
            size_t toRead = std::min((size_t) CHUNK_SIZE, expectedSize - totalRead);
            int bytesRead = stream->readBytes(buffer, toRead);

            if (bytesRead > 0) {
                // Write to flash
                size_t bytesWritten = Update.write(buffer, bytesRead);

                if (bytesWritten != bytesRead) {
                    Serial.printf("[OTA] Flash write failed! Expected: %d, Written: %zu\r\n", bytesRead, bytesWritten);
                    Serial.printf("[OTA] Update error: %s\r\n", Update.errorString());
                    Update.abort();
                    http.end();
                    return false;
                }

                totalRead += bytesWritten;
                lastDataReceived = millis();

                // Feed watchdog and yield to prevent reset during long download
                esp_task_wdt_reset();
                vTaskDelay(1); // Let other tasks run

                // Progress update (every 1 second)
                if (millis() - lastProgressLog > 1000) {
                    int percent = (totalRead * 100) / expectedSize;
                    if (onProgress) {
                        onProgress(percent, totalRead);
                    }

                    lastProgressLog = millis();
                }
            } else if (bytesRead < 0) {
                Serial.printf("[OTA] Stream read error: %d\r\n", bytesRead);
                Update.abort();
                http.end();
                return false;
            }
        } else {
            // No data available, wait a bit
            esp_task_wdt_reset(); // Feed watchdog while waiting
            vTaskDelay(10 / portTICK_PERIOD_MS);

            // Check for timeout (60 seconds of no data)
            if (millis() - lastDataReceived > 60000) {
                Serial.println("[OTA] Download timeout! No data received for 60 seconds");
                Serial.printf("[OTA] Downloaded: %zu/%zu bytes (%d%%)\r\n", totalRead, expectedSize,
                              (totalRead * 100) / expectedSize);
                Update.abort();
                http.end();
                return false;
            }
        }
    }

    // Finalize OTA
    if (!Update.end(false)) {
        // false = don't reboot
        Serial.printf("[OTA] Update.end() failed: %s\r\n", Update.errorString());
        http.end();
        return false;
    }

    http.end();

    Serial.printf("[OTA] Download complete! %zu bytes flashed\r\n", totalRead);
    Serial.println("[OTA] Firmware ready to boot. Call confirmBoot() after verifying it works.");

    return true;
}

bool OTAUpdater::confirmBoot() {
    Serial.println("[OTA] Confirming OTA boot...");

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();

    if (err != ESP_OK) {
        Serial.printf("[OTA] Boot confirmation failed: %s\r\n", esp_err_to_name(err));
        return false;
    }

    Serial.println("[OTA] Boot confirmed - rollback disabled");
    return true;
}

bool OTAUpdater::hasUnconfirmedUpdate() {
    const esp_partition_t *runningPartition = esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    esp_err_t err = esp_ota_get_state_partition(runningPartition, &state);

    if (err != ESP_OK) {
        Serial.printf("[OTA] Failed to get partition state: %s\r\n", esp_err_to_name(err));
        return false;
    }

    return (state == ESP_OTA_IMG_NEW);
}

bool OTAUpdater::getRunningPartitionInfo(String &label, uint32_t &address) {
    const esp_partition_t *runningPartition = esp_ota_get_running_partition();

    if (runningPartition == nullptr) {
        Serial.println("[OTA] Could not get running partition");
        return false;
    }

    label = String(runningPartition->label);
    address = runningPartition->address;

    return true;
}

void OTAUpdater::getMemoryInfo(uint32_t &freeHeap, uint32_t &minFreeHeap) {
    freeHeap = esp_get_free_heap_size();
    minFreeHeap = esp_get_minimum_free_heap_size();
}

bool OTAUpdater::hasEnoughMemory() {
    uint32_t freeHeap, minFree;
    getMemoryInfo(freeHeap, minFree);

    Serial.printf("[OTA] Memory check - Free: %u bytes, Min: %u bytes\r\n", freeHeap, minFree);

    if (freeHeap < MIN_FREE_HEAP) {
        Serial.printf("[OTA] Insufficient free heap! Need: %d, Have: %u\r\n", MIN_FREE_HEAP, freeHeap);
        return false;
    }

    return true;
}

// ============================================================================
// Private Methods
// ============================================================================

WiFiClientSecure OTAUpdater::createSecureClient() {
    WiFiClientSecure client;

    // Use built-in CA certificate bundle for GitHub
    // This verifies the GitHub server certificate against Mozilla's root CA list
    client.setCACert(NULL); // Use built-in bundle
    client.setInsecure(); // For now, skip verification (TODO: add GitHub cert)

    return client;
}

#endif  // ARDUINO