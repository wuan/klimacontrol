//
// OTA Firmware Update Manager Implementation
//

#include "OTAUpdater.h"
#include "Config.h"

#ifdef ARDUINO
#include <esp_task_wdt.h>  // For watchdog timer control

// HTTPClient error codes (from HTTPClient.h)
#define HTTPC_ERROR_CONNECTION_FAILED   (-1)
#define HTTPC_ERROR_SEND_HEADER_FAILED  (-2)
#define HTTPC_ERROR_SEND_PAYLOAD_FAILED (-3)
#define HTTPC_ERROR_NOT_CONNECTED       (-4)
#define HTTPC_ERROR_CONNECTION_LOST     (-5)
#define HTTPC_ERROR_NO_STREAM           (-6)
#define HTTPC_ERROR_NO_HTTP_SERVER      (-7)
#define HTTPC_ERROR_TOO_LESS_RAM        (-8)
#define HTTPC_ERROR_ENCODING            (-9)
#define HTTPC_ERROR_STREAM_WRITE        (-10)
#define HTTPC_ERROR_READ_TIMEOUT        (-11)

// ============================================================================
// Public Methods
// ============================================================================

bool OTAUpdater::checkForUpdate(const char *owner, const char *repo, FirmwareInfo &info) {
    info.isValid = false;

    // Check WiFi connection first
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OTA] WiFi not connected");
        return false;
    }

    // Log memory and WiFi status
    Serial.printf("[OTA] Free heap before request: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("[OTA] WiFi RSSI: %d dBm\n", WiFi.RSSI());

    WiFiClientSecure client = createSecureClient();

    HTTPClient http;
    String apiUrl = String("https://api.github.com/repos/") + owner + "/" + repo + "/releases/latest";

    Serial.printf("[OTA] Checking for updates: %s\n", apiUrl.c_str());

    if (!http.begin(client, apiUrl)) {
        Serial.println("[OTA] HTTP initialization failed - could not begin connection");
        Serial.printf("[OTA] Free heap: %u bytes\n", ESP.getFreeHeap());
        return false;
    }

    // Configure HTTP request with proper headers for GitHub API
    http.addHeader("Accept", "application/vnd.github.v3+json");
    http.addHeader("User-Agent", "ESP32-OTA/1.0");
    http.setTimeout(TIMEOUT_MS);
    http.useHTTP10(true); // Use HTTP/1.0 to reduce overhead
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // Handle GitHub redirects

    Serial.println("[OTA] Sending HTTPS GET request...");
    int httpCode = http.GET();

    // Detailed error reporting
    if (httpCode < 0) {
        Serial.printf("[OTA] HTTP GET failed with error: %d\n", httpCode);
        switch(httpCode) {
            case HTTPC_ERROR_CONNECTION_FAILED:
                Serial.println("[OTA] Connection failed - check network/firewall");
                break;
            case HTTPC_ERROR_SEND_HEADER_FAILED:
                Serial.println("[OTA] Failed to send HTTP headers");
                break;
            case HTTPC_ERROR_SEND_PAYLOAD_FAILED:
                Serial.println("[OTA] Failed to send HTTP payload");
                break;
            case HTTPC_ERROR_NOT_CONNECTED:
                Serial.println("[OTA] Not connected to server");
                break;
            case HTTPC_ERROR_CONNECTION_LOST:
                Serial.println("[OTA] Connection lost during transfer");
                break;
            case HTTPC_ERROR_NO_STREAM:
                Serial.println("[OTA] No HTTP stream available");
                break;
            case HTTPC_ERROR_NO_HTTP_SERVER:
                Serial.println("[OTA] Server didn't respond with HTTP");
                break;
            case HTTPC_ERROR_TOO_LESS_RAM:
                Serial.println("[OTA] Not enough RAM for operation");
                break;
            case HTTPC_ERROR_ENCODING:
                Serial.println("[OTA] Encoding error");
                break;
            case HTTPC_ERROR_STREAM_WRITE:
                Serial.println("[OTA] Stream write error");
                break;
            case HTTPC_ERROR_READ_TIMEOUT:
                Serial.println("[OTA] Read timeout");
                break;
            default:
                Serial.println("[OTA] Unknown error");
                break;
        }
        Serial.printf("[OTA] Free heap after error: %u bytes\n", ESP.getFreeHeap());
        http.end();
        return false;
    }

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[OTA] HTTP response code: %d\n", httpCode);
        if (httpCode == HTTP_CODE_UNAUTHORIZED) {
            Serial.println("[OTA] Authentication required - check if repo is private");
        } else if (httpCode == HTTP_CODE_NOT_FOUND) {
            Serial.println("[OTA] Repository or release not found");
        } else if (httpCode == HTTP_CODE_FORBIDDEN) {
            Serial.println("[OTA] API rate limit may be exceeded");
        }
        http.end();
        return false;
    }

    // Log memory before parsing
    uint32_t freeBefore = ESP.getFreeHeap();
    Serial.printf("[OTA] Free heap before JSON parse: %u bytes\n", freeBefore);

    // Use JSON filter to only parse fields we need (saves memory)
    StaticJsonDocument < Config::JSON_DOC_SMALL > filter;
    filter["tag_name"] = true;
    filter["name"] = true;
    filter["body"] = true;
    // Include ALL assets (not just first one) - firmware.bin might not be first
    filter["assets"] = true;

    // Parse JSON response with filter
    DynamicJsonDocument doc(Config::JSON_DOC_OTA);
    DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));

    uint32_t freeAfter = ESP.getFreeHeap();
    Serial.printf("[OTA] Free heap after JSON parse: %u bytes (used: %d bytes)\n", freeAfter, freeBefore - freeAfter);

    http.end();

    if (error) {
        Serial.printf("[OTA] JSON parse error: %s\n", error.c_str());
        Serial.printf("[OTA] Free heap: %u bytes, Required: ~8KB\n", ESP.getFreeHeap());
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

    Serial.printf("[OTA] Found %d assets in release\n", assets.size());

    for (JsonObject asset: assets) {
        String assetName = asset["name"].as<String>();
        Serial.printf("[OTA] Asset: %s\n", assetName.c_str());

        if (assetName.endsWith(".bin")) {
            info.downloadUrl = asset["browser_download_url"].as<String>();
            info.size = asset["size"].as<size_t>();
            foundBin = true;
            Serial.printf("[OTA] Found firmware: %s (%zu bytes)\n", assetName.c_str(), info.size);
            break;
        }
    }

    if (!foundBin) {
        Serial.println("[OTA] No .bin file found in release assets");
        Serial.println("[OTA] Make sure the GitHub release has a firmware.bin file attached");
        return false;
    }

    info.isValid = true;

    Serial.printf("[OTA] Release found: %s (%s)\n", info.name.c_str(), info.version.c_str());
    Serial.printf("[OTA] Firmware size: %zu bytes\n", info.size);

    return true;
}

bool OTAUpdater::performUpdate(
    const String &downloadUrl,
    size_t expectedSize,
    std::function<void(int, size_t)> onProgress
) {
    Serial.printf("[OTA] Starting firmware download\n");
    Serial.printf("[OTA] URL: %s\n", downloadUrl.c_str());
    Serial.printf("[OTA] Expected size: %zu bytes\n", expectedSize);

    // Check WiFi connection first
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OTA] WiFi not connected - cannot perform update");
        return false;
    }

    Serial.printf("[OTA] WiFi RSSI: %d dBm\n", WiFi.RSSI());

    // Reset watchdog before starting long operation
    esp_task_wdt_reset();

    // Check memory
    if (!hasEnoughMemory()) {
        Serial.println("[OTA] Insufficient memory for OTA");
        Serial.printf("[OTA] Free heap: %u bytes, Required: %d bytes\n",
                     ESP.getFreeHeap(), MIN_FREE_HEAP);
        return false;
    }

    // Check flash space
    const esp_partition_t *nextPartition = esp_ota_get_next_update_partition(nullptr);
    if (nextPartition == nullptr) {
        Serial.println("[OTA] No OTA partition available");
        Serial.println("[OTA] Check partitions.csv for OTA partition configuration");
        return false;
    }

    Serial.printf("[OTA] Update target: %s (offset 0x%x, size 0x%x)\n",
                  nextPartition->label, nextPartition->address, nextPartition->size);

    // Check if firmware will fit in partition
    if (expectedSize > nextPartition->size) {
        Serial.printf("[OTA] Firmware too large! Size: %zu, Partition: %u\n",
                     expectedSize, nextPartition->size);
        return false;
    }

    WiFiClientSecure client = createSecureClient();

    HTTPClient http;
    if (!http.begin(client, downloadUrl)) {
        Serial.println("[OTA] HTTP initialization failed for download");
        Serial.printf("[OTA] Free heap: %u bytes\n", ESP.getFreeHeap());
        return false;
    }

    http.setTimeout(TIMEOUT_MS);
    http.useHTTP10(true);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // Follow GitHub redirects
    http.addHeader("User-Agent", "ESP32-OTA/1.0");

    Serial.println("[OTA] Sending HTTPS GET request for firmware download...");
    int httpCode = http.GET();

    // Handle redirects manually if needed
    if (httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND) {
        String redirectUrl = http.getLocation();
        Serial.printf("[OTA] Following redirect to: %s\n", redirectUrl.c_str());
        http.end();

        // Follow redirect
        if (!http.begin(client, redirectUrl)) {
            Serial.println("[OTA] Failed to follow redirect");
            return false;
        }
        http.setTimeout(TIMEOUT_MS);
        http.addHeader("User-Agent", "ESP32-OTA/1.0");
        httpCode = http.GET();
    }

    // Check for HTTP errors
    if (httpCode < 0) {
        Serial.printf("[OTA] HTTP GET failed with error: %d\n", httpCode);
        if (httpCode == HTTPC_ERROR_CONNECTION_FAILED) {
            Serial.println("[OTA] Connection failed - check network stability");
            Serial.println("[OTA] Possible causes:");
            Serial.println("[OTA] - Poor WiFi signal");
            Serial.println("[OTA] - DNS resolution failure");
            Serial.println("[OTA] - Firewall blocking HTTPS");
            Serial.println("[OTA] - GitHub servers unreachable");
        }
        http.end();
        return false;
    }

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[OTA] HTTP response code: %d\n", httpCode);
        http.end();
        return false;
    }

    // Verify content size
    int contentSize = http.getSize();
    if (contentSize != expectedSize) {
        Serial.printf("[OTA] Size mismatch! Expected: %zu, Got: %d\n", expectedSize, contentSize);
        http.end();
        return false;
    }

    // Begin OTA write
    if (!Update.begin(expectedSize, U_FLASH)) {
        Serial.printf("[OTA] Update.begin() failed: %s\n", Update.errorString());
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
                    Serial.printf("[OTA] Flash write failed! Expected: %d, Written: %zu\n", bytesRead, bytesWritten);
                    Serial.printf("[OTA] Update error: %s\n", Update.errorString());
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
                Serial.printf("[OTA] Stream read error: %d\n", bytesRead);
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
                Serial.printf("[OTA] Downloaded: %zu/%zu bytes (%d%%)\n", totalRead, expectedSize,
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
        Serial.printf("[OTA] Update.end() failed: %s\n", Update.errorString());
        http.end();
        return false;
    }

    http.end();

    Serial.printf("[OTA] Download complete! %zu bytes flashed\n", totalRead);
    Serial.println("[OTA] Firmware ready to boot. Call confirmBoot() after verifying it works.");

    return true;
}

bool OTAUpdater::confirmBoot() {
    Serial.println("[OTA] Confirming OTA boot...");

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();

    if (err != ESP_OK) {
        Serial.printf("[OTA] Boot confirmation failed: %s\n", esp_err_to_name(err));
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
        Serial.printf("[OTA] Failed to get partition state: %s\n", esp_err_to_name(err));
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

void OTAUpdater::getMemoryInfo(uint32_t &freeHeap, uint32_t &minFreeHeap, uint32_t &psramFree) {
    freeHeap = esp_get_free_heap_size();
    minFreeHeap = esp_get_minimum_free_heap_size();

// TODO: Fix this for the QtPy ESP32-S2
// #ifdef CONFIG_SPIRAM
#ifdef CONFIG_HAS_PSRAM
psramFree= esp_get_free_psram_size();
#else
psramFree=0;
#endif
}

bool OTAUpdater::hasEnoughMemory() {
    uint32_t freeHeap, minFree, psramFree;
    getMemoryInfo(freeHeap, minFree, psramFree);

    Serial.printf("[OTA] Memory check - Free: %u bytes, Min: %u bytes\n", freeHeap, minFree);

    if (freeHeap < MIN_FREE_HEAP) {
        Serial.printf("[OTA] Insufficient free heap! Need: %d, Have: %u\n", MIN_FREE_HEAP, freeHeap);
        return false;
    }

    return true;
}

// ============================================================================
// Private Methods
// ============================================================================

WiFiClientSecure OTAUpdater::createSecureClient() {
    WiFiClientSecure client;

    // Configure TLS/SSL for GitHub API access
    // Option 1: Skip certificate verification (less secure but more reliable)
    client.setInsecure();

    // Increase timeout for TLS handshake (default is too short for HTTPS)
    client.setTimeout(15); // 15 seconds for TLS operations

    // Note: For production, consider using GitHub's root CA certificate:
    // client.setCACertBundle(rootca_crt_bundle);
    // or use the ESP32's built-in certificate bundle:
    // client.setCACertBundle(NULL);

    Serial.println("[OTA] WiFiClientSecure configured (certificate verification disabled)");

    return client;
}

#endif  // ARDUINO