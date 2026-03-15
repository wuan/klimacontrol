#include <memory>
#include <cstring>
#include <cstdlib>

#ifdef ARDUINO
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
#include "sensor/DeviceSensor.h"
#include "SensorController.h"
#include "task/SensorMonitor.h"
#include "StatusLed.h"

#ifdef ARDUINO
#include <esp_task_wdt.h>
#include <esp_pm.h>
#endif

TaskHandle_t networkTaskHandle = nullptr;

Config::ConfigManager config;
SensorController sensorController(config);
Task::SensorMonitor sensorMonitor(sensorController);
Network network(config, sensorController, sensorMonitor);

void setup() {
    delay(1000);
    Serial.println("Started");
    // config.reset();
    config.begin();

#ifdef ARDUINO
    Config::DeviceConfig deviceConfig = config.loadDeviceConfig();

    Serial.printf("Initializing I2C");

    // Initialize secondary I2C bus (STEMMA QT connector)
    Wire1.begin(SDA1, SCL1);

    Config::SensorConfig sensorConfig = config.loadSensorConfig();

    // Parse assignment string and instantiate sensors
    // Format: "44=SHT4x,77=BME680,59=SGP40"
    {
        char buf[128];
        strncpy(buf, sensorConfig.assignments, sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';

        char* token = strtok(buf, ",");
        while (token) {
            char* eq = strchr(token, '=');
            if (eq) {
                *eq = '\0';
                uint8_t addr = (uint8_t)strtoul(token, nullptr, 10);
                const char* name = eq + 1;

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
                        Serial.printf("Unknown sensor type: %s\r\n", name);
                        token = strtok(nullptr, ",");
                        continue;
                    }
                    Serial.printf("Sensor %s added at 0x%02X\r\n", name, addr);
                } catch (...) {
                    Serial.printf("Error initializing %s sensor at 0x%02X\r\n", name, addr);
                }
            }
            token = strtok(nullptr, ",");
        }
    }

    // Add device metrics sensor (RSSI, chip temp, free heap, uptime)
    sensorController.addSensor(std::make_unique<Sensor::DeviceSensor>());
    Serial.println("Device sensor added to controller");
#endif

    // Initialize sensor controller
    sensorController.begin();

#ifdef ARDUINO
    // Apply sensor reading interval from config (persisted in NVS)
    sensorMonitor.setReadingInterval(sensorConfig.sensor_interval_ms);
    Serial.printf("Sensor reading interval: %lu ms\r\n", (unsigned long)sensorConfig.sensor_interval_ms);

    // Configure ESP32 power management: enable automatic light sleep when all
    // tasks are idle (blocked in vTaskDelay).  The CPU frequency scales between
    // min_freq_mhz and max_freq_mhz; when all tasks sleep the modem and CPU
    // enter light sleep, dramatically reducing idle current.
    // esp_pm_config_t is available from ESP-IDF v5; older v4 SDK (used by the
    // Arduino ESP32 framework) exposes only chip-specific structs.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_pm_config_t pm_config = {
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
    esp_pm_config_esp32s2_t pm_config = {
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    esp_pm_config_esp32s3_t pm_config = {
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    esp_pm_config_esp32c3_t pm_config = {
#else
    esp_pm_config_esp32_t pm_config = {
#endif
        .max_freq_mhz = 80,
        .min_freq_mhz = 40,
        .light_sleep_enable = true
    };
    if (esp_pm_configure(&pm_config) == ESP_OK) {
        Serial.println("Power management: light sleep enabled (40-80 MHz)");
    } else {
        Serial.println("Warning: Power management configuration failed");
    }
#endif

    // Apply sensor configuration from the already loaded deviceConfig
    sensorController.setTargetTemperature(deviceConfig.target_temperature);
    sensorController.setControlEnabled(deviceConfig.temperature_control_enabled);

    try {
        sensorMonitor.startTask();
    } catch (const std::exception &e) {
        Serial.printf("Error starting sensor monitor task: %s\r\n", e.what());
    } catch (...) {
        Serial.println("Unknown error starting sensor monitor task");
    }

    try {
        network.startTask();
    } catch (const std::exception &e) {
        Serial.printf("Error starting network task: %s\r\n", e.what());
    } catch (...) {
        Serial.println("Unknown error starting network task");
    }

    // Configure task watchdog timer (30s timeout, panic on trigger)
    esp_task_wdt_init(30, true);
    Serial.println("Task watchdog configured (30s timeout)");
}

void loop() {
#ifdef ARDUINO
    config.checkRestart();
    vTaskDelay(2000 / portTICK_PERIOD_MS);
#endif
}
