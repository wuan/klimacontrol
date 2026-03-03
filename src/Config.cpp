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
        Serial.printf("Config: Restart requested in %u ms\r\n", delayMs);
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

    DeviceConfig ConfigManager::loadDeviceConfig() {
        DeviceConfig config;

#ifdef ARDUINO
        prefs.begin(NAMESPACE, true); // Read-only mode

        prefs.getString("device_name", config.device_name, sizeof(config.device_name));
        
        // Load sensor configuration
        config.sensor_i2c_address = prefs.getUChar("sensor_i2c_address", 0x44);
        config.target_temperature = prefs.getFloat("target_temperature", 22.0f);
        config.temperature_control_enabled = prefs.getBool("temperature_control_enabled", false);
        config.elevation = prefs.getFloat("elevation", 0.0f);

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

        prefs.putString("device_name", config.device_name);
        
        // Save sensor configuration
        prefs.putUChar("sensor_i2c_address", config.sensor_i2c_address);
        prefs.putFloat("target_temperature", config.target_temperature);
        prefs.putBool("temperature_control_enabled", config.temperature_control_enabled);
        prefs.putFloat("elevation", config.elevation);

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
        return String("klima-000000");
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

        Serial.printf("TouchConfig: Loaded - enabled=%d, threshold=%u\r\n",
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

        Serial.printf("TouchConfig: Saved - enabled=%d, threshold=%u\r\n",
                      config.enabled, config.threshold);
#endif
    }
    SensorConfig ConfigManager::loadSensorConfig() {
        SensorConfig sensorConfig;

#ifdef ARDUINO
        prefs.begin(NAMESPACE, true); // Read-only mode

        String assign = prefs.getString("sns_assign", "");

        strncpy(sensorConfig.assignments, assign.c_str(), sizeof(sensorConfig.assignments) - 1);
        sensorConfig.assignments[sizeof(sensorConfig.assignments) - 1] = '\0';

        prefs.end();

        Serial.printf("SensorConfig: Loaded assignments='%s'\r\n", sensorConfig.assignments);
#endif

        return sensorConfig;
    }

    void ConfigManager::saveSensorConfig(const SensorConfig &config) {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-write mode

        prefs.putString("sns_assign", config.assignments);

        prefs.end();

        Serial.printf("SensorConfig: Saved assignments='%s'\r\n", config.assignments);
#endif
    }

    MqttConfig ConfigManager::loadMqttConfig() {
        MqttConfig mqttConfig;

#ifdef ARDUINO
        prefs.begin(NAMESPACE, true); // Read-only mode

        mqttConfig.enabled = prefs.getBool("mqtt_enabled", false);
        prefs.getString("mqtt_host", mqttConfig.host, sizeof(mqttConfig.host));
        mqttConfig.port = prefs.getUShort("mqtt_port", 1883);
        prefs.getString("mqtt_user", mqttConfig.username, sizeof(mqttConfig.username));
        prefs.getString("mqtt_pass", mqttConfig.password, sizeof(mqttConfig.password));
        prefs.getString("mqtt_prefix", mqttConfig.prefix, sizeof(mqttConfig.prefix));
        mqttConfig.interval = prefs.getUShort("mqtt_interval", 15);

        if (mqttConfig.prefix[0] == '\0') {
            strcpy(mqttConfig.prefix, "sensors");
        }

        prefs.end();
#endif

        return mqttConfig;
    }

    void ConfigManager::saveMqttConfig(const MqttConfig &config) {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-write mode

        prefs.putBool("mqtt_enabled", config.enabled);
        prefs.putString("mqtt_host", config.host);
        prefs.putUShort("mqtt_port", config.port);
        prefs.putString("mqtt_user", config.username);
        prefs.putString("mqtt_pass", config.password);
        prefs.putString("mqtt_prefix", config.prefix);
        prefs.putUShort("mqtt_interval", config.interval);

        prefs.end();

        Serial.println("Config: Saved MQTT configuration");
#endif
    }
} // namespace Config
