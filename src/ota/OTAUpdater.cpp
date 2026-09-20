//
// OTA Firmware Update Manager Implementation
// Uses ESP-IDF esp_http_client for reliable HTTPS with built-in cert bundle.
//

#include "OTAUpdater.h"
#include "Config.h"
#include "OTAConfig.h"
#include "ota/http/RedirectScheme.h"
#include "VersionCompare.h"

#ifdef ARDUINO
#include <esp_http_client.h>
#include <esp_heap_caps.h>
#include "Log.h"
#include "ota/http/HttpClient.h"
#include "ota/http/HttpEventHandler.h"
#include "ota/http/HttpReader.h"
#include "ota/http/TlsAllocator.h"
#include <cstring>

static constexpr const char* const TAG = "ota";

// ============================================================================
// Activity claim
// ============================================================================

bool OTAUpdater::claimActivity(Activity want) {
    Activity expected = Activity::None;
    return activity.compare_exchange_strong(expected, want);
}

// ============================================================================
// Public Methods
// ============================================================================

bool OTAUpdater::checkForUpdate(const char *owner, const char *repo, FirmwareInfo &info) {
    info.isValid = false;

    // Mark OTA busy for the duration of the check: the TLS connection to
    // api.github.com plus the JSON document together consume tens of KB and can
    // drive free heap below the network task's low-heap guard threshold, which
    // would otherwise restart the device mid-check.
    //
    // The claim is conditional and self-restoring: when called from
    // otaCheckTask the slot is already held (by startBackgroundCheck), so we
    // must not release it here — releasing unconditionally is what previously
    // let a check cancel a concurrent update's claim.
    const bool claimed = claimActivity(Activity::Checking);
    struct ActivityGuard {
        bool owned;
        ~ActivityGuard() { if (owned) releaseActivity(); }
    } guard{claimed};

    String apiUrl = String(OTA_GITHUB_API_HOST) + "repos/" + owner + "/" + repo + "/releases/latest";
    ESP_LOGI(TAG, "Checking: %s", apiUrl.c_str());

    esp_http_client_config_t config{};
    config.url = apiUrl.c_str();
    config.timeout_ms = TIMEOUT_MS;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    // Captures Location headers so openWithRedirects() can reject a redirect
    // that would downgrade the transport to cleartext.
    config.event_handler = OTA::Http::captureRedirectLocation;
    // Same reasoning as the download path — see OTA::Http::kHttpRxBuffer.
    // GitHub emits multi-kilobyte header blocks and we should not depend on
    // its current TLS record framing to make the 512-byte default work.
    config.buffer_size = OTA::Http::kHttpRxBuffer;

    OTA::Http::HttpClient client(config);
    if (!client) {
        info.errorMessage = "HTTP client init failed";
        return false;
    }

    esp_http_client_set_header(client.raw(), "Accept", "application/vnd.github.v3+json");
    esp_http_client_set_header(client.raw(), "User-Agent", "ESP32-OTA/1.0");

    int statusCode = client.openWithRedirects();
    if (statusCode != 200) {
        info.errorMessage = statusCode < 0 ? "Connection failed" : "HTTP status " + String(statusCode);
        return false;
    }

    OTA::Http::HttpReader reader{client.raw()};

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
    while (esp_http_client_read(client.raw(), discard, sizeof(discard)) > 0) {}
    esp_http_client_close(client.raw());

    info.version = doc["tag_name"].as<String>();
    info.name = doc["name"].as<String>();
    // Release notes intentionally not fetched to save heap

    if (info.version.isEmpty()) {
        info.errorMessage = "No tag_name in release response";
        return false;
    }

    // Require the exact expected asset name rather than the first thing ending
    // in ".bin". A release that also carries a filesystem image, a bootloader
    // blob, or a build for a different board would otherwise have one of those
    // flashed as the application: such an image can still pass
    // esp_ota_set_boot_partition()'s checksum/SHA-256 verification (it is a
    // valid image, just not one this board can run) and would boot-loop the
    // device into needing USB recovery.
    for (JsonObject asset : doc["assets"].as<JsonArray>()) {
        auto assetName = asset["name"].as<String>();
        if (Support::isExpectedFirmwareAsset(assetName.c_str())) {
            info.downloadUrl = asset["browser_download_url"].as<String>();
            info.size = asset["size"].as<size_t>();
            info.isValid = true;
            ESP_LOGI(TAG, "Release %s: %s (%zu bytes)",
                     info.version.c_str(), assetName.c_str(), info.size);
            break;
        }
        if (assetName.endsWith(".bin")) {
            ESP_LOGD(TAG, "Ignoring asset %s (expecting %s)",
                     assetName.c_str(), OTA_FIRMWARE_ASSET);
        }
    }

    // Free the JSON document before returning — the release response can be
    // large and holding it keeps heap pressure high for the subsequent OTA
    // download which needs all the memory it can get.
    doc.clear();
    doc.shrinkToFit();

    if (!info.isValid) {
        info.errorMessage = "Release has no " OTA_FIRMWARE_ASSET " asset";
        return false;
    }

    return true;
}

bool OTAUpdater::isUpdateAvailable(const FirmwareInfo &info) {
    return info.isValid && Support::isNewerVersion(FIRMWARE_VERSION, info.version.c_str());
}

bool OTAUpdater::performUpdate(
    const String &downloadUrl,
    size_t expectedSize,
    const std::function<void(int, size_t)> &onProgress
) {
    ESP_LOGI(TAG, "Downloading %zu bytes from %s", expectedSize, downloadUrl.c_str());

    setUpdateState(UpdateState::Downloading, 0, 0, nullptr);

    // Report the failure to the client as well as the log. Without this a
    // failed update is invisible to the UI: the POST has already returned
    // "starting" and there is nothing else the client can poll.
    auto fail = [](const char *message) {
        ESP_LOGE(TAG, "%s", message);
        setUpdateState(UpdateState::Failed, 0, 0, message);
        return false;
    };

    // Runs on a dedicated worker task (see begin()) that is NOT
    // watchdog-subscribed, and the per-chunk vTaskDelay(1) below lets the
    // subscribed Network/SensorMonitor tasks keep feeding their own watchdogs.
    // So there is nothing to disable here.

    if (!hasEnoughMemory()) {
        return fail("Not enough free internal memory for OTA");
    }

    const esp_partition_t *nextPartition = esp_ota_get_next_update_partition(nullptr);
    if (nextPartition == nullptr) {
        return fail("No OTA partition available");
    }

    esp_http_client_config_t config{};
    config.url = downloadUrl.c_str();
    config.timeout_ms = TIMEOUT_MS;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    // Captures Location headers so openWithRedirects() can reject a redirect
    // that would downgrade the transport to cleartext.
    config.event_handler = OTA::Http::captureRedirectLocation;
    // RX buffer must hold GitHub's whole 302 redirect header block (~5 KB, with a
    // ~3.6 KB Content-Security-Policy line) in one read, or fetch_headers() stalls
    // and openWithRedirects() returns -1 ("No HTTP response") without ever
    // following the redirect — see OTA::Http::kHttpRxBuffer. The default 512
    // and the CHUNK_SIZE (4096) are both smaller than that block.
    config.buffer_size = OTA::Http::kHttpRxBuffer;
    // github.com 302-redirects release downloads to a signed CDN URL
    // (release-assets.githubusercontent.com) whose path+query carries the full
    // AWS/JWT signature (~860 bytes). esp_http_client builds the entire redirect
    // request line into this TX buffer in one shot; 2048 leaves comfortable
    // headroom over the default 512.
    config.buffer_size_tx = 2048;

    OTA::Http::HttpClient client(config);
    if (!client) {
        return fail("HTTP client init failed");
    }

    int statusCode = client.openWithRedirects();
    if (statusCode != 200) {
        ESP_LOGE(TAG, "HTTP status: %d", statusCode);
        setUpdateState(UpdateState::Failed, 0, 0,
                       String("Download failed (HTTP " + String(statusCode) + ")").c_str());
        return false;
    }

    int contentLength = esp_http_client_get_content_length(client.raw());
    if (contentLength > 0 && static_cast<size_t>(contentLength) != expectedSize) {
        ESP_LOGE(TAG, "Size mismatch: expected %zu, got %d", expectedSize, contentLength);
        return fail("Download size does not match the release metadata");
    }

    if (!Update.begin(expectedSize, U_FLASH)) {
        ESP_LOGE(TAG, "Update.begin() failed: %s", Update.errorString());
        setUpdateState(UpdateState::Failed, 0, 0, Update.errorString());
        return false;
    }

    // Internal heap *after* the TLS handshake and the client's buffer
    // allocations, i.e. the trough of the whole download. This is the number to
    // tune MIN_FREE_INTERNAL / MIN_LARGEST_INTERNAL_BLOCK against: the pre-flight
    // gate can only guess how much the session will cost, this measures it.
    ESP_LOGI(TAG, "Internal heap with TLS session up: free=%u largest=%u",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    ESP_LOGI(TAG, "Flashing to %s...", nextPartition->label);

    uint8_t buffer[CHUNK_SIZE];
    size_t totalRead = 0;
    unsigned long lastProgressLog = millis();

    while (totalRead < expectedSize) {
        int bytesRead = esp_http_client_read(client.raw(), reinterpret_cast<char *>(buffer),
                                              std::min(static_cast<size_t>(CHUNK_SIZE), expectedSize - totalRead));

        if (bytesRead <= 0) {
            ESP_LOGE(TAG, "Download failed at %zu/%zu bytes (read returned %d)",
                     totalRead, expectedSize, bytesRead);
            Update.abort();
            setUpdateState(UpdateState::Failed, (int)((totalRead * 100) / expectedSize), totalRead,
                           "Connection lost during download");
            return false;
        }

        size_t bytesWritten = Update.write(buffer, bytesRead);
        if (bytesWritten != static_cast<size_t>(bytesRead)) {
            ESP_LOGE(TAG, "Flash write failed at %zu bytes", totalRead);
            Update.abort();
            setUpdateState(UpdateState::Failed, (int)((totalRead * 100) / expectedSize), totalRead,
                           "Flash write failed");
            return false;
        }

        totalRead += bytesWritten;
        vTaskDelay(1);

        if (millis() - lastProgressLog > 1000) {
            int percent = (totalRead * 100) / expectedSize;
            setUpdateState(UpdateState::Downloading, percent, totalRead, nullptr);
            if (onProgress) {
                onProgress(percent, totalRead);
            }
            lastProgressLog = millis();
        }
    }

    if (!Update.end(false)) {
        ESP_LOGE(TAG, "Update.end() failed: %s", Update.errorString());
        setUpdateState(UpdateState::Failed, 100, totalRead, Update.errorString());
        return false;
    }

    ESP_LOGI(TAG, "Complete: %zu bytes flashed", totalRead);
    setUpdateState(UpdateState::Success, 100, totalRead, nullptr);
    return true;
}

SemaphoreHandle_t OTAUpdater::stateMutex() {
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();
    return m;
}

void OTAUpdater::setUpdateState(UpdateState state, int percent, size_t bytes, const char *error) {
    SemaphoreHandle_t m = stateMutex();
    if (m && xSemaphoreTake(m, portMAX_DELAY) == pdTRUE) {
        updateState = state;
        updatePercent = percent;
        updateBytes = bytes;
        updateError = error ? error : "";
        xSemaphoreGive(m);
    }
}

// ============================================================================
// Parked worker tasks
//
// Both tasks are created once by begin() and then block forever on a task
// notification. They are never deleted, which is what makes their static
// stacks safe to reuse across requests: vTaskDelete() only queues a task for
// reclamation by the idle task, so recreating a task on the same StaticTask_t
// before idle has run inserts a TCB into the ready list while it is still
// linked into xTasksWaitingTermination — scheduler list corruption. The
// previous design cleared its busy flag *before* self-deleting, so a client
// retrying immediately after a failed update (the expected reaction) could hit
// exactly that window.
// ============================================================================

void OTAUpdater::otaCheckTask(void *) {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        FirmwareInfo info;
        bool ok = checkForUpdate(pendingOwner, pendingRepo, info);

        SemaphoreHandle_t m = stateMutex();
        if (m && xSemaphoreTake(m, portMAX_DELAY) == pdTRUE) {
            checkResult = info;
            checkState = ok ? CheckState::Done : CheckState::Failed;
            xSemaphoreGive(m);
        }

        // High-water mark = smallest free stack (in bytes) seen on this task. If
        // this ever approaches 0, CHECK_TASK_STACK is too small and must be raised.
        ESP_LOGI(TAG, "ota_check stack high-water mark: %u bytes free",
                 (unsigned)uxTaskGetStackHighWaterMark(nullptr));

        // Release last: the slot is the gate that lets the next request in, so
        // it must not open until this iteration has fully finished touching
        // shared state.
        releaseActivity();
    }
}

void OTAUpdater::otaWorkerTask(void *) {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        bool success = performUpdate(pendingUpdate.downloadUrl, pendingUpdate.size,
            [](int percent, size_t bytes) {
                ESP_LOGI(TAG, "Progress: %d%% (%zu bytes)", percent, bytes);
            });

        if (success) {
            ESP_LOGI(TAG, "OTA update successful, scheduling restart...");
            if (pendingConfig) {
                pendingConfig->requestRestart(1000);
            }
        } else {
            ESP_LOGE(TAG, "OTA update failed");
        }

        // High-water mark = smallest free stack (in bytes) seen on this task.
        // If this ever approaches 0, UPDATE_TASK_STACK is too small; the worker
        // runs the TLS download + flash-write loop so it needs more than the
        // check task. Logged here so the static stack can be re-tuned.
        ESP_LOGI(TAG, "ota_update stack high-water mark: %u bytes free",
                 (unsigned)uxTaskGetStackHighWaterMark(nullptr));

        releaseActivity();
    }
}

void OTAUpdater::confirmRunningImage() {
    // With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y a freshly flashed image is
    // marked ESP_OTA_IMG_NEW by esp_ota_set_boot_partition(); the bootloader
    // promotes it to ESP_OTA_IMG_PENDING_VERIFY on first boot and, if it is
    // still PENDING_VERIFY at the *next* boot, marks it ABORTED and reverts to
    // the previous partition. See the header for why this runs at the end of
    // setup() rather than the start.
    if (!hasUnconfirmedUpdate()) {
        return; // already confirmed — the common case
    }
    ESP_LOGI(TAG, "Running a newly flashed image — confirming it to cancel rollback");
    confirmBoot();
}

void OTAUpdater::begin() {
    if (checkTaskHandle == nullptr) {
        checkTaskHandle = xTaskCreateStatic(otaCheckTask, "ota_check", CHECK_TASK_STACK,
                                            nullptr, 1, otaCheckStack, &otaCheckTCB);
    }
    if (updateTaskHandle == nullptr) {
        updateTaskHandle = xTaskCreateStatic(otaWorkerTask, "ota_update", UPDATE_TASK_STACK,
                                             nullptr, 1, otaUpdateStack, &otaUpdateTCB);
    }

    if (checkTaskHandle == nullptr || updateTaskHandle == nullptr) {
        // xTaskCreateStatic() only returns NULL if the buffers are null, so this
        // is a programming error rather than a heap condition — but the OTA
        // endpoints would silently do nothing, so say so loudly.
        ESP_LOGE(TAG, "OTA worker tasks could not be created - OTA is unavailable");
    } else {
        ESP_LOGI(TAG, "OTA workers ready (check %u B, update %u B static stacks)",
                 (unsigned)CHECK_TASK_STACK, (unsigned)UPDATE_TASK_STACK);
    }
}

bool OTAUpdater::startBackgroundCheck(const char *owner, const char *repo) {
    if (checkTaskHandle == nullptr) {
        ESP_LOGE(TAG, "Check not started: OTAUpdater::begin() has not run");
        return false;
    }

    // One atomic claim gates both activities, so a check can never start
    // alongside an update (or vice versa).
    if (!claimActivity(Activity::Checking)) {
        ESP_LOGW(TAG, "Check not started (busy)");
        return false;
    }

    SemaphoreHandle_t m = stateMutex();
    if (m && xSemaphoreTake(m, portMAX_DELAY) == pdTRUE) {
        checkState = CheckState::InProgress;
        checkResult = FirmwareInfo{};
        xSemaphoreGive(m);
    }

    // Safe without a lock: the claim above excludes every other writer until
    // the worker releases the slot.
    strlcpy(pendingOwner, owner, sizeof(pendingOwner));
    strlcpy(pendingRepo, repo, sizeof(pendingRepo));

    xTaskNotifyGive(checkTaskHandle);
    ESP_LOGI(TAG, "OTA check requested");
    return true;
}

OTAUpdater::CheckState OTAUpdater::getCheckResult(FirmwareInfo &infoOut) {
    SemaphoreHandle_t m = stateMutex();
    if (m && xSemaphoreTake(m, portMAX_DELAY) == pdTRUE) {
        CheckState state = checkState;
        infoOut = checkResult;
        xSemaphoreGive(m);
        return state;
    }
    return CheckState::Idle;
}

OTAUpdater::UpdateState OTAUpdater::getUpdateProgress(int &percentOut, size_t &bytesOut,
                                                      String &errorOut) {
    SemaphoreHandle_t m = stateMutex();
    if (m && xSemaphoreTake(m, portMAX_DELAY) == pdTRUE) {
        UpdateState state = updateState;
        percentOut = updatePercent;
        bytesOut = updateBytes;
        errorOut = updateError;
        xSemaphoreGive(m);
        return state;
    }
    return UpdateState::Idle;
}

bool OTAUpdater::startBackgroundUpdateFromLatestCheck(
    Config::ConfigManager &config,
    bool allowReinstall
) {
    FirmwareInfo info;
    CheckState state = getCheckResult(info);

    if (state != CheckState::Done || !info.isValid ||
        info.downloadUrl.isEmpty() || info.size == 0) {
        ESP_LOGW(TAG, "Update refused: no verified update available (run a check first)");
        return false;
    }

    // Strict-greater default; semver-equal requires allow_reinstall. The
    // ordering predicate (not textual inequality) is what makes an untagged
    // developer build (FIRMWARE_VERSION "v1.2.3-4-gabc1234") refuse the v1.2.3
    // release by default — otherwise the device would have offered, and
    // installed, a downgrade while calling it an update.
    if (!Support::isReinstallOrNewer(FIRMWARE_VERSION, info.version.c_str(), allowReinstall)) {
        ESP_LOGW(TAG, "Update refused: %s is not newer than running %s",
                 info.version.c_str(), FIRMWARE_VERSION);
        return false;
    }

    // Protect the rollback target the spec already preserves: an unconfirmed
    // update means the partition we'd overwrite is the only one still pointing
    // at the previously known-good image. Reinstall writes to the same
    // partition the regular worker targets, so the same protection applies.
    if (hasUnconfirmedUpdate()) {
        ESP_LOGW(TAG, "Reinstall refused: a pending update has not yet been confirmed");
        return false;
    }

    // Defense in depth: the URL came from our own GitHub check, but reject
    // anything that isn't a github.com release download before flashing it.
    // GitHub serves release assets from github.com (which then 302-redirects to
    // its CDN); openWithRedirects() enforces HTTPS on those later hops.
    if (!info.downloadUrl.startsWith(OTA_GITHUB_RELEASE_HOST)) {
        ESP_LOGE(TAG, "Update refused: unexpected download host in %s", info.downloadUrl.c_str());
        return false;
    }

    return startBackgroundUpdate(info, config);
}

bool OTAUpdater::startBackgroundUpdate(const FirmwareInfo &info, Config::ConfigManager &config) {
    if (updateTaskHandle == nullptr) {
        ESP_LOGE(TAG, "Update not started: OTAUpdater::begin() has not run");
        return false;
    }

    // Claim the slot before touching the job payload. The single
    // compare-exchange is what prevents two concurrent workers *and* a
    // concurrent check.
    if (!claimActivity(Activity::Updating)) {
        ESP_LOGW(TAG, "Update already in progress - ignoring request");
        return false;
    }

    pendingUpdate = info;
    pendingConfig = &config;
    setUpdateState(UpdateState::Downloading, 0, 0, nullptr);

    xTaskNotifyGive(updateTaskHandle);
    ESP_LOGI(TAG, "OTA update requested (%s, %zu bytes)", info.version.c_str(), info.size);
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
    // PENDING_VERIFY, not NEW. ESP_OTA_IMG_NEW exists only between
    // esp_ota_set_boot_partition() and the next boot; the bootloader promotes it
    // to ESP_OTA_IMG_PENDING_VERIFY before handing control to the app (see
    // esp_flash_partitions.h). Testing for NEW here therefore always returned
    // false, so nothing could ever tell that the running image was unconfirmed.
    return state == ESP_OTA_IMG_PENDING_VERIFY;
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

bool OTAUpdater::getOtherPartitionVersion(String &versionOut) {
    versionOut = "";
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == nullptr) return false;
    const esp_partition_t *other = esp_ota_get_next_update_partition(running);
    if (other == nullptr) return false;
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(other, &desc) != ESP_OK) {
        return false;
    }
    versionOut = String(desc.version);
    return true;
}

bool OTAUpdater::rollbackToOtherPartition(Config::ConfigManager &config) {
    if (isUpdateInProgress()) {
        ESP_LOGW(TAG, "Rollback refused: an OTA check or update is already running");
        return false;
    }
    if (hasUnconfirmedUpdate()) {
        ESP_LOGW(TAG, "Rollback refused: a pending update has not yet been confirmed");
        return false;
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == nullptr) return false;
    const esp_partition_t *other = esp_ota_get_next_update_partition(running);
    if (other == nullptr) return false;
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(other, &desc) != ESP_OK) {
        ESP_LOGW(TAG, "Rollback refused: the other partition has no readable image header");
        return false;
    }
    esp_err_t err = esp_ota_set_boot_partition(other);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "Rollback to %s (%s) scheduled; restarting", other->label, desc.version);
    config.requestRestart(1000);
    return true;
}

void OTAUpdater::getMemoryInfo(uint32_t &freeHeap, uint32_t &minFreeHeap) {
    freeHeap = esp_get_free_heap_size();
    minFreeHeap = esp_get_minimum_free_heap_size();
}

bool OTAUpdater::hasEnoughMemory() {
    // Internal SRAM only — see MIN_FREE_INTERNAL. esp_get_free_heap_size()
    // includes PSRAM on this board and would pass unconditionally.
    uint32_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    if (internalFree < MIN_FREE_INTERNAL || largestBlock < MIN_LARGEST_INTERNAL_BLOCK) {
        ESP_LOGW(TAG, "Insufficient internal heap: free=%u (need %u), largest block=%u (need %u)",
                 internalFree, MIN_FREE_INTERNAL, largestBlock, MIN_LARGEST_INTERNAL_BLOCK);
        return false;
    }
    return true;
}

#endif  // ARDUINO
