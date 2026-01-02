//
// OTA Firmware Update Manager for ESP32-S3
//
// Handles GitHub release checking, firmware download, and safe OTA updates
// with rollback support
//

#pragma once

#include <Arduino.h>

#ifdef ARDUINO
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#endif

/**
 * Firmware release information from GitHub
 */
struct FirmwareInfo {
    String version; // Tag name (e.g., "v1.0.0")
    String name; // Release name
    String downloadUrl; // Direct download link to .bin file
    size_t size; // File size in bytes
    String releaseNotes; // Release notes body
    bool isValid; // Whether the structure contains valid data
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
    /**
   * Check GitHub for latest firmware release
   * @param owner GitHub repository owner
   * @param repo GitHub repository name
   * @param info [out] Populated with release information
   * @return true if successfully retrieved release info
   */
    static bool checkForUpdate(const char *owner, const char *repo, FirmwareInfo &info);

    /**
   * Download firmware from URL and flash to OTA partition
   * Streams data directly to flash to minimize memory usage
   * @param downloadUrl Direct download URL to .bin file
   * @param expectedSize Expected firmware size in bytes
   * @param onProgress Optional callback for progress updates (0-100)
   * @return true if update successful
   */
    static bool performUpdate(
        const String &downloadUrl,
        size_t expectedSize,
        std::function<void(int, size_t)> onProgress = nullptr
    );

    /**
   * Confirm successful boot after OTA update
   * Call this after verifying new firmware works correctly
   * Disables automatic rollback
   * @return true if confirmation successful
   */
    static bool confirmBoot();

    /**
   * Check if device is running NEW firmware (not yet confirmed)
   * @return true if running unconfirmed OTA update
   */
    static bool hasUnconfirmedUpdate();

    /**
   * Get information about currently running partition
   * @param label [out] Partition label (e.g., "app0")
   * @param address [out] Partition base address
   * @return true if successful
   */
    static bool getRunningPartitionInfo(String &label, uint32_t &address);

    /**
   * Get memory information for OTA operations
   * @param freeHeap [out] Free SRAM in bytes
   * @param minFreeHeap [out] Minimum free SRAM since boot
   * @param psramFree [out] Free PSRAM in bytes (0 if no PSRAM)
   */
    static void getMemoryInfo(uint32_t &freeHeap, uint32_t &minFreeHeap, uint32_t &psramFree);

    /**
   * Check if sufficient memory is available for OTA
   * Requires at least 65KB free heap
   * @return true if safe to perform OTA
   */
    static bool hasEnoughMemory();

private:
    // Configuration constants
    static constexpr int TIMEOUT_MS = 30000; // 30 seconds timeout (HTTPClient max)
    static constexpr int CHUNK_SIZE = 4096; // 4KB download chunks
    static constexpr int MIN_FREE_HEAP = 65536; // 64KB minimum required
    static constexpr int MAX_JSON_SIZE = 8192; // JSON document size (increased for GitHub API)

    /**
   * Initialize secure WiFi client with certificate bundle
   * @return Configured WiFiClientSecure
   */
    static WiFiClientSecure createSecureClient();
};