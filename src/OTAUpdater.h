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
    static bool checkForUpdate(const char *owner, const char *repo, FirmwareInfo &info);

    static bool performUpdate(
        const String &downloadUrl,
        size_t expectedSize,
        const std::function<void(int, size_t)> &onProgress = nullptr
    );

    /**
     * Run performUpdate() on a dedicated FreeRTOS task and return immediately.
     *
     * The firmware download blocks for minutes; running it inline in an
     * ESPAsyncWebServer request callback would stall the single AsyncTCP event
     * task (dropping connections / stalling the TCP stack). This spawns a
     * worker so the HTTP handler can respond right away. On success the worker
     * schedules a restart via the supplied ConfigManager.
     *
     * @return true if the worker was started; false if an update is already in
     *         progress or the task could not be created.
     */
    static bool startBackgroundUpdate(
        const String &downloadUrl,
        size_t expectedSize,
        Config::ConfigManager &config
    );

    static bool confirmBoot();
    static bool hasUnconfirmedUpdate();
    static bool getRunningPartitionInfo(String &label, uint32_t &address);
    static void getMemoryInfo(uint32_t &freeHeap, uint32_t &minFreeHeap);
    static bool hasEnoughMemory();
    static bool isUpdateInProgress() { return updateInProgress; }

private:
    static inline bool updateInProgress = false;
    static constexpr int TIMEOUT_MS = 30000;
    static constexpr int CHUNK_SIZE = 4096;
    static constexpr int MIN_FREE_HEAP = 65536;
    // Worker stack must hold the 4 KB chunk buffer plus the mbedTLS handshake
    // working set; sized to match the AsyncTCP task stack the update used to run on.
    static constexpr uint32_t UPDATE_TASK_STACK = 16384;
};
