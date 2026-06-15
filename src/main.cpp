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

#ifdef ARDUINO
#include <esp_task_wdt.h>
#include <esp_idf_version.h>
#include "HardwareWatchdog.h"
#endif

static const char* TAG = "main";

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

void setup() {
    delay(1000);
    // Serial.setDebugOutput(true);
    // config.reset();
    ESP_LOGI(TAG, "Started");
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

    // Configure task watchdog timer (30s timeout, panic on trigger).
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
    if (esp_task_wdt_init(&wdtConfig) == ESP_ERR_INVALID_STATE) {
        esp_task_wdt_reconfigure(&wdtConfig);
    }
#else
    esp_task_wdt_init(30, true);
#endif
    ESP_LOGI(TAG, "Task watchdog configured (30s timeout)");

    // Independent hardware (RTC) watchdog backstop. Longer than the 30s TWDT so
    // the TWDT fires first on an ordinary task stall; this only triggers on a
    // total hang the TWDT can't catch. Fed from loop() below.
    static constexpr uint32_t HW_WDT_TIMEOUT_MS = 60000;
    HardwareWatchdog::begin(HW_WDT_TIMEOUT_MS);
}

void loop() {
#ifdef ARDUINO
    HardwareWatchdog::feed();
    config.checkRestart();
    vTaskDelay(2000 / portTICK_PERIOD_MS);
#endif
}
