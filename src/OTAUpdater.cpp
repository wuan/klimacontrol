//
// OTA Firmware Update Manager Implementation
// Uses ESP-IDF esp_http_client for reliable HTTPS with built-in cert bundle.
//

#include "OTAUpdater.h"
#include "Config.h"

#ifdef ARDUINO
#include <esp_http_client.h>
#include <esp_heap_caps.h>
#include "Log.h"
#include <WiFi.h>

static const char* TAG = "ota";

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
                ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
                return -1;
            }

            esp_http_client_fetch_headers(handle);
            int status = esp_http_client_get_status_code(handle);

            if (status == 301 || status == 302 || status == 307 || status == 308) {
                esp_http_client_close(handle);
                if (esp_http_client_set_redirection(handle) != ESP_OK) {
                    ESP_LOGE(TAG, "Redirect failed: no Location header");
                    return -1;
                }
                ESP_LOGI(TAG, "Following redirect (%d)...", status);
                continue;
            }
            return status;
        }
        ESP_LOGE(TAG, "Too many redirects");
        return -1;
    }
};

// ============================================================================
// Public Methods
// ============================================================================

bool OTAUpdater::checkForUpdate(const char *owner, const char *repo, FirmwareInfo &info) {
    info.isValid = false;

    // Mark OTA in progress for the duration of the check: the TLS connection to
    // api.github.com plus the JSON document together consume tens of KB and can
    // drive free heap below the network task's low-heap guard threshold, which
    // would otherwise restart the device mid-check. The flag is restored on
    // every return path by the RAII guard below.
    updateInProgress = true;
    struct InProgressGuard {
        ~InProgressGuard() { updateInProgress = false; }
    } guard;

    String apiUrl = String("https://api.github.com/repos/") + owner + "/" + repo + "/releases/latest";
    ESP_LOGI(TAG, "Checking: %s", apiUrl.c_str());

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
    filter["assets"] = true;
    // Skip "body" (release notes) to save heap - can be hundreds of KB

    JsonDocument doc;
    DeserializationError jsonErr = deserializeJson(doc, reader,
                                                   DeserializationOption::Filter(filter));
    if (jsonErr) {
        info.errorMessage = String("JSON parse error: ") + jsonErr.c_str();
        return false;
    }

    // Drain any remaining response body so esp_http_client_close() doesn't
    // block trying to consume it, then close the connection to free TLS
    // buffers (~32KB) before we return.
    char discard[256];
    while (esp_http_client_read(client.handle, discard, sizeof(discard)) > 0) {}
    esp_http_client_close(client.handle);

    info.version = doc["tag_name"].as<String>();
    info.name = doc["name"].as<String>();
    // Release notes intentionally not fetched to save heap

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
            ESP_LOGI(TAG, "Release %s: %s (%zu bytes)",
                     info.version.c_str(), assetName.c_str(), info.size);
            break;
        }
    }

    // Free the JSON document before returning — the release response can be
    // large and holding it keeps heap pressure high for the subsequent OTA
    // download which needs all the memory it can get.
    doc.clear();
    doc.shrinkToFit();

    if (!info.isValid) {
        info.errorMessage = "No .bin file found in release assets";
        return false;
    }

    return true;
}

bool OTAUpdater::performUpdate(
    const String &downloadUrl,
    size_t expectedSize,
    const std::function<void(int, size_t)> &onProgress
) {
    ESP_LOGI(TAG, "Downloading %zu bytes from %s", expectedSize, downloadUrl.c_str());

    updateInProgress = true;

    // Runs on a dedicated worker task (startBackgroundUpdate) that is NOT
    // watchdog-subscribed, and the per-chunk vTaskDelay(1) below lets the
    // subscribed Network/SensorMonitor tasks keep feeding their own watchdogs.
    // So there is nothing to disable here.
    auto cleanup = []() {
        updateInProgress = false;
    };

    if (!hasEnoughMemory()) {
        cleanup();
        return false;
    }

    const esp_partition_t *nextPartition = esp_ota_get_next_update_partition(nullptr);
    if (nextPartition == nullptr) {
        ESP_LOGE(TAG, "No OTA partition available");
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
        ESP_LOGE(TAG, "HTTP client init failed");
        cleanup();
        return false;
    }

    int statusCode = client.openWithRedirects();
    if (statusCode != 200) {
        ESP_LOGE(TAG, "HTTP status: %d", statusCode);
        cleanup();
        return false;
    }

    int contentLength = esp_http_client_get_content_length(client.handle);
    if (contentLength > 0 && static_cast<size_t>(contentLength) != expectedSize) {
        ESP_LOGE(TAG, "Size mismatch: expected %zu, got %d", expectedSize, contentLength);
        cleanup();
        return false;
    }

    if (!Update.begin(expectedSize, U_FLASH)) {
        ESP_LOGE(TAG, "Update.begin() failed: %s", Update.errorString());
        cleanup();
        return false;
    }

    ESP_LOGI(TAG, "Flashing to %s...", nextPartition->label);

    uint8_t buffer[CHUNK_SIZE];
    size_t totalRead = 0;
    unsigned long lastProgressLog = millis();

    while (totalRead < expectedSize) {
        int bytesRead = esp_http_client_read(client.handle, reinterpret_cast<char *>(buffer),
                                              std::min(static_cast<size_t>(CHUNK_SIZE), expectedSize - totalRead));

        if (bytesRead <= 0) {
            ESP_LOGE(TAG, "Download failed at %zu/%zu bytes (read returned %d)",
                     totalRead, expectedSize, bytesRead);
            Update.abort();
            cleanup();
            return false;
        }

        size_t bytesWritten = Update.write(buffer, bytesRead);
        if (bytesWritten != static_cast<size_t>(bytesRead)) {
            ESP_LOGE(TAG, "Flash write failed at %zu bytes", totalRead);
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
        ESP_LOGE(TAG, "Update.end() failed: %s", Update.errorString());
        cleanup();
        return false;
    }

    ESP_LOGI(TAG, "Complete: %zu bytes flashed", totalRead);
    cleanup();
    return true;
}

namespace {
    // Heap-allocated payload handed to the OTA worker task. Owned by the worker,
    // which deletes it before exiting.
    struct OtaJob {
        String url;
        size_t size;
        Config::ConfigManager *config;
    };

    void otaWorkerTask(void *arg) {
        auto *job = static_cast<OtaJob *>(arg);

        bool success = OTAUpdater::performUpdate(job->url, job->size,
            [](int percent, size_t bytes) {
                ESP_LOGI(TAG, "Progress: %d%% (%zu bytes)", percent, bytes);
            });

        if (success) {
            ESP_LOGI(TAG, "OTA update successful, scheduling restart...");
            job->config->requestRestart(1000);
        } else {
            ESP_LOGE(TAG, "OTA update failed");
        }

        delete job;
        vTaskDelete(nullptr);
    }
}

SemaphoreHandle_t OTAUpdater::checkResultMutex() {
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();
    return m;
}

namespace {
    // Heap-allocated payload for the check worker. Owner/repo are copied so the
    // worker doesn't depend on the caller's string lifetimes.
    struct CheckJob {
        String owner;
        String repo;
    };
}

void OTAUpdater::otaCheckTask(void *arg) {
    auto *job = static_cast<CheckJob *>(arg);

    FirmwareInfo info;
    bool ok = checkForUpdate(job->owner.c_str(), job->repo.c_str(), info);

    SemaphoreHandle_t m = checkResultMutex();
    if (m && xSemaphoreTake(m, portMAX_DELAY) == pdTRUE) {
        checkResult = info;
        checkState = ok ? CheckState::Done : CheckState::Failed;
        xSemaphoreGive(m);
    }

    delete job;
    // High-water mark = smallest free stack (in bytes) seen on this task. If this
    // ever approaches 0, CHECK_TASK_STACK is too small and must be raised.
    ESP_LOGD(TAG, "ota_check stack high-water mark: %u bytes free",
             (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    vTaskDelete(nullptr);
}

bool OTAUpdater::startBackgroundCheck(const char *owner, const char *repo) {
    SemaphoreHandle_t m = checkResultMutex();
    if (!m) return false;

    // A FreeRTOS task stack must be allocated from internal SRAM (it can never
    // live in PSRAM), and it must be one contiguous block. esp_get_free_heap_size()
    // counts PSRAM too, so it can look healthy while internal SRAM is too
    // fragmented to hand out CHECK_TASK_STACK bytes - that is exactly what makes
    // xTaskCreate() fail here. Check the largest free *internal* block up front so
    // we fail fast with an actionable number instead of an opaque create error.
    size_t largestInternal =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (largestInternal < CHECK_TASK_STACK + TASK_CREATE_HEADROOM) {
        ESP_LOGW(TAG,
                 "Check not started: largest free internal block %u < %u needed "
                 "(internal SRAM fragmented)",
                 (unsigned)largestInternal,
                 (unsigned)(CHECK_TASK_STACK + TASK_CREATE_HEADROOM));
        return false;
    }

    if (xSemaphoreTake(m, portMAX_DELAY) == pdTRUE) {
        // Don't start if a check is already running or a real update is underway.
        if (checkState == CheckState::InProgress || updateInProgress) {
            xSemaphoreGive(m);
            ESP_LOGW(TAG, "Check not started (busy)");
            return false;
        }
        checkState = CheckState::InProgress;
        checkResult = FirmwareInfo{};
        xSemaphoreGive(m);
    } else {
        return false;
    }

    auto *job = new CheckJob{String(owner), String(repo)};
    BaseType_t created = xTaskCreate(otaCheckTask, "ota_check",
                                     CHECK_TASK_STACK, job, 1, nullptr);
    if (created != pdPASS) {
        ESP_LOGE(TAG,
                 "Failed to create OTA check task (largest free internal block %u)",
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                            MALLOC_CAP_8BIT));
        // Roll back to Failed so we don't get stuck in InProgress forever.
        if (xSemaphoreTake(m, portMAX_DELAY) == pdTRUE) {
            checkState = CheckState::Failed;
            xSemaphoreGive(m);
        }
        delete job;
        return false;
    }

    ESP_LOGI(TAG, "OTA check task started");
    return true;
}

OTAUpdater::CheckState OTAUpdater::getCheckResult(FirmwareInfo &infoOut) {
    SemaphoreHandle_t m = checkResultMutex();
    if (m && xSemaphoreTake(m, portMAX_DELAY) == pdTRUE) {
        CheckState state = checkState;
        infoOut = checkResult;
        xSemaphoreGive(m);
        return state;
    }
    return CheckState::Idle;
}

bool OTAUpdater::startBackgroundUpdateFromLatestCheck(Config::ConfigManager &config) {
    FirmwareInfo info;
    CheckState state = getCheckResult(info);

    if (state != CheckState::Done || !info.isValid ||
        info.downloadUrl.isEmpty() || info.size == 0) {
        ESP_LOGW(TAG, "Update refused: no verified update available (run a check first)");
        return false;
    }

    // Defense in depth: the URL came from our own GitHub check, but reject
    // anything that isn't a github.com release download before flashing it.
    // GitHub serves release assets from github.com (which then 302-redirects to
    // its CDN); esp_http_client follows that redirect internally.
    if (!info.downloadUrl.startsWith("https://github.com/")) {
        ESP_LOGE(TAG, "Update refused: unexpected download host in %s", info.downloadUrl.c_str());
        return false;
    }

    return startBackgroundUpdate(info.downloadUrl, info.size, config);
}

bool OTAUpdater::startBackgroundUpdate(const String &downloadUrl, size_t expectedSize,
                                       Config::ConfigManager &config) {
    // Claim the in-progress slot up front so a rapid second request can't spawn
    // a concurrent worker in the window before performUpdate() sets the flag.
    // The test-and-set must be atomic: two callers racing on a plain read+write
    // could both observe false and both spawn a worker (TOCTOU).
    bool expected = false;
    if (!updateInProgress.compare_exchange_strong(expected, true)) {
        ESP_LOGW(TAG, "Update already in progress - ignoring request");
        return false;
    }

    auto *job = new OtaJob{downloadUrl, expectedSize, &config};

    BaseType_t created = xTaskCreate(otaWorkerTask, "ota_update",
                                     UPDATE_TASK_STACK, job, 1, nullptr);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA worker task");
        updateInProgress = false;
        delete job;
        return false;
    }

    ESP_LOGI(TAG, "OTA worker task started");
    return true;
}

bool OTAUpdater::confirmBoot() {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Boot confirmation failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "Boot confirmed");
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
        ESP_LOGW(TAG, "Insufficient heap: %u (need %d)", freeHeap, MIN_FREE_HEAP);
        return false;
    }
    return true;
}

#endif  // ARDUINO
