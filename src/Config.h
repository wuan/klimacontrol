#ifndef KLIMACONTROL_CONFIG_H
#define KLIMACONTROL_CONFIG_H

#include <atomic>
#include <cstdint>

#ifdef ARDUINO
#include <Preferences.h>
#include <Arduino.h>
#else
#include <cstring>
#include <string>
using String = std::string;
#endif

#include "Constants.h"

#ifdef ARDUINO
/**
 * RAII wrapper for Preferences to ensure proper cleanup.
 * Automatically calls end() in destructor, even if exception is thrown.
 */
class PreferencesGuard {
    Preferences& prefs;
public:
    /**
     * Constructor - opens the Preferences namespace
     * @param p Preferences instance to wrap
     * @param ns Namespace name
     * @param readOnly true for read-only access, false for read-write
     */
    PreferencesGuard(Preferences& p, const char* ns, bool readOnly)
        : prefs(p) {
        prefs.begin(ns, readOnly);
    }

    /**
     * Destructor - closes the Preferences namespace
     */
    ~PreferencesGuard() {
        prefs.end();
    }

    /**
     * Get reference to the underlying Preferences object
     * @return Preferences reference
     */
    Preferences& get() { return prefs; }

    /**
     * Disallow copying
     */
    PreferencesGuard(const PreferencesGuard&) = delete;
    PreferencesGuard& operator=(const PreferencesGuard&) = delete;
};
#endif

namespace Config {
    // Valid 7-bit I2C address range (0x00-0x07 and 0x78-0x7F are reserved)
    constexpr uint8_t MIN_SENSOR_I2C_ADDRESS = 0x08;
    constexpr uint8_t MAX_SENSOR_I2C_ADDRESS = 0x77;
    constexpr uint8_t DEFAULT_SENSOR_I2C_ADDRESS = 0x44;

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
        uint8_t sensor_i2c_address = DEFAULT_SENSOR_I2C_ADDRESS; // Default I2C address for sensors
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
        uint8_t wifi_sleep_mode = 0; // 0=WIFI_PS_NONE, 1=WIFI_PS_MIN_MODEM, 2=WIFI_PS_MAX_MODEM
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

    // Deferred-restart state encoding helpers (pure C++, testable on native builds).
    // The state is packed into a single 64-bit word: low bit = "requested" flag,
    // upper 63 bits = unsigned deadline.
    constexpr uint64_t packRestartState(bool requested, uint64_t deadline) {
        return (deadline << 1) | (requested ? 1ULL : 0ULL);
    }

    constexpr void unpackRestartState(uint64_t state, bool& requested, uint64_t& deadline) {
        requested = (state & 1ULL) != 0;
        deadline  = state >> 1;
    }

    constexpr bool isRequestedOf(uint64_t state) {
        return (state & 1ULL) != 0;
    }

    constexpr uint64_t deadlineOf(uint64_t state) {
        return state >> 1;
    }

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
        static constexpr const char *ENERGY_WIFI_SLEEP = "energy_wifi_sleep";
        static constexpr const char *SENSOR_I2C_ADDRESS = "sensor_i2c_address";

        // In-memory cache of device config — always read from here, never maintain separate copies
        DeviceConfig deviceConfig;

        // Deferred-restart scheduling state.
        //
        // Written from web-request callbacks (AsyncTCP task) via requestRestart(),
        // read from the main loop task via checkRestart(). The state is packed into
        // a single 64-bit word (flag in the low bit, deadline in the upper 63 bits)
        // and guarded by a std::atomic_flag spinlock so the flag and the deadline
        // are published/consumed as one indivisible pair — no torn reads, safe on a
        // future dual-core target.
        //
        // The Xtensa toolchain reports std::atomic<uint64_t> as not lock-free, so
        // a plain atomic word would silently fall back to a libgcc call. The
        // spinlock is a few loads/stores in a critical section that's strictly
        // shorter than the I/O surrounding the call sites.
        //
        // Bit layout: low bit = "requested" flag, upper 63 bits = unsigned deadline
        // (millis()-derived, fits comfortably in 63 bits).
        mutable std::atomic_flag restartLock = ATOMIC_FLAG_INIT;
        uint64_t restartState = 0;

        // Acquire the spinlock. Caller must already be prepared to busy-wait.
        void lockRestart() const {
            while (restartLock.test_and_set(std::memory_order_acquire)) {
                // spin
            }
        }

        // Release the spinlock.
        void unlockRestart() const {
            restartLock.clear(std::memory_order_release);
        }

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
        bool isRestartPending() const {
            lockRestart();
            bool r = isRequestedOf(restartState);
            unlockRestart();
            return r;
        }

        /**
         * Request a deferred restart
         * @param delayMs Delay in milliseconds before restarting
         */
        void requestRestart(uint32_t delayMs = 1000);

        /**
         * Set the restart deadline directly. Public for native unit tests that
         * have no `millis()` clock; production code should use requestRestart().
         * @param deadline Absolute deadline in the same units as millis()
         */
        void setRestartDeadline(uint64_t deadline) {
            lockRestart();
            restartState = packRestartState(true, deadline);
            unlockRestart();
        }

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
         * Load device configuration from NVS into the in-memory cache
         * @return DeviceConfig structure with defaults if not found
         */
        DeviceConfig loadDeviceConfig();

        /**
         * Get reference to in-memory device configuration (source of truth at runtime)
         * @return reference to current DeviceConfig
         */
        const DeviceConfig& getDeviceConfig() const { return deviceConfig; }

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
        void updateSensorI2CAddress(uint8_t address);

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
