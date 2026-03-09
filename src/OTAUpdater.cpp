//
// OTA Firmware Update Manager Implementation
// Uses ESP-IDF esp_http_client for reliable HTTPS with built-in cert bundle.
//

#include "OTAUpdater.h"
#include "Config.h"

#ifdef ARDUINO
#include <esp_task_wdt.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <WiFi.h>

// The IDF esp_crt_bundle_attach uses the CA bundle embedded in the firmware binary.
// We declare it directly because the Arduino WiFiClientSecure wrapper shadows the IDF header.
extern "C" esp_err_t esp_crt_bundle_attach(void *conf);

// Override the pre-compiled SDK's mbedTLS allocator.
// The SDK version uses MALLOC_CAP_INTERNAL only, which fails on ESP32-S2 with
// fragmented internal SRAM. This version tries internal first, then falls back
// to PSRAM for large allocations (like the 16KB TLS buffers).
extern "C" void *esp_mbedtls_mem_calloc(size_t n, size_t size) {
    void *ptr = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ptr == nullptr) {
        ptr = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return ptr;
}

extern "C" void esp_mbedtls_mem_free(void *ptr) {
    heap_caps_free(ptr);
}

static const char *TAG = "OTA";

// ============================================================================
// Streaming reader for ArduinoJson — reads directly from esp_http_client
// so we don't need a large response buffer in RAM.
// ============================================================================

struct EspHttpReader {
    esp_http_client_handle_t client;

    int read() {
        char c;
        int r = esp_http_client_read(client, &c, 1);
        return r == 1 ? static_cast<unsigned char>(c) : -1;
    }

    size_t readBytes(char *buffer, size_t length) {
        int r = esp_http_client_read(client, buffer, length);
        return r > 0 ? static_cast<size_t>(r) : 0;
    }
};

// ============================================================================
// Public Methods
// ============================================================================

bool OTAUpdater::checkForUpdate(const char *owner, const char *repo, FirmwareInfo &info) {
    info.isValid = false;

    String apiUrl = String("https://api.github.com/repos/") + owner + "/" + repo + "/releases/latest";
    Serial.printf("[OTA] Checking for updates: %s\r\n", apiUrl.c_str());
    Serial.printf("[OTA] Free heap: %u, internal: %u, largest internal block: %u\r\n",
                  ESP.getFreeHeap(),
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    esp_http_client_config_t config{};
    config.url = apiUrl.c_str();
    config.timeout_ms = TIMEOUT_MS;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        info.errorMessage = "HTTP client init failed";
        Serial.println("[OTA] HTTP client init failed");
        return false;
    }

    esp_http_client_set_header(client, "Accept", "application/vnd.github.v3+json");
    esp_http_client_set_header(client, "User-Agent", "ESP32-OTA/1.0");

    // Open connection + send request + receive headers
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        int errno_val = esp_http_client_get_errno(client);
        Serial.printf("[OTA] HTTP connect failed: %s, errno: %d (free heap: %u)\r\n",
                      esp_err_to_name(err), errno_val, ESP.getFreeHeap());
        info.errorMessage = String("HTTP connect failed: ") + esp_err_to_name(err);
        esp_http_client_cleanup(client);
        return false;
    }

    esp_http_client_fetch_headers(client);
    int statusCode = esp_http_client_get_status_code(client);
    Serial.printf("[OTA] HTTP status: %d, free heap: %u\r\n", statusCode, ESP.getFreeHeap());

    bool result = false;

    if (statusCode != 200) {
        info.errorMessage = "HTTP status " + String(statusCode);
    } else {
        // Stream JSON directly from HTTP response into ArduinoJson parser
        EspHttpReader reader{client};

        JsonDocument filter;
        filter["tag_name"] = true;
        filter["name"] = true;
        filter["body"] = true;
        filter["assets"] = true;

        JsonDocument doc;
        DeserializationError jsonErr = deserializeJson(doc, reader,
                                                       DeserializationOption::Filter(filter));

        if (jsonErr) {
            Serial.printf("[OTA] JSON parse error: %s\r\n", jsonErr.c_str());
            info.errorMessage = String("JSON parse error: ") + jsonErr.c_str();
        } else {
            info.version = doc["tag_name"].as<String>();
            info.name = doc["name"].as<String>();
            info.releaseNotes = doc["body"].as<String>();

            if (info.version.isEmpty()) {
                info.errorMessage = "No tag_name in release response";
            } else {
                JsonArray assets = doc["assets"];
                Serial.printf("[OTA] Found %d assets\r\n", assets.size());

                for (JsonObject asset : assets) {
                    auto assetName = asset["name"].as<String>();
                    if (assetName.endsWith(".bin")) {
                        info.downloadUrl = asset["browser_download_url"].as<String>();
                        info.size = asset["size"].as<size_t>();
                        info.isValid = true;
                        Serial.printf("[OTA] Found firmware: %s (%zu bytes)\r\n",
                                      assetName.c_str(), info.size);
                        break;
                    }
                }

                if (!info.isValid) {
                    info.errorMessage = "No .bin file found in release assets";
                } else {
                    Serial.printf("[OTA] Release: %s (%s)\r\n",
                                  info.name.c_str(), info.version.c_str());
                    result = true;
                }
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    Serial.printf("[OTA] Free heap after check: %u bytes\r\n", ESP.getFreeHeap());
    return result;
}

bool OTAUpdater::performUpdate(
    const String &downloadUrl,
    size_t expectedSize,
    const std::function<void(int, size_t)> &onProgress
) {
    Serial.printf("[OTA] Starting firmware download\r\n");
    Serial.printf("[OTA] URL: %s\r\n", downloadUrl.c_str());
    Serial.printf("[OTA] Expected size: %zu bytes\r\n", expectedSize);

    esp_task_wdt_reset();

    Serial.printf("[OTA] WDT reset done\r\n");

    if (!hasEnoughMemory()) {
        Serial.println("[OTA] Insufficient memory for OTA");
        return false;
    }

    Serial.printf("[OTA] has enough memory done\r\n");

    const esp_partition_t *nextPartition = esp_ota_get_next_update_partition(nullptr);
    if (nextPartition == nullptr) {
        Serial.println("[OTA] No OTA partition available");
        return false;
    }

    Serial.printf("[OTA] Update target: %s (offset 0x%x)\r\n", nextPartition->label, nextPartition->address);

    esp_http_client_config_t config{};
    config.url = downloadUrl.c_str();
    config.timeout_ms = TIMEOUT_MS;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.buffer_size = CHUNK_SIZE;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        Serial.println("[OTA] HTTP client init failed");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        Serial.printf("[OTA] HTTP open failed: %s\r\n", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int contentLength = esp_http_client_fetch_headers(client);
    int statusCode = esp_http_client_get_status_code(client);

    if (statusCode != 200) {
        Serial.printf("[OTA] HTTP status: %d\r\n", statusCode);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    if (contentLength > 0 && static_cast<size_t>(contentLength) != expectedSize) {
        Serial.printf("[OTA] Size mismatch! Expected: %zu, Got: %d\r\n", expectedSize, contentLength);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    if (!Update.begin(expectedSize, U_FLASH)) {
        Serial.printf("[OTA] Update.begin() failed: %s\r\n", Update.errorString());
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    Serial.println("[OTA] Flashing firmware...");

    uint8_t buffer[CHUNK_SIZE];
    size_t totalRead = 0;
    unsigned long lastProgressLog = millis();
    bool success = true;

    while (totalRead < expectedSize) {
        int bytesRead = esp_http_client_read(client, reinterpret_cast<char *>(buffer),
                                              std::min(static_cast<size_t>(CHUNK_SIZE), expectedSize - totalRead));

        if (bytesRead > 0) {
            size_t bytesWritten = Update.write(buffer, bytesRead);
            if (bytesWritten != static_cast<size_t>(bytesRead)) {
                Serial.printf("[OTA] Flash write failed! Expected: %d, Written: %zu\r\n", bytesRead, bytesWritten);
                success = false;
                break;
            }
            totalRead += bytesWritten;
            esp_task_wdt_reset();
            vTaskDelay(1);

            if (millis() - lastProgressLog > 1000) {
                int percent = (totalRead * 100) / expectedSize;
                if (onProgress) {
                    onProgress(percent, totalRead);
                }
                lastProgressLog = millis();
            }
        } else if (bytesRead == 0) {
            Serial.println("[OTA] Connection closed prematurely");
            success = false;
            break;
        } else {
            Serial.printf("[OTA] Read error: %d\r\n", bytesRead);
            success = false;
            break;
        }
    }

    if (!success) {
        Update.abort();
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    if (!Update.end(false)) {
        Serial.printf("[OTA] Update.end() failed: %s\r\n", Update.errorString());
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    Serial.printf("[OTA] Download complete! %zu bytes flashed\r\n", totalRead);
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

#endif  // ARDUINO
