//
// OTA Firmware Update Manager for ESP32-S2
//
// Handles GitHub release checking, firmware download, and safe OTA updates
// with rollback support. Uses ESP-IDF esp_http_client for reliable HTTPS.
//

#pragma once

#include <Arduino.h>
#include <atomic>
#include <functional>

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

    // State of the most recent background update (see startBackgroundUpdate).
    // The download takes minutes, so the HTTP handler returns immediately and
    // clients poll this to learn the progress and the final outcome — without
    // it a failed update is invisible to the UI.
    enum class UpdateState : uint8_t {
        Idle,        // no update has been attempted
        Downloading, // download/flash in progress; percent/bytes are valid
        Success,     // flash completed; a restart has been scheduled
        Failed       // last attempt failed; errorMessage is set
    };

    /**
     * Create the two OTA worker tasks. Must be called from setup(), before the
     * web server can accept requests.
     *
     * The tasks are created once, here, and then park on a task notification
     * for the lifetime of the device (see otaCheckTask / otaWorkerTask).
     * Creating them at boot rather than per request solves two problems at
     * once: a FreeRTOS stack needs one contiguous block of *internal* SRAM,
     * which is unreliable to obtain once WiFi/mbedTLS have fragmented the heap;
     * and repeatedly recreating a task on the same static stack/TCB races the
     * idle task's reclamation of the previous incarnation (vTaskDelete() only
     * queues a task for cleanup, so reinitializing its TCB before idle has run
     * corrupts the scheduler's lists).
     */
    static void begin();

    /**
     * Mark the running image valid, cancelling the pending rollback.
     *
     * With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y (the default in the Arduino
     * precompiled SDK) a freshly flashed image boots in
     * ESP_OTA_IMG_PENDING_VERIFY state, and the bootloader reverts to the
     * previous partition on the *next* reset unless the running app marks
     * itself valid. This device restarts itself on several paths (low-heap
     * guard, WiFi force-restart, watchdog panic, plain power cycle), so without
     * this call every successful update silently undid itself on the first such
     * restart. Confirming also protects the rollback target: a second OTA while
     * still PENDING_VERIFY would overwrite the partition holding the last
     * known-good image.
     *
     * Call this at the *end* of setup(), once the network and sensor tasks are
     * running — deliberately not at the top. A new image that crashes during
     * init should still be rolled back, and that only works while it remains
     * unconfirmed; the automatic restart paths that would cause an unwanted
     * rollback all need minutes to trigger, so confirming a few seconds into
     * boot keeps both properties.
     *
     * No-op when the running image is already confirmed (the common case).
     */
    static void confirmRunningImage();

    static bool checkForUpdate(const char *owner, const char *repo, FirmwareInfo &info);

    /**
     * Run checkForUpdate() on the background OTA task and return immediately.
     *
     * checkForUpdate() blocks for the GitHub TLS round-trip (seconds, up to
     * TIMEOUT_MS); running it inline in an ESPAsyncWebServer callback would stall
     * the single AsyncTCP event task. Callers start a check, then poll
     * getCheckResult() for the outcome.
     *
     * @return true if a check was started; false if a check or update is already
     *         in progress, or begin() has not run.
     */
    static bool startBackgroundCheck(const char *owner, const char *repo);

    /**
     * Read the background check state and (if Done/Failed) a copy of the result.
     * Thread-safe snapshot of both values under one lock.
     */
    static CheckState getCheckResult(FirmwareInfo &infoOut);

    /**
     * Thread-safe snapshot of the background update's progress and outcome.
     * `percentOut`/`bytesOut` are meaningful while Downloading and after
     * Success; `errorOut` is set when Failed.
     */
    static UpdateState getUpdateProgress(int &percentOut, size_t &bytesOut, String &errorOut);

    /**
     * True if the release found by the last successful check is strictly newer
     * than the running firmware. Drives both the API's `update_available` flag
     * and the decision to flash, so a downgrade is never offered or installed.
     */
    static bool isUpdateAvailable(const FirmwareInfo &info);

    /**
     * Start an OTA update for the firmware identified by the most recent
     * successful background check (see startBackgroundCheck / getCheckResult).
     *
     * The download URL and size are taken from the device's own check result —
     * a GitHub release for the compiled-in owner/repo — and never from the
     * caller. This is deliberate: clients must not be able to point the device
     * at an arbitrary binary. A check (CheckState::Done with a valid asset that
     * is strictly newer than the running version) must have completed first.
     *
     * The multi-minute download runs on the background OTA task so the HTTP
     * handler can respond right away; on success the worker schedules a restart
     * via the supplied ConfigManager.
     *
     * @return true if the worker was started; false if no verified newer update
     *         is available, or a check/update is already in progress.
     */
    static bool startBackgroundUpdateFromLatestCheck(Config::ConfigManager &config);

    static bool confirmBoot();
    static bool hasUnconfirmedUpdate();
    static bool getRunningPartitionInfo(String &label, uint32_t &address);
    static void getMemoryInfo(uint32_t &freeHeap, uint32_t &minFreeHeap);
    static bool hasEnoughMemory();

    // True while a check or an update is running. The network task's low-heap
    // guard consults this to avoid rebooting the device mid-OTA, when TLS
    // buffers legitimately consume most of the free heap.
    static bool isUpdateInProgress() { return activity.load() != Activity::None; }

private:
    // What the OTA subsystem is currently doing. A single atomic state variable
    // replaces the earlier pair of (checkState, updateInProgress) flags, which
    // lived in two different synchronization domains: startBackgroundCheck()
    // read the update flag under a mutex while startBackgroundUpdate() claimed
    // it with a compare-exchange, so a check and an update could both pass
    // their guard and then race — with the check's exit clearing the update's
    // claim. Every transition out of None now goes through one
    // compare_exchange_strong, so the two activities are strictly exclusive.
    enum class Activity : uint8_t { None, Checking, Updating };
    static inline std::atomic<Activity> activity{Activity::None};

    // Try to move None -> want. Returns false if another activity holds the slot.
    static bool claimActivity(Activity want);
    static void releaseActivity() { activity.store(Activity::None); }

    // Spawn the OTA worker for an already-validated download URL/size.
    // Internal only: the URL must originate from a trusted source (the device's
    // own GitHub check), never directly from a client request.
    static bool startBackgroundUpdate(
        const FirmwareInfo &info,
        Config::ConfigManager &config
    );

    // Download and flash. Private because it accepts an arbitrary URL: only
    // otaWorkerTask may call it, with a URL that came from the device's own
    // GitHub check.
    static bool performUpdate(
        const String &downloadUrl,
        size_t expectedSize,
        const std::function<void(int, size_t)> &onProgress = nullptr
    );

    static constexpr int TIMEOUT_MS = 30000;
    static constexpr int CHUNK_SIZE = 4096;
    // esp_http_client response (RX) buffer. Must be large enough to hold a
    // whole HTTP response header block in a SINGLE esp_tls_conn_read(): GitHub's
    // github.com 302 release-download redirect carries a ~3.6 KB
    // Content-Security-Policy header (total header block ~5 KB) with a 0-byte
    // body, sent as one small TLS record. mbedTLS decrypts the full record into
    // its internal buffer on the first read; if our buffer is smaller than the
    // record, the leftover plaintext stays buffered inside mbedTLS and is
    // invisible to the socket poll() that esp_http_client's next read performs,
    // so esp_http_client_fetch_headers() never reaches on_headers_complete and
    // get_status_code() keeps its -1 init value ("No HTTP response"). 8 KB holds
    // the current ~5 KB block with headroom for CSP growth.
    //
    // Applied to the check request too, not just the download: api.github.com
    // happens to stream a body across many TLS records today (so the 512-byte
    // default survives), but that is a property of GitHub's current framing,
    // not something we should depend on.
    static constexpr int HTTP_RX_BUFFER = 8192;

    // OTA memory floor, measured in *internal* SRAM only.
    //
    // esp_get_free_heap_size() is useless as a gate on this board: with
    // CONFIG_SPIRAM_USE_MALLOC=y and CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=0 it
    // sums internal SRAM and the 2 MB of PSRAM, so a 64 KB check passed
    // unconditionally while the resources OTA actually needs — the mbedTLS
    // working set, lwIP/socket structures, DMA buffers — are internal-only.
    // That is exactly the "largest free internal block 10228 < 10240" class of
    // failure this file has been fighting, so gate on both the internal total
    // and the largest contiguous internal block. hasEnoughMemory() logs both
    // numbers, so these can be re-tuned from a real device.
    static constexpr uint32_t MIN_FREE_INTERNAL = 32768;
    static constexpr uint32_t MIN_LARGEST_INTERNAL_BLOCK = 8192;

    // Worker stack must hold the 4 KB chunk buffer plus the mbedTLS handshake
    // working set. Measured actual usage is ~9.3 KB; 12 KB gives ~32% headroom
    // over the high-water mark. otaWorkerTask logs its stack high-water mark so
    // this can be re-tuned.
    static constexpr uint32_t UPDATE_TASK_STACK = 12288;
    // Check task needs the TLS handshake working set + JSON parse buffers.
    // The large mbedTLS buffers are offloaded to PSRAM (esp_mbedtls_mem_calloc),
    // so the stack itself stays modest. Measured actual usage is ~5.5 KB;
    // 7 KB gives ~27% headroom over the high-water mark.
    static constexpr uint32_t CHECK_TASK_STACK = 7168;

#ifdef ARDUINO
    // Statically-reserved stacks + TCBs for the two OTA tasks. A FreeRTOS task
    // stack must come from one contiguous block of *internal* SRAM (never
    // PSRAM), and on the ESP32-S2 such a block is scarce once WiFi/lwIP/mbedTLS
    // have fragmented the heap (a runtime xTaskCreate() failed with
    // errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY — "largest free internal block
    // 10228 < 10240 needed"). Reserving the stacks in BSS at link time
    // sidesteps the fragmented runtime heap entirely: the blocks exist from
    // boot, so xTaskCreateStatic() can never fail to allocate them.
    //
    // Both tasks are created exactly once, by begin(), and then live forever
    // parked on a task notification. Nothing ever calls vTaskDelete() on them,
    // which is what makes reusing these buffers safe: vTaskDelete() merely
    // queues a task for cleanup by the idle task, so recreating a task on the
    // same StaticTask_t before idle has run would insert a TCB into the ready
    // list while it is still linked into xTasksWaitingTermination — scheduler
    // list corruption. Never deleting them removes that window by construction.
    //
    // alignas(16) documents the Xtensa stack alignment requirement; FreeRTOS
    // also rounds the top-of-stack down to a 16-byte boundary, but being
    // explicit costs nothing and avoids wasting up to 15 bytes.
    alignas(16) static inline StaticTask_t otaCheckTCB{};
    alignas(16) static inline StackType_t otaCheckStack[CHECK_TASK_STACK]{};
    alignas(16) static inline StaticTask_t otaUpdateTCB{};
    alignas(16) static inline StackType_t otaUpdateStack[UPDATE_TASK_STACK]{};

    static inline TaskHandle_t checkTaskHandle = nullptr;
    static inline TaskHandle_t updateTaskHandle = nullptr;
#endif

    // Job payloads for the parked worker tasks. Writing these is safe without a
    // lock because the writer has already claimed `activity` via
    // claimActivity(), which excludes every other writer until the worker
    // releases the slot; the xTaskNotifyGive() that follows the write acts as
    // the release barrier.
    //
    // owner/repo are fixed char buffers rather than String to avoid heap churn
    // in the very subsystem whose enemy is heap fragmentation.
    static inline char pendingOwner[64]{};
    static inline char pendingRepo[64]{};
    static inline FirmwareInfo pendingUpdate{};
    static inline Config::ConfigManager *pendingConfig = nullptr;

    // Background-check result, guarded by stateMutex().
    static inline CheckState checkState = CheckState::Idle;
    static inline FirmwareInfo checkResult{};

    // Background-update progress/outcome, guarded by stateMutex().
    static inline UpdateState updateState = UpdateState::Idle;
    static inline int updatePercent = 0;
    static inline size_t updateBytes = 0;
    static inline String updateError{};

#ifdef ARDUINO
    static SemaphoreHandle_t stateMutex();
    static void setUpdateState(UpdateState state, int percent, size_t bytes, const char *error);
    static void otaCheckTask(void *arg);  // parked worker for startBackgroundCheck
    static void otaWorkerTask(void *arg); // parked worker for startBackgroundUpdate
#endif
};
