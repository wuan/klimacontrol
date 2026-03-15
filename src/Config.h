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
        char ssid[64];
        char password[64];
        bool configured;
        uint8_t connection_failures; // Track consecutive connection failures

        WiFiConfig() : configured(false), connection_failures(0), ssid(""), password("") {
        }
    };

    /**
     * Device configuration structure
     */
    struct DeviceConfig {
        char device_id[16]; // e.g., "AABBCC"
        char device_name[32]; // Custom device name

        // Sensor and temperature control configuration
        uint8_t sensor_i2c_address; // Default I2C address for sensors
        float target_temperature; // Target temperature for control
        bool temperature_control_enabled; // Temperature control enabled
        float elevation; // Meters above sea level, for sea-level pressure calculation

        DeviceConfig() : sensor_i2c_address(0x44), // Default SHT4x address
                         target_temperature(22.0f),
                         temperature_control_enabled(false),
                         elevation(0.0f),
                         device_id(""),
                         device_name("") {
        }
    };

    /**
     * Timer action types
     */
    enum class TimerAction : uint8_t {
        ENABLE_CONTROL = 0,
        DISABLE_CONTROL = 1,
        UPDATE_SETPOINT = 2
    };

    /**
     * Timer types
     */
    enum class TimerType : uint8_t {
        COUNTDOWN = 0, // One-shot countdown timer (duration-based)
        ALARM_DAILY = 1 // Recurring daily alarm
    };

    /**
     * Timer entry structure
     */
    struct TimerEntry {
        bool enabled = false;
        TimerType type = TimerType::COUNTDOWN;
        TimerAction action = TimerAction::ENABLE_CONTROL;
        uint8_t preset_index = 0;
        uint32_t target_time = 0; // epoch for COUNTDOWN, seconds-since-midnight for ALARM_DAILY
        uint32_t duration_seconds = 0; // original duration for countdown display
    };

    /**
     * Timers configuration structure
     */
    struct TimersConfig {
        static constexpr uint8_t MAX_TIMERS = 4;
        TimerEntry timers[MAX_TIMERS];
        int8_t timezone_offset_hours = 0;
    };

    /**
     * MQTT configuration structure
     */
    struct MqttConfig {
        char host[64];
        uint16_t port;
        char username[64];
        char password[64];
        char prefix[64]; // topic prefix, e.g. "sensors/bedroom"
        uint16_t interval; // publish interval in seconds
        bool enabled;

        MqttConfig() : port(1883), interval(15), enabled(false) {
            host[0] = '\0';
            username[0] = '\0';
            password[0] = '\0';
            strcpy(prefix, "sensors");
        }
    };

    /**
     * Sensor configuration structure
     * Stores sensor assignments as a compact string like "44=SHT4x,77=BME680,59=SGP40"
     */
    struct SensorConfig {
        static constexpr uint32_t MIN_INTERVAL_MS = 1000;   ///< Minimum sensor reading interval
        static constexpr uint32_t MAX_INTERVAL_MS = 300000; ///< Maximum sensor reading interval

        char assignments[128]; // e.g. "44=SHT4x,77=BME680,59=SGP40"
        uint32_t sensor_interval_ms; // Sensor reading interval in milliseconds (min 1000, max 300000)

        SensorConfig() : sensor_interval_ms(5000) { assignments[0] = '\0'; }

        /** Clamp ms to [MIN_INTERVAL_MS, MAX_INTERVAL_MS]. */
        static uint32_t clampInterval(uint32_t ms) {
            if (ms < MIN_INTERVAL_MS) return MIN_INTERVAL_MS;
            if (ms > MAX_INTERVAL_MS) return MAX_INTERVAL_MS;
            return ms;
        }
    };

    /**
     * Touch control configuration structure
     * Maps touch pins to preset indices for physical control
     */
    struct TouchConfig {
        static constexpr uint8_t MAX_TOUCH_PINS = 3;
        bool enabled; // Touch control enabled
        uint16_t threshold; // Touch detection threshold (lower = more sensitive)

        TouchConfig() : enabled(true), threshold(45000) {
        }
    };

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


        bool restartRequested = false;
        uint32_t restartAt = 0;

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
         * Save device configuration to NVS
         * @param config Device configuration to save
         */
        void saveDeviceConfig(const DeviceConfig &config);

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
         * Load timers configuration from NVS
         * @return TimersConfig structure
         */
        TimersConfig loadTimersConfig();

        /**
         * Save timers configuration to NVS
         * @param config Timers configuration to save
         */
        void saveTimersConfig(const TimersConfig &config);

        /**
         * Load touch configuration from NVS
         * @return TouchConfig structure
         */
        TouchConfig loadTouchConfig();

        /**
         * Save touch configuration to NVS
         * @param config Touch configuration to save
         */
        void saveTouchConfig(const TouchConfig &config);

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
    };
} // namespace Config

#endif //KLIMACONTROL_CONFIG_H
