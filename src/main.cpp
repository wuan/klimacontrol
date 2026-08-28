#include <memory>
#include <cstring>
#include <cstdlib>

#ifdef ARDUINO
#include "esp_pm.h"
#include "Log.h"
#include <Wire.h>
#endif

#include "Network.h"
#include "Config.h"
#include "WebServerManager.h"
#include "sensor/SHT4x.h"
#include "sensor/BME680.h"
#include "sensor/SGP40.h"
#include "sensor/BMP3xx.h"
#include "sensor/SCD4x.h"
#include "sensor/TSL2591.h"
#include "sensor/PM25.h"
#include "sensor/VEML7700.h"
#include "sensor/DPS310.h"
#include "sensor/BH1750.h"
#include "SensorController.h"
#include "StatusLed.h"
#include "task/SensorMonitor.h"
#include "OTAUpdater.h"
#include "support/LocalTime.h"
#ifdef ARDUINO
#include "display/DisplayManager.h"
#endif

#ifdef ARDUINO
#include <esp_task_wdt.h>
#include <esp_idf_version.h>
#include <esp_system.h>
#include <esp_core_dump.h>
#include <esp_heap_caps.h>
#include "HardwareWatchdog.h"
#endif

static const char* TAG = "main";

#ifdef ARDUINO
// Map esp_reset_reason() to a short string for boot diagnostics. A
// ESP_RST_BROWNOUT here means the 3.3V rail sagged below the brownout
// threshold (typically the WiFi radio's TX inrush) and the chip reset
// instantly, which looks like a silent crash with no backtrace.
static const char *resetReasonStr(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXT";
        case ESP_RST_SW:        return "SW";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNKNOWN";
    }
}

// Print the stored core dump summary, if one is present in the `coredump`
// partition (see partitions.csv). The IDF panic handler writes the dump to
// flash automatically (CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH), but its live
// backtrace goes to UART0 — which nobody is watching on this board, since our
// console is USB CDC. Replaying the summary here is the only way the crashing
// PC/task from the *previous* boot ever reaches the serial monitor.
//
// The dump is deliberately NOT erased: the partition holds only the most
// recent crash and is overwritten by the next one, and leaving it in place
// lets espcoredump.py pull the full ELF with symbolicated frames:
//
//   python $HOME/.platformio/packages/framework-espidf/components/espcoredump/
//     espcoredump.py -p <port> info_corefile
//     -t elf .pio/build/adafruit_qtpy_esp32s2/firmware.elf
static void logCoreDumpSummary() {
    esp_err_t check = esp_core_dump_image_check();
    if (check == ESP_ERR_NOT_FOUND) {
        return; // no crash recorded — the common case
    }
    if (check != ESP_OK) {
        ESP_LOGW(TAG, "Core dump present but unreadable (err 0x%x)", check);
        return;
    }

#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
    esp_core_dump_summary_t summary;
    if (esp_core_dump_get_summary(&summary) != ESP_OK) {
        ESP_LOGW(TAG, "Core dump present but summary could not be parsed");
        return;
    }

    ESP_LOGE(TAG, "Core dump from previous crash: task='%s' PC=0x%08x cause=%u vaddr=0x%08x",
             summary.exc_task, summary.exc_pc,
             summary.ex_info.exc_cause, summary.ex_info.exc_vaddr);

    // One line per frame keeps each Serial.printf small; the USB CDC TX
    // buffer is only 64 bytes and a long line would just stall the boot.
    for (uint32_t i = 0; i < summary.exc_bt_info.depth && i < 16; i++) {
        ESP_LOGE(TAG, "  bt[%u] 0x%08x", i, summary.exc_bt_info.bt[i]);
    }
    if (summary.exc_bt_info.corrupted) {
        ESP_LOGW(TAG, "  (backtrace marked corrupted)");
    }
#else
    size_t addr = 0, size = 0;
    if (esp_core_dump_image_get(&addr, &size) == ESP_OK) {
        ESP_LOGE(TAG, "Core dump from previous crash: %u bytes at 0x%06x", size, addr);
    }
#endif
}
#endif

// Maximum number of sensors the I2C scan loop is willing to add. Each branch
// in the assignment-parser below (one per Sensor::* type) maps to one slot,
// so the value must be kept in sync with that list. The native test
// `test_memory/test_singleton_lifetimes` asserts the match.
static constexpr size_t MAX_KNOWN_SENSORS = 10;

TaskHandle_t networkTaskHandle = nullptr;

Config::ConfigManager config;
// StatusLed is a top-level object so SensorController's failure path can drive
// it even before Network is constructed.
StatusLed statusLed;
SensorController sensorController(config, &statusLed);
Task::SensorMonitor sensorMonitor(sensorController);
// Network is constructed first with a null webServer pointer; the
// WebServerManager is constructed right after (it needs a Network& reference)
// and wired in via setWebServer. This breaks the circular reference while
// keeping both objects as long-lived singletons — see spec `memory-management`
// → "Long-lived singletons are constructed once".
Network network(config, sensorController, sensorMonitor, statusLed, nullptr);
WebServerManager webServer(config, network, sensorController, sensorMonitor);
#ifdef ARDUINO
// E-paper display. Constructed unconditionally (its 625 B page buffer is in BSS
// either way), but only initialized when enabled in configuration — see
// setupDisplay() below.
Display::DisplayManager displayManager(sensorController);
#endif

#ifdef ARDUINO
// Bring up the e-paper display according to the persisted configuration.
//
// Three cases, driven by the `enabled` flag and the `clear_pending` one-shot:
//
//   enabled                  -> init, splash, wire into the Network task.
//   disabled + clear_pending -> init, blank the panel, drop the flag. e-paper
//                               retains its image with no power, so a display
//                               that was just turned off has to be actively
//                               cleared or it shows a stale reading forever.
//   disabled                 -> do nothing at all; the SPI and control pins are
//                               never claimed.
//
// The flag is persisted rather than blanked inline in the HTTP handler so that
// losing power between the save and the clear still leaves the blanking queued
// for the next boot.
static void setupDisplay(const Config::DeviceConfig &deviceConfig) {
    Config::DisplayConfig displayConfig = config.loadDisplayConfig();

    if (!displayConfig.enabled) {
        if (displayConfig.clear_pending) {
            ESP_LOGI(TAG, "Display disabled with a pending clear - blanking panel");
            displayManager.clearAndPark(displayConfig);
            displayConfig.clear_pending = false;
            config.saveDisplayConfig(displayConfig);
        }
        return;
    }

    // Prefer the user-assigned name; fall back to the derived hostname.
    char name[32];
    if (deviceConfig.device_name[0] != '\0') {
        strlcpy(name, deviceConfig.device_name, sizeof(name));
    } else {
        snprintf(name, sizeof(name), "%s%s", Constants::HOSTNAME_PREFIX, config.getDeviceId().c_str());
    }

    if (displayManager.begin(displayConfig, name)) {
        displayManager.setNetwork(&network);
        network.setDisplay(&displayManager);
    } else {
        ESP_LOGE(TAG, "Display enabled in config but failed to initialize");
    }

    // A stale clear_pending alongside an enabled display just means the user
    // re-enabled it before the blanking ran; drop it.
    if (displayConfig.clear_pending) {
        displayConfig.clear_pending = false;
        config.saveDisplayConfig(displayConfig);
    }
}
#endif


void setup() {
    delay(1000);
    // Serial.setDebugOutput(true);
    // config.reset();
    ESP_LOGI(TAG, "Started");
#ifdef ARDUINO
    esp_reset_reason_t resetReason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset reason: %s (%d)", resetReasonStr(resetReason), resetReason);
    ESP_LOGI(TAG, "Boot heap: internal free=%u largest=%u, total free=%u",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    logCoreDumpSummary();
#endif
    config.begin();
    // Wire the pre-constructed WebServerManager into the network task. Both
    // objects exist at file scope (Network is constructed first with a null
    // pointer to break the circular reference, WebServerManager is constructed
    // next and given the Network& reference it needs); this call completes
    // the long-lived wiring before the network task starts.
    network.setWebServer(&webServer);

#ifdef ARDUINO
    Config::DeviceConfig deviceConfig = config.loadDeviceConfig();

    ESP_LOGI(TAG, "Initializing I2C");

    // Initialize secondary I2C bus (STEMMA QT connector)
    Wire1.begin(SDA1, SCL1);

    Config::SensorConfig sensorConfig = config.loadSensorConfig();

    // Parse assignment string and instantiate sensors
    // Format: "44=SHT4x,77=BME680,59=SGP40"
    {
        char buf[128];
        strlcpy(buf, sensorConfig.assignments, sizeof(buf));

        char *token = strtok(buf, ",");
        while (token) {
            char *eq = strchr(token, '=');
            if (eq) {
                *eq = '\0';
                uint8_t addr = (uint8_t) strtoul(token, nullptr, 10);
                const char *name = eq + 1;

                try {
                    if (strcmp(name, Sensor::SHT4x::type()) == 0) {
                        sensorController.addSensor(std::make_unique<Sensor::SHT4x>(addr));
                    } else if (strcmp(name, Sensor::BME680::type()) == 0) {
                        sensorController.addSensor(std::make_unique<Sensor::BME680>(addr));
                    } else if (strcmp(name, Sensor::SGP40::type()) == 0) {
                        sensorController.addSensor(std::make_unique<Sensor::SGP40>(addr));
                    } else if (strcmp(name, Sensor::BMP3xx::type()) == 0) {
                        sensorController.addSensor(std::make_unique<Sensor::BMP3xx>(addr));
                    } else if (strcmp(name, Sensor::SCD4x::type()) == 0) {
                        sensorController.addSensor(std::make_unique<Sensor::SCD4x>(addr));
                    } else if (strcmp(name, Sensor::TSL2591::type()) == 0) {
                        sensorController.addSensor(std::make_unique<Sensor::TSL2591>(addr));
                    } else if (strcmp(name, Sensor::PM25::type()) == 0) {
                        sensorController.addSensor(std::make_unique<Sensor::PM25>(addr));
                    } else if (strcmp(name, Sensor::VEML7700::type()) == 0) {
                        sensorController.addSensor(std::make_unique<Sensor::VEML7700>(addr));
                    } else if (strcmp(name, Sensor::DPS310::type()) == 0) {
                        sensorController.addSensor(std::make_unique<Sensor::DPS310>(addr));
                    } else if (strcmp(name, Sensor::BH1750Sensor::type()) == 0) {
                        sensorController.addSensor(std::make_unique<Sensor::BH1750Sensor>(addr));
                    } else {
                        ESP_LOGW(TAG, "Unknown sensor type: %s", name);
                        token = strtok(nullptr, ",");
                        continue;
                    }
                    ESP_LOGI(TAG, "Sensor %s added at 0x%02X", name, addr);
                } catch (...) {
                    ESP_LOGE(TAG, "Error initializing %s sensor at 0x%02X", name, addr);
                }
            }
            token = strtok(nullptr, ",");
        }
    }

#endif

    // Initialize sensor controller
    sensorController.begin();
    // Reserve the sensor + measurement vector capacity so the I2C scan loop's
    // addSensor() calls do not reallocate. Must happen before the scan loop —
    // see spec `memory-management` → "Vector capacities are reserved at boot".
    sensorController.reserveSensorSlots(MAX_KNOWN_SENSORS);

    // Apply sensor configuration from the already loaded deviceConfig
    sensorController.setTargetTemperature(deviceConfig.target_temperature);
    sensorController.setControlEnabled(deviceConfig.temperature_control_enabled);

#ifdef ARDUINO
    // Power management API conflicts with WIFI_PS_NONE configuration
    // DFS doesn't work in Arduino+WiFi_PS_NONE. Focus on reducing Serial logging instead.
    // See: POWER_OPTIMIZATION.md - "Why it's not working"
#endif

    // Configure the task watchdog timer (30s timeout, panic on trigger) BEFORE
    // any task that subscribes to it is created. Both the network and sensor
    // tasks call esp_task_wdt_add(NULL) as their first act; they run at the same
    // priority as this setup task, so if init happened afterwards the add() would
    // race it, return ESP_ERR_INVALID_STATE, and leave the task permanently
    // unsubscribed — every later esp_task_wdt_reset() in it a silent no-op, and
    // the 30s stall protection quietly gone. The RTC watchdog would not cover it
    // either, since loop() keeps feeding that independently.
    //
    // The init API changed between IDF 4.x (this toolchain: arduino-esp32 2.0.x)
    // and IDF 5.x, where it takes a config struct and the core may have already
    // initialized the TWDT (hence the reconfigure fallback). Guard by version so
    // a toolchain bump doesn't silently fail to compile.
#if ESP_IDF_VERSION_MAJOR >= 5
    const esp_task_wdt_config_t wdtConfig = {
        .timeout_ms = 30000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_err_t wdtInit = esp_task_wdt_init(&wdtConfig);
    if (wdtInit == ESP_ERR_INVALID_STATE) {
        wdtInit = esp_task_wdt_reconfigure(&wdtConfig);
    }
#else
    esp_err_t wdtInit = esp_task_wdt_init(30, true);
#endif
    if (wdtInit == ESP_OK) {
        ESP_LOGI(TAG, "Task watchdog configured (30s timeout)");
    } else {
        ESP_LOGE(TAG, "Task watchdog init FAILED (err 0x%x) - tasks will run unguarded", wdtInit);
    }

#ifdef ARDUINO
    // Create the parked OTA worker tasks before the web server can accept
    // requests, so /api/ota/* always has a worker to notify. Their stacks are
    // reserved in BSS, so this cannot fail on a fragmented heap.
    OTAUpdater::begin();
#endif

#ifdef ARDUINO
    // Apply the configured timezone before anything can format a local time.
    // The POSIX TZ string carries the daylight-saving rules, so transitions are
    // handled by libc with no further configuration — see support/LocalTime.h.
    Support::applyTimezone(deviceConfig.timezone);
    ESP_LOGI(TAG, "Timezone: %s", deviceConfig.timezone);

    // Before network.begin() so the splash is on the panel while WiFi
    // association is still in progress.
    setupDisplay(deviceConfig);
#endif

    ESP_LOGI(TAG, "Starting network task");
    try {
        network.begin();
        network.startTask();
    } catch (const std::exception &e) {
        ESP_LOGE(TAG, "Error starting network task: %s", e.what());
    } catch (...) {
        ESP_LOGE(TAG, "Unknown error starting network task");
    }

    ESP_LOGI(TAG, "Starting sensor task");
    try {
        sensorMonitor.startTask();
    } catch (const std::exception &e) {
        ESP_LOGE(TAG, "Error starting sensor monitor task: %s", e.what());
    } catch (...) {
        ESP_LOGE(TAG, "Unknown error starting sensor monitor task");
    }

    // Independent hardware (RTC) watchdog backstop. Longer than the 30s TWDT so
    // the TWDT fires first on an ordinary task stall; this only triggers on a
    // total hang the TWDT can't catch. Fed from loop() below.
    static constexpr uint32_t HW_WDT_TIMEOUT_MS = 60000;
    HardwareWatchdog::begin(HW_WDT_TIMEOUT_MS);

#ifdef ARDUINO
    // Cancel the pending rollback now that init has completed and both tasks
    // are running. Deliberately the last thing in setup(): an image that
    // crashes during init should still be rolled back by the bootloader, which
    // only happens while it remains unconfirmed. Once we get here the image has
    // proven it can boot, so confirm it — otherwise the next restart from any
    // source (low-heap guard, WiFi force-restart, watchdog, power cycle) would
    // silently revert the device to the previous firmware.
    OTAUpdater::confirmRunningImage();
#endif
}

void loop() {
#ifdef ARDUINO
    HardwareWatchdog::feed();
    config.checkRestart();
    vTaskDelay(2000 / portTICK_PERIOD_MS);
#endif
}
