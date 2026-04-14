#ifndef KLIMACONTROL_CONFIG_H
#define KLIMACONTROL_CONFIG_H

#ifdef ARDUINO
#include <Preferences.h>
#include <Arduino.h>
#else
#include <cstdint>
#include <cstring>
#include <string>
using String = std::string;
#endif

#include "Constants.h"

namespace Config {
    /**
     * WiFi configuration structure
     */
    struct WiFiConfig {
        char ssid[64] = "";
        char password[64] = "";
        bool configured = false;
        uint8_t connection_failures = 0; // Track consecutive connection failures

        WiFiConfig() = default;
    };

    /**
     * Device configuration structure
     */
    struct DeviceConfig {
        char device_id[16] = ""; // e.g., "AABBCC"
        char device_name[32] = ""; // Custom device name

        // Sensor and temperature control configuration
        uint8_t sensor_i2c_address = 0x44; // Default I2C address for sensors
        float target_temperature = 22.0f; // Target temperature for control
        bool temperature_control_enabled = false; // Temperature control enabled
        float elevation = 0.0f; // Meters above sea level, for sea-level pressure calculation

        DeviceConfig() = default;
    };

    /**
     * MQTT configuration structure
     */
    struct MqttConfig {
        char host[64] = "";
        uint16_t port = 1883;
        char username[64] = "";
        char password[64] = "";
        char prefix[64] = "sensors"; // topic prefix, e.g. "sensors/bedroom"
        uint16_t interval = 15; // publish interval in seconds
        bool enabled = false;

        MqttConfig() = default;
    };

    /**
     * Sensor configuration structure
     * Stores sensor assignments as a compact string like "44=SHT4x,77=BME680,59=SGP40"
     */
    struct SensorConfig {
        char assignments[128] = ""; // e.g. "44=SHT4x,77=BME680,59=SGP40"
        SensorConfig() = default;
    };

    /**
     * Energy configuration structure
     */
    struct EnergyConfig {
        uint8_t wifi_power = Constants::DEFAULT_WIFI_POWER; // wifi_power_t raw value, default Constants::DEFAULT_WIFI_POWER (13 dBm)
        EnergyConfig() = default;
    };

    /**
     * Syslog configuration structure
     */
    struct SyslogConfig {
        char host[64] = "";
        uint16_t port = 514;
        bool enabled = false;

        SyslogConfig() = default;
    };

    // Validation functions — pure C++, testable on native builds
    void validateDeviceConfig(DeviceConfig &config);
    void validateMqttConfig(MqttConfig &config);
    void validateEnergyConfig(EnergyConfig &config);

    /**
     * Configuration Manager
     * Handles persistent storage using ESP32 Preferences (NVS)
     */
    class ConfigManager {
    private:
#ifdef ARDUINO
        Preferences prefs;
#endif
        static constexpr const char *NAMESPACE = "klima";
        static constexpr const char *TARGET_TEMPERATURE = "target_temperature";
        static constexpr const char *TEMPERATURE_CONTROL_ENABLED = "temperature_control_enabled";
        static constexpr const char *ELEVATION = "elevation";
        static constexpr const char *ENERGY_WIFI_PW = "energy_wifi_pw";


        bool restartRequested = false;
#ifdef ARDUINO
        uint32_t restartAt = 0;
#endif

    public:
        ConfigManager();

        /**
         * Initialize the configuration manager
         * Must be called before using other methods
         */
        void begin();

        /**
         * Check if a deferred restart is pending
         * @return true if restart is pending
         */
        bool isRestartPending() const { return restartRequested; }

        /**
         * Request a deferred restart
         * @param delayMs Delay in milliseconds before restarting
         */
        void requestRestart(uint32_t delayMs = 1000);

        /**
         * Check and perform restart if scheduled and time has passed
         * Called from main loop
         */
        void checkRestart();

        /**
         * Check if WiFi has been configured
         * @return true if WiFi configuration exists
         */
        bool isConfigured();

        /**
         * Mark as unconfigured
         */
        void markUnconfigured();

        /**
         * Load WiFi configuration from NVS
         * @return WiFiConfig structure
         */
        WiFiConfig loadWiFiConfig();

        /**
         * Save WiFi configuration to NVS
         * @param config WiFi configuration to save
         */
        void saveWiFiConfig(const WiFiConfig &config);

        /**
         * Load device configuration from NVS
         * @return DeviceConfig structure with defaults if not found
         */
        DeviceConfig loadDeviceConfig();

        /**
         * Save device configuration to NVS (partial updates)
         * @param config Device configuration to save
         */
        void saveDeviceConfig(const DeviceConfig &config);
        
        /**
         * Update individual device configuration fields
         */
        void updateDeviceName(const char* device_name);
        void updateTargetTemperature(float temperature);
        void updateTemperatureControlEnabled(bool enabled);
        void updateElevation(float elevation);

        /**
         * Factory reset - clear all stored configuration
         */
        void reset();

        /**
         * Get unique device ID based on MAC address
         * @return Device ID string (e.g., "AABBCC")
         */
        String getDeviceId();

        /**
         * Increment WiFi connection failure counter
         * @return New failure count
         */
        uint8_t incrementConnectionFailures();

        /**
         * Reset WiFi connection failure counter
         */
        void resetConnectionFailures();

        /**
         * Get WiFi connection failure count
         * @return Number of consecutive failures
         */
        uint8_t getConnectionFailures();

        /**
         * Load sensor configuration from NVS
         * @return SensorConfig structure
         */
        SensorConfig loadSensorConfig();

        /**
         * Save sensor configuration to NVS
         * @param config Sensor configuration to save
         */
        void saveSensorConfig(const SensorConfig &config);

        /**
         * Load MQTT configuration from NVS
         * @return MqttConfig structure
         */
        MqttConfig loadMqttConfig();

        /**
         * Save MQTT configuration to NVS
         * @param config MQTT configuration to save
         */
        void saveMqttConfig(const MqttConfig &config);

        EnergyConfig loadEnergyConfig();
        void saveEnergyConfig(const EnergyConfig &config);

        SyslogConfig loadSyslogConfig();
        void saveSyslogConfig(const SyslogConfig &config);
    };
} // namespace Config

#endif //KLIMACONTROL_CONFIG_H
