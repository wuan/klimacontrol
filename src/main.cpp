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
#include "task/SensorMonitor.h"

#ifdef ARDUINO
#include <esp_task_wdt.h>
#endif

static const char* TAG = "main";

TaskHandle_t networkTaskHandle = nullptr;

Config::ConfigManager config;
SensorController sensorController(config);
Task::SensorMonitor sensorMonitor(sensorController);
Network network(config, sensorController, sensorMonitor);

void setup() {
    delay(1000);
    ESP_LOGI(TAG, "Started");
    // config.reset();
    config.begin();

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
        strncpy(buf, sensorConfig.assignments, sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';

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

    // Apply sensor configuration from the already loaded deviceConfig
    sensorController.setTargetTemperature(deviceConfig.target_temperature);
    sensorController.setControlEnabled(deviceConfig.temperature_control_enabled);

#ifdef ARDUINO
    // esp_pm_config_esp32s2_t pm_config = {
    //     .max_freq_mhz = 80,
    //     .min_freq_mhz = 10,
    //     .light_sleep_enable = true
    // };
    // esp_pm_configure(&pm_config);
#endif

    ESP_LOGI(TAG, "Starting network task");
    try {
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

    // Configure task watchdog timer (30s timeout, panic on trigger)
    esp_task_wdt_init(30, true);
    ESP_LOGI(TAG, "Task watchdog configured (30s timeout)");
}

void loop() {
#ifdef ARDUINO
    config.checkRestart();
    vTaskDelay(2000 / portTICK_PERIOD_MS);
#endif
}
