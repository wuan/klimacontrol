//
// OTA Firmware Update Manager for ESP32-S2
//
// Handles GitHub release checking, firmware download, and safe OTA updates
// with rollback support. Uses ESP-IDF esp_http_client for reliable HTTPS.
//

#pragma once

#include <Arduino.h>

#ifdef ARDUINO
#include <Update.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

namespace Config {
    class ConfigManager;
}

/**
 * Firmware release information from GitHub
 */
struct FirmwareInfo {
    String version = ""; // Tag name (e.g., "v1.0.0")
    String name = ""; // Release name
    String downloadUrl = ""; // Direct download link to .bin file
    size_t size = 0; // File size in bytes
    bool isValid = false; // Whether the structure contains valid data
    String errorMessage = ""; // Error description when check fails

    FirmwareInfo() = default;
};

/**
 * OTA Update Manager
 * Handles:
 * - Checking GitHub for new releases
 * - Downloading firmware with progress
 * - Flashing to OTA partition
 * - Safe boot confirmation with rollback support
 */
class OTAUpdater {
public:
    // State of the most recent background update check (see startBackgroundCheck).
    enum class CheckState : uint8_t {
        Idle,        // no check has run yet
        InProgress,  // a check is currently running on the worker task
        Done,        // last check completed; result is valid
        Failed       // last check failed; errorMessage is set
    };

    static bool checkForUpdate(const char *owner, const char *repo, FirmwareInfo &info);

    /**
     * Run checkForUpdate() on a dedicated FreeRTOS task and return immediately.
     *
     * checkForUpdate() blocks for the GitHub TLS round-trip (seconds, up to
     * TIMEOUT_MS); running it inline in an ESPAsyncWebServer callback would stall
     * the single AsyncTCP event task. Callers start a check, then poll
     * getCheckResult() for the outcome.
     *
     * @return true if a check was started; false if a check or update is already
     *         in progress, or the task could not be created.
     */
    static bool startBackgroundCheck(const char *owner, const char *repo);

    /**
     * Read the background check state and (if Done/Failed) a copy of the result.
     * Thread-safe snapshot of both values under one lock.
     */
    static CheckState getCheckResult(FirmwareInfo &infoOut);

    static bool performUpdate(
        const String &downloadUrl,
        size_t expectedSize,
        const std::function<void(int, size_t)> &onProgress = nullptr
    );

    /**
     * Start an OTA update for the firmware identified by the most recent
     * successful background check (see startBackgroundCheck / getCheckResult).
     *
     * The download URL and size are taken from the device's own check result —
     * a GitHub release for the compiled-in owner/repo — and never from the
     * caller. This is deliberate: clients must not be able to point the device
     * at an arbitrary binary. A check (CheckState::Done with a valid asset)
     * must have completed first.
     *
     * Like startBackgroundUpdate(), the multi-minute download runs on a
     * dedicated worker task so the HTTP handler can respond right away; on
     * success the worker schedules a restart via the supplied ConfigManager.
     *
     * @return true if the worker was started; false if no verified update is
     *         available, an update is already in progress, or the task could
     *         not be created.
     */
    static bool startBackgroundUpdateFromLatestCheck(Config::ConfigManager &config);

    static bool confirmBoot();
    static bool hasUnconfirmedUpdate();
    static bool getRunningPartitionInfo(String &label, uint32_t &address);
    static void getMemoryInfo(uint32_t &freeHeap, uint32_t &minFreeHeap);
    static bool hasEnoughMemory();
    static bool isUpdateInProgress() { return updateInProgress; }

private:
    // Spawn the OTA worker for an already-validated download URL/size.
    // Internal only: the URL must originate from a trusted source (the device's
    // own GitHub check), never directly from a client request.
    static bool startBackgroundUpdate(
        const String &downloadUrl,
        size_t expectedSize,
        Config::ConfigManager &config
    );

    static inline bool updateInProgress = false;
    static constexpr int TIMEOUT_MS = 30000;
    static constexpr int CHUNK_SIZE = 4096;
    static constexpr int MIN_FREE_HEAP = 65536;
    // Worker stack must hold the 4 KB chunk buffer plus the mbedTLS handshake
    // working set; sized to match the AsyncTCP task stack the update used to run on.
    static constexpr uint32_t UPDATE_TASK_STACK = 16384;
    // Check worker needs the TLS handshake working set + JSON parse buffers.
    static constexpr uint32_t CHECK_TASK_STACK = 16384;

    // Background-check result, guarded by checkResultMutex().
    static inline CheckState checkState = CheckState::Idle;
    static inline FirmwareInfo checkResult{};
#ifdef ARDUINO
    static SemaphoreHandle_t checkResultMutex();
    static void otaCheckTask(void *arg);  // FreeRTOS worker for startBackgroundCheck
#endif
};
