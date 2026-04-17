#include "Config.h"
#include <cmath>

#ifdef ARDUINO
#include <esp_system.h>
#include "Log.h"
#endif

[[maybe_unused]] static constexpr const char* TAG = "config";

namespace Config {
    ConfigManager::ConfigManager() = default;

    void ConfigManager::requestRestart([[maybe_unused]] uint32_t delayMs) {
#ifdef ARDUINO
        restartAt = millis() + delayMs;
        restartRequested = true;
        ESP_LOGI(TAG, "Restart requested in %u ms", delayMs);
#endif
    }

    void ConfigManager::checkRestart() {
#ifdef ARDUINO
        if (restartRequested && millis() >= restartAt) {
            ESP_LOGI(TAG, "Performing scheduled restart...");
            ESP.restart();
        }
#endif
    }

    void ConfigManager::begin() {
#ifdef ARDUINO
        // Preferences are initialized when needed in each method
        prefs.begin(NAMESPACE, true); // Read-only mode
#endif
        // Initialize in-memory device config cache from NVS
        loadDeviceConfig();
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
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-only mode
        prefs.putBool("configured", false);
        prefs.end();
#endif
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

    void ConfigManager::saveWiFiConfig([[maybe_unused]] const WiFiConfig &config) {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-write mode

        prefs.putString("wifi_ssid", config.ssid);
        prefs.putString("wifi_pass", config.password);
        prefs.putBool("configured", config.configured);

        prefs.end();
#endif
    }

    void validateDeviceConfig(DeviceConfig &config) {
        if (std::isnan(config.target_temperature) || config.target_temperature < 10.0f || config.target_temperature > 30.0f) {
            config.target_temperature = 22.0f;
        }
        if (std::isnan(config.elevation) || config.elevation < -500.0f || config.elevation > 9000.0f) {
            config.elevation = 0.0f;
        }
    }

    DeviceConfig ConfigManager::loadDeviceConfig() {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, true); // Read-only mode

        String deviceId = getDeviceId();
        strlcpy(deviceConfig.device_id, deviceId.c_str(), sizeof(deviceConfig.device_id));

        // Load device name (use device ID as fallback)
        String deviceName = prefs.getString("device_name", "");
        if (deviceName.length() > 0) {
            strlcpy(deviceConfig.device_name, deviceName.c_str(), sizeof(deviceConfig.device_name));
        } else {
            strlcpy(deviceConfig.device_name, deviceConfig.device_id, sizeof(deviceConfig.device_name));
        }

        // Load other device settings
        deviceConfig.target_temperature = prefs.getFloat(TARGET_TEMPERATURE, 22.0f);
        deviceConfig.temperature_control_enabled = prefs.getBool(TEMPERATURE_CONTROL_ENABLED, false);
        deviceConfig.elevation = prefs.getFloat(ELEVATION, 0.0f);

        prefs.end();
#endif

        // Validate ranges — NVS may hold garbage after flash corruption
        validateDeviceConfig(deviceConfig);

        return deviceConfig;
    }

    void ConfigManager::saveDeviceConfig([[maybe_unused]] const DeviceConfig &config) {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-write mode

        prefs.putString("device_name", config.device_name);

        prefs.putFloat(TARGET_TEMPERATURE, config.target_temperature);
        prefs.putBool(TEMPERATURE_CONTROL_ENABLED, config.temperature_control_enabled);
        prefs.putFloat(ELEVATION, config.elevation);

        prefs.end();
#endif
        // Also update in-memory cache
        deviceConfig = config;
    }
    
    void ConfigManager::updateDeviceName([[maybe_unused]] const char* device_name) {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false);
        prefs.putString("device_name", device_name);
        prefs.end();
#endif
        strlcpy(deviceConfig.device_name, device_name, sizeof(deviceConfig.device_name));
    }

    void ConfigManager::updateTargetTemperature([[maybe_unused]] float temperature) {
        // Validate — same logic as validateDeviceConfig()
        if (std::isnan(temperature) || temperature < 10.0f || temperature > 30.0f) {
            temperature = 22.0f;
        }
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false);
        prefs.putFloat(TARGET_TEMPERATURE, temperature);
        prefs.end();
#endif
        deviceConfig.target_temperature = temperature;
    }

    void ConfigManager::updateTemperatureControlEnabled([[maybe_unused]] bool enabled) {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false);
        prefs.putBool(TEMPERATURE_CONTROL_ENABLED, enabled);
        prefs.end();
#endif
        deviceConfig.temperature_control_enabled = enabled;
    }

    void ConfigManager::updateElevation([[maybe_unused]] float elevation) {
        // Validate — same logic as validateDeviceConfig()
        if (std::isnan(elevation) || elevation < -500.0f || elevation > 9000.0f) {
            elevation = 0.0f;
        }
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false);
        prefs.putFloat(ELEVATION, elevation);
        prefs.end();
#endif
        deviceConfig.elevation = elevation;
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

    SensorConfig ConfigManager::loadSensorConfig() {
        SensorConfig sensorConfig;

#ifdef ARDUINO
        prefs.begin(NAMESPACE, true); // Read-only mode

        String assign = prefs.getString("sns_assign", "");

        strlcpy(sensorConfig.assignments, assign.c_str(), sizeof(sensorConfig.assignments));

        prefs.end();

        ESP_LOGD(TAG, "SensorConfig: Loaded assignments='%s'", sensorConfig.assignments);
#endif

        return sensorConfig;
    }

    void ConfigManager::saveSensorConfig([[maybe_unused]] const SensorConfig &config) {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-write mode

        prefs.putString("sns_assign", config.assignments);

        prefs.end();

        ESP_LOGD(TAG, "SensorConfig: Saved assignments='%s'", config.assignments);
#endif
    }

    void validateMqttConfig(MqttConfig &config) {
        if (config.prefix[0] == '\0') {
            strlcpy(config.prefix, "sensors", sizeof(config.prefix));
        }
        if (config.port == 0) {
            config.port = 1883;
        }
        if (config.interval < 1 || config.interval > 3600) {
            config.interval = 15;
        }
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

        prefs.end();
#endif

        // Validate ranges — NVS may hold garbage after flash corruption
        validateMqttConfig(mqttConfig);

        return mqttConfig;
    }

    void ConfigManager::saveMqttConfig([[maybe_unused]] const MqttConfig &config) {
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

        ESP_LOGD(TAG, "Saved MQTT configuration");
#endif
    }
    void validateEnergyConfig(EnergyConfig &config) {
        uint8_t wp = config.wifi_power;
        if (wp != 8 && wp != 34 && wp != 52 && wp != 68 && wp != 80) {
            config.wifi_power = Constants::DEFAULT_WIFI_POWER;
        }
    }

    EnergyConfig ConfigManager::loadEnergyConfig() {
        EnergyConfig energyConfig;

#ifdef ARDUINO
        prefs.begin(NAMESPACE, true);

        energyConfig.wifi_power = prefs.getUChar(ENERGY_WIFI_PW, Constants::DEFAULT_WIFI_POWER);

        prefs.end();
#endif

        // Validate wifi_power is one of the known values
        validateEnergyConfig(energyConfig);

        return energyConfig;
    }

    void ConfigManager::saveEnergyConfig([[maybe_unused]] const EnergyConfig &config) {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false);

        prefs.putUChar(ENERGY_WIFI_PW, config.wifi_power);

        prefs.end();

        ESP_LOGD(TAG, "Saved energy configuration");
#endif
    }
    SyslogConfig ConfigManager::loadSyslogConfig() {
        SyslogConfig config;
#ifdef ARDUINO
        prefs.begin(NAMESPACE, true);

        config.enabled = prefs.getBool("syslog_on", false);
        config.port = prefs.getUShort("syslog_port", 514);
        prefs.getString("syslog_host", config.host, sizeof(config.host));

        prefs.end();
#endif
        return config;
    }

    void ConfigManager::saveSyslogConfig([[maybe_unused]] const SyslogConfig &config) {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false);

        prefs.putBool("syslog_on", config.enabled);
        prefs.putUShort("syslog_port", config.port);
        prefs.putString("syslog_host", config.host);

        prefs.end();

        ESP_LOGD(TAG, "Saved syslog configuration");
#endif
    }
} // namespace Config
