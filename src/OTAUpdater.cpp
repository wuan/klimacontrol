//
// OTA Firmware Update Manager Implementation
// Uses ESP-IDF esp_http_client for reliable HTTPS with built-in cert bundle.
//

#include "OTAUpdater.h"
#include "Config.h"

#ifdef ARDUINO
#include <esp_task_wdt.h>
#include <esp_http_client.h>
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

// Streaming reader for ArduinoJson — reads directly from esp_http_client
// so we don't need a large response buffer in RAM.
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

// RAII wrapper for esp_http_client lifecycle
struct HttpClient {
    esp_http_client_handle_t handle = nullptr;

    explicit HttpClient(const esp_http_client_config_t &config)
        : handle(esp_http_client_init(&config)) {}

    ~HttpClient() {
        if (handle) {
            esp_http_client_close(handle);
            esp_http_client_cleanup(handle);
        }
    }

    HttpClient(const HttpClient &) = delete;
    HttpClient &operator=(const HttpClient &) = delete;

    explicit operator bool() const { return handle != nullptr; }

    // Open connection, following redirects (up to maxRedirects hops).
    // Returns the HTTP status code, or -1 on connection failure.
    int openWithRedirects(int maxRedirects = 5) {
        for (int i = 0; i < maxRedirects; i++) {
            esp_err_t err = esp_http_client_open(handle, 0);
            if (err != ESP_OK) {
                Serial.printf("[OTA] HTTP open failed: %s\r\n", esp_err_to_name(err));
                return -1;
            }

            esp_http_client_fetch_headers(handle);
            int status = esp_http_client_get_status_code(handle);

            if (status == 301 || status == 302 || status == 307 || status == 308) {
                esp_http_client_close(handle);
                if (esp_http_client_set_redirection(handle) != ESP_OK) {
                    Serial.println("[OTA] Redirect failed: no Location header");
                    return -1;
                }
                Serial.printf("[OTA] Following redirect (%d)...\r\n", status);
                continue;
            }
            return status;
        }
        Serial.println("[OTA] Too many redirects");
        return -1;
    }
};

// ============================================================================
// Public Methods
// ============================================================================

bool OTAUpdater::checkForUpdate(const char *owner, const char *repo, FirmwareInfo &info) {
    info.isValid = false;

    String apiUrl = String("https://api.github.com/repos/") + owner + "/" + repo + "/releases/latest";
    Serial.printf("[OTA] Checking: %s\r\n", apiUrl.c_str());

    esp_http_client_config_t config{};
    config.url = apiUrl.c_str();
    config.timeout_ms = TIMEOUT_MS;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    HttpClient client(config);
    if (!client) {
        info.errorMessage = "HTTP client init failed";
        return false;
    }

    esp_http_client_set_header(client.handle, "Accept", "application/vnd.github.v3+json");
    esp_http_client_set_header(client.handle, "User-Agent", "ESP32-OTA/1.0");

    int statusCode = client.openWithRedirects();
    if (statusCode != 200) {
        info.errorMessage = statusCode < 0 ? "Connection failed" : "HTTP status " + String(statusCode);
        return false;
    }

    EspHttpReader reader{client.handle};

    JsonDocument filter;
    filter["tag_name"] = true;
    filter["name"] = true;
    filter["body"] = true;
    filter["assets"] = true;

    JsonDocument doc;
    DeserializationError jsonErr = deserializeJson(doc, reader,
                                                   DeserializationOption::Filter(filter));
    if (jsonErr) {
        info.errorMessage = String("JSON parse error: ") + jsonErr.c_str();
        return false;
    }

    info.version = doc["tag_name"].as<String>();
    info.name = doc["name"].as<String>();
    info.releaseNotes = doc["body"].as<String>();

    if (info.version.isEmpty()) {
        info.errorMessage = "No tag_name in release response";
        return false;
    }

    for (JsonObject asset : doc["assets"].as<JsonArray>()) {
        auto assetName = asset["name"].as<String>();
        if (assetName.endsWith(".bin")) {
            info.downloadUrl = asset["browser_download_url"].as<String>();
            info.size = asset["size"].as<size_t>();
            info.isValid = true;
            Serial.printf("[OTA] Release %s: %s (%zu bytes)\r\n",
                          info.version.c_str(), assetName.c_str(), info.size);
            return true;
        }
    }

    info.errorMessage = "No .bin file found in release assets";
    return false;
}

bool OTAUpdater::performUpdate(
    const String &downloadUrl,
    size_t expectedSize,
    const std::function<void(int, size_t)> &onProgress
) {
    Serial.printf("[OTA] Downloading %zu bytes from %s\r\n", expectedSize, downloadUrl.c_str());

    updateInProgress = true;

    // ESP32-S2 is single-core: heavy TLS/network I/O can starve other tasks,
    // preventing them from resetting the watchdog. Disable WDT for the duration.
    esp_task_wdt_deinit();

    auto cleanup = []() {
        updateInProgress = false;
        esp_task_wdt_init(30, true);
    };

    if (!hasEnoughMemory()) {
        cleanup();
        return false;
    }

    const esp_partition_t *nextPartition = esp_ota_get_next_update_partition(nullptr);
    if (nextPartition == nullptr) {
        Serial.println("[OTA] No OTA partition available");
        cleanup();
        return false;
    }

    esp_http_client_config_t config{};
    config.url = downloadUrl.c_str();
    config.timeout_ms = TIMEOUT_MS;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.buffer_size = CHUNK_SIZE;
    config.buffer_size_tx = 1024;  // GitHub CDN redirect URLs exceed the 512-byte default

    HttpClient client(config);
    if (!client) {
        Serial.println("[OTA] HTTP client init failed");
        cleanup();
        return false;
    }

    int statusCode = client.openWithRedirects();
    if (statusCode != 200) {
        Serial.printf("[OTA] HTTP status: %d\r\n", statusCode);
        cleanup();
        return false;
    }

    int contentLength = esp_http_client_get_content_length(client.handle);
    if (contentLength > 0 && static_cast<size_t>(contentLength) != expectedSize) {
        Serial.printf("[OTA] Size mismatch: expected %zu, got %d\r\n", expectedSize, contentLength);
        cleanup();
        return false;
    }

    if (!Update.begin(expectedSize, U_FLASH)) {
        Serial.printf("[OTA] Update.begin() failed: %s\r\n", Update.errorString());
        cleanup();
        return false;
    }

    Serial.printf("[OTA] Flashing to %s...\r\n", nextPartition->label);

    uint8_t buffer[CHUNK_SIZE];
    size_t totalRead = 0;
    unsigned long lastProgressLog = millis();

    while (totalRead < expectedSize) {
        int bytesRead = esp_http_client_read(client.handle, reinterpret_cast<char *>(buffer),
                                              std::min(static_cast<size_t>(CHUNK_SIZE), expectedSize - totalRead));

        if (bytesRead <= 0) {
            Serial.printf("[OTA] Download failed at %zu/%zu bytes (read returned %d)\r\n",
                          totalRead, expectedSize, bytesRead);
            Update.abort();
            cleanup();
            return false;
        }

        size_t bytesWritten = Update.write(buffer, bytesRead);
        if (bytesWritten != static_cast<size_t>(bytesRead)) {
            Serial.printf("[OTA] Flash write failed at %zu bytes\r\n", totalRead);
            Update.abort();
            cleanup();
            return false;
        }

        totalRead += bytesWritten;
        vTaskDelay(1);

        if (millis() - lastProgressLog > 1000) {
            int percent = (totalRead * 100) / expectedSize;
            if (onProgress) {
                onProgress(percent, totalRead);
            }
            lastProgressLog = millis();
        }
    }

    if (!Update.end(false)) {
        Serial.printf("[OTA] Update.end() failed: %s\r\n", Update.errorString());
        cleanup();
        return false;
    }

    Serial.printf("[OTA] Complete: %zu bytes flashed\r\n", totalRead);
    cleanup();
    return true;
}

bool OTAUpdater::confirmBoot() {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        Serial.printf("[OTA] Boot confirmation failed: %s\r\n", esp_err_to_name(err));
        return false;
    }
    Serial.println("[OTA] Boot confirmed");
    return true;
}

bool OTAUpdater::hasUnconfirmedUpdate() {
    const esp_partition_t *partition = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(partition, &state) != ESP_OK) {
        return false;
    }
    return state == ESP_OTA_IMG_NEW;
}

bool OTAUpdater::getRunningPartitionInfo(String &label, uint32_t &address) {
    const esp_partition_t *partition = esp_ota_get_running_partition();
    if (partition == nullptr) {
        return false;
    }
    label = String(partition->label);
    address = partition->address;
    return true;
}

void OTAUpdater::getMemoryInfo(uint32_t &freeHeap, uint32_t &minFreeHeap) {
    freeHeap = esp_get_free_heap_size();
    minFreeHeap = esp_get_minimum_free_heap_size();
}

bool OTAUpdater::hasEnoughMemory() {
    uint32_t freeHeap = esp_get_free_heap_size();
    if (freeHeap < MIN_FREE_HEAP) {
        Serial.printf("[OTA] Insufficient heap: %u (need %d)\r\n", freeHeap, MIN_FREE_HEAP);
        return false;
    }
    return true;
}

#endif  // ARDUINO
