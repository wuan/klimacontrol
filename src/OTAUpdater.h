//
// OTA Firmware Update Manager for ESP32-S2
//
// Handles GitHub release checking, firmware download, and safe OTA updates
// with rollback support. Uses ESP-IDF esp_http_client for reliable HTTPS.
//

#pragma once

#include <Arduino.h>
#include <atomic>

#ifdef ARDUINO
#include <Update.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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
    static bool isUpdateInProgress() { return updateInProgress.load(); }

private:
    // Spawn the OTA worker for an already-validated download URL/size.
    // Internal only: the URL must originate from a trusted source (the device's
    // own GitHub check), never directly from a client request.
    static bool startBackgroundUpdate(
        const String &downloadUrl,
        size_t expectedSize,
        Config::ConfigManager &config
    );

    // Guards against concurrent OTA workers and marks heap-heavy operations so
    // the network task's low-heap guard won't reboot mid-update. Atomic because
    // it is claimed/released across the AsyncTCP, check, and update tasks; the
    // update slot is claimed with a single compare-exchange to close the
    // check-then-set TOCTOU window in startBackgroundUpdate().
    static inline std::atomic<bool> updateInProgress{false};
    static constexpr int TIMEOUT_MS = 30000;
    static constexpr int CHUNK_SIZE = 4096;
    static constexpr int MIN_FREE_HEAP = 65536;
    // Worker stack must hold the 4 KB chunk buffer plus the mbedTLS handshake
    // working set; sized to match the AsyncTCP task stack the update used to run on.
    static constexpr uint32_t UPDATE_TASK_STACK = 16384;
    // Check task needs the TLS handshake working set + JSON parse buffers.
    // The large mbedTLS buffers are offloaded to PSRAM (esp_mbedtls_mem_calloc),
    // so the stack itself stays modest - 8 KB matches Espressif's HTTPS-over-TLS
    // task examples. The check runs on the shared OTA task stack below (sized for
    // the larger update worker), so it simply uses the first 8 KB of that buffer.
    // otaCheckTask logs its stack high-water mark so this can be re-tuned.
    static constexpr uint32_t CHECK_TASK_STACK = 8192;

#ifdef ARDUINO
    // Statically-reserved stack + TCB shared by BOTH OTA tasks (the background
    // check and the update worker).
    //
    // A FreeRTOS task stack must come from one contiguous block of *internal*
    // SRAM (never PSRAM). On the ESP32-S2 such a block is scarce once
    // WiFi/lwIP/mbedTLS are running: the check task's own TLS handshake fragments
    // internal heap, which made a runtime xTaskCreate() fail with
    // errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY - first for the 16 KB update worker,
    // then (once that was made static) for the 8 KB check task itself
    // ("largest free internal block 10228 < 10240 needed"). Reserving the stack in
    // BSS at link time sidesteps the fragmented runtime heap entirely: the block
    // exists from boot, so xTaskCreateStatic() can never fail to allocate it.
    //
    // One buffer serves both tasks because they are mutually exclusive - an update
    // can only start after a check reaches CheckState::Done, and startBackgroundCheck
    // refuses to run while updateInProgress - so they never need the stack at the
    // same time. Sizing it for the larger worker (16 KB) and giving the check task a
    // smaller stack depth lets the check use just the first 8 KB.
    static inline StaticTask_t otaTaskTCB{};
    static inline StackType_t otaTaskStack[UPDATE_TASK_STACK]{};

    // The stack/TCB above are a single shared buffer, so the previous OTA task must
    // be fully terminated before the next one reuses them. A self-deleting task
    // (vTaskDelete(NULL)) is reclaimed asynchronously by the idle task, so its
    // handle can dangle - we can't tell when the buffers are free again. Instead
    // each task parks itself (vTaskSuspend, holding nothing) as its final act and
    // sets taskParked; the next start waits for that flag, then synchronously reaps
    // the parked task with vTaskDelete(handle) before reusing the buffers (see
    // reapParkedTask). updateInProgress is cleared inside performUpdate (before the
    // worker parks), so it can't gate reuse on its own - hence the separate flag.
    static inline TaskHandle_t otaTaskHandle = nullptr;
    static inline std::atomic<bool> taskParked{false};

    // Synchronously delete the previously-parked OTA task (if any) so its shared
    // static stack/TCB can be reused. Called on the AsyncTCP event task from both
    // startBackgroundCheck and startBackgroundUpdate, which are serialized there.
    static void reapParkedTask();
#endif

    // Background-check result, guarded by checkResultMutex().
    static inline CheckState checkState = CheckState::Idle;
    static inline FirmwareInfo checkResult{};
#ifdef ARDUINO
    static SemaphoreHandle_t checkResultMutex();
    static void otaCheckTask(void *arg);  // FreeRTOS worker for startBackgroundCheck
    static void otaWorkerTask(void *arg); // FreeRTOS worker for startBackgroundUpdate
#endif
};
