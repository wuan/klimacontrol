#include <memory>

#ifdef ARDUINO
#include <Wire.h>
#endif

#include "SensorDataLogger.h"
#include "Network.h"
#include "Config.h"
#include "WebServerManager.h"
#include "sensor/SHT4x.h"
#include "sensor/BME680.h"
#include "sensor/DeviceSensor.h"
#include "task/SensorMonitor.h"
#include "StatusLed.h"

TaskHandle_t networkTaskHandle = nullptr;

Config::ConfigManager config;
SensorController sensorController(config);
Task::SensorMonitor sensorMonitor(sensorController);
Network network(config, sensorController);

void setup() {
    delay(1000);
    Serial.println("Started");
    // config.reset();
    config.begin();

#ifdef ARDUINO
    Config::DeviceConfig deviceConfig = config.loadDeviceConfig();

    Serial.printf("Initializing I2C");

    // Initialize secondary I2C bus (STEMMA QT connector)
    Wire.begin(SDA1, SCL1);

    Config::SensorConfig sensorConfig = config.loadSensorConfig();

    if (sensorConfig.sht4x_enabled) {
        try {
            sensorController.addSensor(std::make_unique<Sensor::SHT4x>(sensorConfig.sht4x_address));
            Serial.printf("SHT4x sensor added at 0x%02X\n", sensorConfig.sht4x_address);
        } catch (...) {
            Serial.println("Error initializing SHT4x sensor");
        }
    }

    if (sensorConfig.bme680_enabled) {
        try {
            sensorController.addSensor(std::make_unique<Sensor::BME680>(sensorConfig.bme680_address));
            Serial.printf("BME680 sensor added at 0x%02X\n", sensorConfig.bme680_address);
        } catch (...) {
            Serial.println("Error initializing BME680 sensor");
        }
    }

    // Add device metrics sensor (RSSI, chip temp, free heap, uptime)
    sensorController.addSensor(std::make_unique<Sensor::DeviceSensor>());
    Serial.println("Device sensor added to controller");
#endif

    
    // Initialize sensor controller
    sensorController.begin();
    
    // Set network pointer for status LED control
    sensorController.setNetwork(&network);
    
    // Apply sensor configuration from the already loaded deviceConfig
    sensorController.setTargetTemperature(deviceConfig.target_temperature);
    sensorController.setControlEnabled(deviceConfig.temperature_control_enabled);

    // Start tasks on their designated cores
    // Sensor task: Core 1 (isolated from WiFi)
    // Network task: Core 0 (same as WiFi stack)

    try {
        sensorMonitor.startTask();
    } catch (const std::exception &e) {
        Serial.printf("Error starting sensor monitor task: %s\n", e.what());
    } catch (...) {
        Serial.println("Unknown error starting sensor monitor task");
    }

    try {
        network.startTask();
    } catch (const std::exception &e) {
        Serial.printf("Error starting network task: %s\n", e.what());
    } catch (...) {
        Serial.println("Unknown error starting network task");
    }
}

void loop() {
#ifdef ARDUINO
    config.checkRestart();
    vTaskDelay(250 / portTICK_PERIOD_MS);
#endif
}
