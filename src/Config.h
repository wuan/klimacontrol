#ifndef LEDZ_CONFIG_H
#define LEDZ_CONFIG_H

#ifdef ARDUINO
#include <Preferences.h>
#include <Arduino.h>
#endif

namespace Config {
    /**
     * JSON Document size constants
     */
    constexpr size_t JSON_DOC_TINY   = 128;
    constexpr size_t JSON_DOC_SMALL  = 256;
    constexpr size_t JSON_DOC_MEDIUM = 512;
    constexpr size_t JSON_DOC_LARGE  = 1024;
    constexpr size_t JSON_DOC_XLARGE = 2048;
    constexpr size_t JSON_DOC_OTA    = 8192;

    /**
     * WiFi configuration structure
     */
    struct WiFiConfig {
        char ssid[64];
        char password[64];
        bool configured;
        uint8_t connection_failures; // Track consecutive connection failures

        WiFiConfig() : configured(false), connection_failures(0) {
            ssid[0] = '\0';
            password[0] = '\0';
        }
    };

    /**
     * LED show configuration structure
     */
    struct ShowConfig {
        char current_show[32]; // e.g., "Rainbow", "Mandelbrot"
        char params_json[256]; // JSON string for show parameters

        ShowConfig() {
            strcpy(current_show, "Rainbow");
            strcpy(params_json, "{}");
        }
    };

    /**
     * Device configuration structure
     */
    struct DeviceConfig {
        uint8_t brightness; // 0-255
        uint16_t num_pixels;
        uint8_t led_pin; // GPIO pin for LED strip (default: PIN_NEOPIXEL=39 for onboard, or 35=MOSI for external)
        uint16_t cycle_time; // Cycle time in ms (e.g., 10, 20, 25, 50)
        char device_id[16]; // e.g., "AABBCC"
        char device_name[32]; // Custom device name
        
        // Sensor and temperature control configuration
        uint8_t i2c_sda_pin; // I2C SDA pin
        uint8_t i2c_scl_pin; // I2C SCL pin
        uint8_t sensor_i2c_address; // Default I2C address for sensors
        float target_temperature; // Target temperature for control
        bool temperature_control_enabled; // Temperature control enabled
        
        DeviceConfig() : brightness(128), num_pixels(300),
#ifdef PIN_NEOPIXEL
            led_pin(PIN_NEOPIXEL),
#else
            led_pin(39),
#endif
            cycle_time(10),
            i2c_sda_pin(8), // Default I2C pins for QT Py ESP32-S3
            i2c_scl_pin(9),
            sensor_i2c_address(0x44), // Default SHT4x address
            target_temperature(22.0f),
            temperature_control_enabled(false)
        {
            device_id[0] = '\0';
            device_name[0] = '\0';
        }
    };

    /**
     * LED strip layout configuration structure
     */
    struct LayoutConfig {
        bool reverse; // Reverse LED order
        bool mirror; // Mirror LED pattern
        int16_t dead_leds; // Number of dead LEDs at the end

        LayoutConfig() : reverse(false), mirror(false), dead_leds(0) {
        }
    };

    /**
     * Show preset structure
     */
    struct Preset {
        char name[32];
        char show_name[32];
        char params_json[256];
        bool layout_reverse;
        bool layout_mirror;
        int16_t layout_dead_leds;
        bool valid;

        Preset() : layout_reverse(false), layout_mirror(false),
                   layout_dead_leds(0), valid(false) {
            name[0] = '\0';
            show_name[0] = '\0';
            strcpy(params_json, "{}");
        }
    };

    /**
     * Presets configuration structure
     */
    struct PresetsConfig {
        static constexpr uint8_t MAX_PRESETS = 8;
        Preset presets[MAX_PRESETS];
    };

    /**
     * Timer action types
     */
    enum class TimerAction : uint8_t {
        LOAD_PRESET = 0,  // Load a preset by index
        TURN_OFF = 1      // Turn off LEDs (Solid show with black color)
    };

    /**
     * Timer types
     */
    enum class TimerType : uint8_t {
        COUNTDOWN = 0,    // One-shot countdown timer (duration-based)
        ALARM_DAILY = 1   // Recurring daily alarm
    };

    /**
     * Timer entry structure
     */
    struct TimerEntry {
        bool enabled = false;
        TimerType type = TimerType::COUNTDOWN;
        TimerAction action = TimerAction::TURN_OFF;
        uint8_t preset_index = 0;
        uint32_t target_time = 0;      // epoch for COUNTDOWN, seconds-since-midnight for ALARM_DAILY
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
     * Touch control configuration structure
     * Maps touch pins to preset indices for physical control
     */
    struct TouchConfig {
        static constexpr uint8_t MAX_TOUCH_PINS = 3;
        bool enabled;                              // Touch control enabled
        uint16_t threshold;                        // Touch detection threshold (lower = more sensitive)

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
        static constexpr const char *NAMESPACE = "ledz";

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
         * Load show configuration from NVS
         * @return ShowConfig structure with defaults if not found
         */
        ShowConfig loadShowConfig();

        /**
         * Save show configuration to NVS
         * @param config Show configuration to save
         */
        void saveShowConfig(const ShowConfig &config);

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
         * Load layout configuration from NVS
         * @return LayoutConfig structure with defaults if not found
         */
        LayoutConfig loadLayoutConfig();

        /**
         * Save layout configuration to NVS
         * @param config Layout configuration to save
         */
        void saveLayoutConfig(const LayoutConfig &config);

        /**
         * Load all presets configuration from NVS
         * @return PresetsConfig structure
         */
        PresetsConfig loadPresetsConfig();

        /**
         * Save a preset to NVS
         * @param index Preset slot index (0-7)
         * @param preset Preset to save
         * @return true if saved successfully
         */
        bool savePreset(uint8_t index, const Preset &preset);

        /**
         * Delete a preset from NVS
         * @param index Preset slot index (0-7)
         * @return true if deleted successfully
         */
        bool deletePreset(uint8_t index);

        /**
         * Find a preset by name
         * @param name Preset name to search for
         * @return Preset index (0-7) or -1 if not found
         */
        int findPresetByName(const char *name);

        /**
         * Get next available preset slot
         * @return Preset index (0-7) or -1 if all slots full
         */
        int getNextPresetSlot();

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
    };
} // namespace Config

#endif //LEDZ_CONFIG_H