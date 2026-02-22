#include "Config.h"

#ifdef ARDUINO
#include <esp_system.h>
#endif

namespace Config {
    ConfigManager::ConfigManager() {
    }

    void ConfigManager::requestRestart(uint32_t delayMs) {
        restartAt = millis() + delayMs;
        restartRequested = true;
        Serial.printf("Config: Restart requested in %u ms\n", delayMs);
    }

    void ConfigManager::checkRestart() {
        if (restartRequested && millis() >= restartAt) {
            Serial.println("Config: Performing scheduled restart...");
#ifdef ARDUINO
            ESP.restart();
#endif
        }
    }

    void ConfigManager::begin() {
#ifdef ARDUINO
        // Preferences are initialized when needed in each method
        prefs.begin(NAMESPACE, true); // Read-only mode
#endif
    }

    bool ConfigManager::isConfigured() {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, true); // Read-only mode
        bool configured = prefs.getBool("configured", false);
        prefs.end();
        return configured;
#else
        return false;
#endif
    }

    void ConfigManager::markUnconfigured() {
        prefs.begin(NAMESPACE, false); // Read-only mode
        prefs.putBool("configured", false);
        prefs.end();
    }

    WiFiConfig ConfigManager::loadWiFiConfig() {
        WiFiConfig config;

#ifdef ARDUINO
        prefs.begin(NAMESPACE, true); // Read-only mode

        config.configured = prefs.getBool("configured", false);
        config.connection_failures = prefs.getUChar("wifi_failures", 0);

        if (config.configured) {
            prefs.getString("wifi_ssid", config.ssid, sizeof(config.ssid));
            prefs.getString("wifi_pass", config.password, sizeof(config.password));
        }

        prefs.end();
#endif

        return config;
    }

    void ConfigManager::saveWiFiConfig(const WiFiConfig &config) {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-write mode

        prefs.putString("wifi_ssid", config.ssid);
        prefs.putString("wifi_pass", config.password);
        prefs.putBool("configured", config.configured);

        prefs.end();
#endif
    }

    ShowConfig ConfigManager::loadShowConfig() {
        ShowConfig config;

#ifdef ARDUINO
        prefs.begin(NAMESPACE, true); // Read-only mode

        prefs.getString("show_name", config.current_show, sizeof(config.current_show));

        // If no show name is stored, default constructor already set "Rainbow"
        if (config.current_show[0] == '\0') {
            strcpy(config.current_show, "Rainbow");
        }

        prefs.getString("show_params", config.params_json, sizeof(config.params_json));

        // If no params stored, default constructor already set "{}"
        if (config.params_json[0] == '\0') {
            strcpy(config.params_json, "{}");
        }

        prefs.end();
#endif

        return config;
    }

    void ConfigManager::saveShowConfig(const ShowConfig &config) {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-write mode

        prefs.putString("show_name", config.current_show);
        prefs.putString("show_params", config.params_json);

        prefs.end();
#endif
    }

    DeviceConfig ConfigManager::loadDeviceConfig() {
        DeviceConfig config;

#ifdef ARDUINO
        prefs.begin(NAMESPACE, true); // Read-only mode

        config.brightness = prefs.getUChar("brightness", 128);
        config.num_pixels = prefs.getUShort("num_pixels", 1);
        config.led_pin = prefs.getUChar("led_pin", PIN_NEOPIXEL);
        config.cycle_time = prefs.getUShort("cycle_time", 10);
        prefs.getString("device_name", config.device_name, sizeof(config.device_name));
        
        // Load sensor configuration
        config.i2c_sda_pin = prefs.getUChar("i2c_sda_pin", 8);
        config.i2c_scl_pin = prefs.getUChar("i2c_scl_pin", 9);
        config.sensor_i2c_address = prefs.getUChar("sensor_i2c_address", 0x44);
        config.target_temperature = prefs.getFloat("target_temperature", 22.0f);
        config.temperature_control_enabled = prefs.getBool("temperature_control_enabled", false);

        prefs.end();

        // Always generate device ID from MAC
        String deviceId = getDeviceId();
        strncpy(config.device_id, deviceId.c_str(), sizeof(config.device_id) - 1);
        config.device_id[sizeof(config.device_id) - 1] = '\0';

        // If no custom name is set, use device ID as name
        if (config.device_name[0] == '\0') {
            strncpy(config.device_name, config.device_id, sizeof(config.device_name) - 1);
            config.device_name[sizeof(config.device_name) - 1] = '\0';
        }
#endif

        return config;
    }

    void ConfigManager::saveDeviceConfig(const DeviceConfig &config) {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-write mode

        prefs.putUChar("brightness", config.brightness);
        prefs.putUShort("num_pixels", config.num_pixels);
        prefs.putUChar("led_pin", config.led_pin);
        prefs.putUShort("cycle_time", config.cycle_time);
        prefs.putString("device_name", config.device_name);
        
        // Save sensor configuration
        prefs.putUChar("i2c_sda_pin", config.i2c_sda_pin);
        prefs.putUChar("i2c_scl_pin", config.i2c_scl_pin);
        prefs.putUChar("sensor_i2c_address", config.sensor_i2c_address);
        prefs.putFloat("target_temperature", config.target_temperature);
        prefs.putBool("temperature_control_enabled", config.temperature_control_enabled);
        
        // Note: device_id is derived from MAC, not stored

        prefs.end();
#endif
    }

    void ConfigManager::reset() {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-write mode
        prefs.clear(); // Clear all keys in this namespace
        prefs.end();
#endif
    }

    String ConfigManager::getDeviceId() {
#ifdef ARDUINO
        uint64_t mac = ESP.getEfuseMac();
        uint8_t mac_bytes[6];
        memcpy(mac_bytes, &mac, 6);

        char id[16];
        snprintf(id, sizeof(id), "%02X%02X%02X",
                 mac_bytes[3], mac_bytes[4], mac_bytes[5]);
        return String(id);
#else
        return String("ledz-000000");
#endif
    }

    uint8_t ConfigManager::incrementConnectionFailures() {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-write mode
        uint8_t failures = prefs.getUChar("wifi_failures", 0);
        failures++;
        prefs.putUChar("wifi_failures", failures);
        prefs.end();
        return failures;
#else
        return 0;
#endif
    }

    void ConfigManager::resetConnectionFailures() {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-write mode
        prefs.putUChar("wifi_failures", 0);
        prefs.end();
#endif
    }

    uint8_t ConfigManager::getConnectionFailures() {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, true); // Read-only mode
        uint8_t failures = prefs.getUChar("wifi_failures", 0);
        prefs.end();
        return failures;
#else
        return 0;
#endif
    }

    LayoutConfig ConfigManager::loadLayoutConfig() {
        LayoutConfig config;

#ifdef ARDUINO
        prefs.begin(NAMESPACE, true); // Read-only mode
        config.reverse = prefs.getBool("layout_reverse", false);
        config.mirror = prefs.getBool("layout_mirror", false);
        config.dead_leds = prefs.getUShort("layout_dead", 0);
        prefs.end();
#endif

        return config;
    }

    void ConfigManager::saveLayoutConfig(const LayoutConfig &config) {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-write mode
        prefs.putBool("layout_reverse", config.reverse);
        prefs.putBool("layout_mirror", config.mirror);
        prefs.putUShort("layout_dead", config.dead_leds);
        prefs.end();

        Serial.printf("Config: Saved layout - reverse=%d, mirror=%d, dead_leds=%u\n",
                      config.reverse, config.mirror, config.dead_leds);
#endif
    }

    PresetsConfig ConfigManager::loadPresetsConfig() {
        PresetsConfig presetsConfig;

#ifdef ARDUINO
        prefs.begin(NAMESPACE, true); // Read-only mode

        char key[20];
        for (uint8_t i = 0; i < PresetsConfig::MAX_PRESETS; i++) {
            snprintf(key, sizeof(key), "preset_%u_valid", i);
            presetsConfig.presets[i].valid = prefs.getBool(key, false);

            if (presetsConfig.presets[i].valid) {
                snprintf(key, sizeof(key), "preset_%u_name", i);
                prefs.getString(key, presetsConfig.presets[i].name, sizeof(presetsConfig.presets[i].name));

                snprintf(key, sizeof(key), "preset_%u_show", i);
                prefs.getString(key, presetsConfig.presets[i].show_name, sizeof(presetsConfig.presets[i].show_name));

                snprintf(key, sizeof(key), "preset_%u_params", i);
                prefs.getString(key, presetsConfig.presets[i].params_json, sizeof(presetsConfig.presets[i].params_json));

                snprintf(key, sizeof(key), "preset_%u_rev", i);
                presetsConfig.presets[i].layout_reverse = prefs.getBool(key, false);

                snprintf(key, sizeof(key), "preset_%u_mir", i);
                presetsConfig.presets[i].layout_mirror = prefs.getBool(key, false);

                snprintf(key, sizeof(key), "preset_%u_dead", i);
                presetsConfig.presets[i].layout_dead_leds = prefs.getShort(key, 0);
            }
        }

        prefs.end();
#endif

        return presetsConfig;
    }

    bool ConfigManager::savePreset(uint8_t index, const Preset &preset) {
        if (index >= PresetsConfig::MAX_PRESETS) {
            return false;
        }

#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-write mode

        char key[20];

        snprintf(key, sizeof(key), "preset_%u_valid", index);
        prefs.putBool(key, preset.valid);

        snprintf(key, sizeof(key), "preset_%u_name", index);
        prefs.putString(key, preset.name);

        snprintf(key, sizeof(key), "preset_%u_show", index);
        prefs.putString(key, preset.show_name);

        snprintf(key, sizeof(key), "preset_%u_params", index);
        prefs.putString(key, preset.params_json);

        snprintf(key, sizeof(key), "preset_%u_rev", index);
        prefs.putBool(key, preset.layout_reverse);

        snprintf(key, sizeof(key), "preset_%u_mir", index);
        prefs.putBool(key, preset.layout_mirror);

        snprintf(key, sizeof(key), "preset_%u_dead", index);
        prefs.putShort(key, preset.layout_dead_leds);

        prefs.end();

        Serial.printf("Config: Saved preset %u '%s'\n", index, preset.name);
        return true;
#else
        return false;
#endif
    }

    bool ConfigManager::deletePreset(uint8_t index) {
        if (index >= PresetsConfig::MAX_PRESETS) {
            return false;
        }

#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-write mode

        char key[20];

        // Just mark as invalid - NVS keys remain but won't be loaded
        snprintf(key, sizeof(key), "preset_%u_valid", index);
        prefs.putBool(key, false);

        prefs.end();

        Serial.printf("Config: Deleted preset %u\n", index);
        return true;
#else
        return false;
#endif
    }

    int ConfigManager::findPresetByName(const char *name) {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, true); // Read-only mode

        char key[20];
        char storedName[32];

        for (uint8_t i = 0; i < PresetsConfig::MAX_PRESETS; i++) {
            snprintf(key, sizeof(key), "preset_%u_valid", i);
            if (prefs.getBool(key, false)) {
                snprintf(key, sizeof(key), "preset_%u_name", i);
                prefs.getString(key, storedName, sizeof(storedName));
                if (strcmp(storedName, name) == 0) {
                    prefs.end();
                    return i;
                }
            }
        }

        prefs.end();
#endif
        return -1;
    }

    int ConfigManager::getNextPresetSlot() {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, true); // Read-only mode

        char key[20];

        for (uint8_t i = 0; i < PresetsConfig::MAX_PRESETS; i++) {
            snprintf(key, sizeof(key), "preset_%u_valid", i);
            if (!prefs.getBool(key, false)) {
                prefs.end();
                return i;
            }
        }

        prefs.end();
#endif
        return -1;
    }

    TimersConfig ConfigManager::loadTimersConfig() {
        TimersConfig timersConfig;

#ifdef ARDUINO
        prefs.begin(NAMESPACE, true); // Read-only mode

        timersConfig.timezone_offset_hours = prefs.getChar("tz_offset", 0);

        char key[20];
        for (uint8_t i = 0; i < TimersConfig::MAX_TIMERS; i++) {
            snprintf(key, sizeof(key), "timer_%u_enabled", i);
            timersConfig.timers[i].enabled = prefs.getBool(key, false);

            if (timersConfig.timers[i].enabled) {
                snprintf(key, sizeof(key), "timer_%u_type", i);
                timersConfig.timers[i].type = static_cast<TimerType>(prefs.getUChar(key, 0));

                snprintf(key, sizeof(key), "timer_%u_action", i);
                timersConfig.timers[i].action = static_cast<TimerAction>(prefs.getUChar(key, 0));

                snprintf(key, sizeof(key), "timer_%u_preset", i);
                timersConfig.timers[i].preset_index = prefs.getUChar(key, 0);

                snprintf(key, sizeof(key), "timer_%u_target", i);
                timersConfig.timers[i].target_time = prefs.getULong(key, 0);

                snprintf(key, sizeof(key), "timer_%u_dur", i);
                timersConfig.timers[i].duration_seconds = prefs.getULong(key, 0);
            }
        }

        prefs.end();
#endif

        return timersConfig;
    }

    void ConfigManager::saveTimersConfig(const TimersConfig &config) {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-write mode

        prefs.putChar("tz_offset", config.timezone_offset_hours);

        char key[20];
        for (uint8_t i = 0; i < TimersConfig::MAX_TIMERS; i++) {
            snprintf(key, sizeof(key), "timer_%u_enabled", i);
            prefs.putBool(key, config.timers[i].enabled);

            if (config.timers[i].enabled) {
                snprintf(key, sizeof(key), "timer_%u_type", i);
                prefs.putUChar(key, static_cast<uint8_t>(config.timers[i].type));

                snprintf(key, sizeof(key), "timer_%u_action", i);
                prefs.putUChar(key, static_cast<uint8_t>(config.timers[i].action));

                snprintf(key, sizeof(key), "timer_%u_preset", i);
                prefs.putUChar(key, config.timers[i].preset_index);

                snprintf(key, sizeof(key), "timer_%u_target", i);
                prefs.putULong(key, config.timers[i].target_time);

                snprintf(key, sizeof(key), "timer_%u_dur", i);
                prefs.putULong(key, config.timers[i].duration_seconds);
            }
        }

        prefs.end();

        Serial.println("Config: Saved timers configuration");
#endif
    }

    TouchConfig ConfigManager::loadTouchConfig() {
        TouchConfig touchConfig;

#ifdef ARDUINO
        prefs.begin(NAMESPACE, true); // Read-only mode

        touchConfig.enabled = prefs.getBool("touch_enabled", true);
        touchConfig.threshold = prefs.getUShort("touch_thresh", 45000);

        prefs.end();

        Serial.printf("TouchConfig: Loaded - enabled=%d, threshold=%u\n",
                      touchConfig.enabled, touchConfig.threshold);
#endif

        return touchConfig;
    }

    void ConfigManager::saveTouchConfig(const TouchConfig &config) {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-write mode

        prefs.putBool("touch_enabled", config.enabled);
        prefs.putUShort("touch_thresh", config.threshold);

        prefs.end();

        Serial.printf("TouchConfig: Saved - enabled=%d, threshold=%u\n",
                      config.enabled, config.threshold);
#endif
    }
} // namespace Config
