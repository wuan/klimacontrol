#include <memory>

#ifdef ARDUINO
#include <Wire.h>
#endif

#include "SensorDataLogger.h"
#include "Network.h"
#include "Config.h"
#include "WebServerManager.h"
#include "sensor/SHT4x.h"
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
    // Load device configuration
    Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
    uint8_t sensor_address = deviceConfig.sensor_i2c_address;
    
    // Hardwired I2C pins for QT Py ESP32-S3
    const uint8_t i2c_sda_pin = 8;
    const uint8_t i2c_scl_pin = 9;
    
    Serial.printf("Initializing I2C on pins SDA=%u, SCL=%u\n", i2c_sda_pin, i2c_scl_pin);
    Serial.printf("Looking for sensor at address 0x%02X\n", sensor_address);

    // Initialize I2C with hardwired pins
    Wire.begin(i2c_sda_pin, i2c_scl_pin);
    
    // Initialize SHT4x sensor
    try {
        auto sht4x = std::make_unique<Sensor::SHT4x>(sensor_address);
        sensorController.addSensor(std::move(sht4x));
        Serial.println("SHT4x sensor added to controller");
    } catch (const std::exception &e) {
        Serial.printf("Error initializing SHT4x sensor: %s\n", e.what());
    } catch (...) {
        Serial.println("Unknown error initializing SHT4x sensor");
    }
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
