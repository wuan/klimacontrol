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
        // Initialize in-memory device config cache from NVS
        // Preferences are initialized when needed in each method
        {
            PreferencesGuard guard(prefs, NAMESPACE, true); // Read-only mode
            // Just ensure it's initialized
        }
#endif
        // Initialize in-memory device config cache from NVS
        loadDeviceConfig();
    }

    bool ConfigManager::isConfigured() {
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, true); // Read-only mode
        return guard.get().getBool("configured", false);
#else
        return false;
#endif
    }

    void ConfigManager::markUnconfigured() {
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false); // Read-write mode
        guard.get().putBool("configured", false);
#endif
    }

    WiFiConfig ConfigManager::loadWiFiConfig() {
        WiFiConfig config;

#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, true); // Read-only mode

        config.configured = guard.get().getBool("configured", false);
        config.connection_failures = guard.get().getUChar("wifi_failures", 0);

        if (config.configured) {
            guard.get().getString("wifi_ssid", config.ssid, sizeof(config.ssid));
            guard.get().getString("wifi_pass", config.password, sizeof(config.password));
        }
#endif

        return config;
    }

    void ConfigManager::saveWiFiConfig([[maybe_unused]] const WiFiConfig &config) {
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false); // Read-write mode

        guard.get().putString("wifi_ssid", config.ssid);
        guard.get().putString("wifi_pass", config.password);
        guard.get().putBool("configured", config.configured);
#endif
    }

    void validateDeviceConfig(DeviceConfig &config) {
        if (std::isnan(config.target_temperature) || config.target_temperature < 10.0f || config.target_temperature > 30.0f) {
            config.target_temperature = 22.0f;
        }
        if (std::isnan(config.elevation) || config.elevation < -500.0f || config.elevation > 9000.0f) {
            config.elevation = 0.0f;
        }
        if (config.sensor_i2c_address < MIN_SENSOR_I2C_ADDRESS || config.sensor_i2c_address > MAX_SENSOR_I2C_ADDRESS) {
            config.sensor_i2c_address = DEFAULT_SENSOR_I2C_ADDRESS;
        }
    }

    DeviceConfig ConfigManager::loadDeviceConfig() {
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, true); // Read-only mode

        String deviceId = getDeviceId();
        strlcpy(deviceConfig.device_id, deviceId.c_str(), sizeof(deviceConfig.device_id));

        // Load device name (use device ID as fallback)
        String deviceName = guard.get().getString("device_name", "");
        if (deviceName.length() > 0) {
            strlcpy(deviceConfig.device_name, deviceName.c_str(), sizeof(deviceConfig.device_name));
        } else {
            strlcpy(deviceConfig.device_name, deviceConfig.device_id, sizeof(deviceConfig.device_name));
        }

        // Load other device settings
        deviceConfig.target_temperature = guard.get().getFloat(TARGET_TEMPERATURE, 22.0f);
        deviceConfig.temperature_control_enabled = guard.get().getBool(TEMPERATURE_CONTROL_ENABLED, false);
        deviceConfig.elevation = guard.get().getFloat(ELEVATION, 0.0f);
        deviceConfig.sensor_i2c_address = guard.get().getUChar(SENSOR_I2C_ADDRESS, DEFAULT_SENSOR_I2C_ADDRESS);
#endif

        // Validate ranges — NVS may hold garbage after flash corruption
        validateDeviceConfig(deviceConfig);

        return deviceConfig;
    }

    void ConfigManager::saveDeviceConfig([[maybe_unused]] const DeviceConfig &config) {
        // Validate before persisting to keep NVS consistent
        DeviceConfig validated = config;
        validateDeviceConfig(validated);

#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false); // Read-write mode
        guard.get().putString("device_name", validated.device_name);
        guard.get().putFloat(TARGET_TEMPERATURE, validated.target_temperature);
        guard.get().putBool(TEMPERATURE_CONTROL_ENABLED, validated.temperature_control_enabled);
        guard.get().putFloat(ELEVATION, validated.elevation);
        guard.get().putUChar(SENSOR_I2C_ADDRESS, validated.sensor_i2c_address);
#endif

        // Also update in-memory cache
        deviceConfig = validated;
    }

    void ConfigManager::updateDeviceName([[maybe_unused]] const char* device_name) {
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false);
        guard.get().putString("device_name", device_name);
#endif
        strlcpy(deviceConfig.device_name, device_name, sizeof(deviceConfig.device_name));
    }

    void ConfigManager::updateTargetTemperature([[maybe_unused]] float temperature) {
        // Validate — same logic as validateDeviceConfig()
        if (std::isnan(temperature) || temperature < 10.0f || temperature > 30.0f) {
            temperature = 22.0f;
        }
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false);
        guard.get().putFloat(TARGET_TEMPERATURE, temperature);
#endif
        deviceConfig.target_temperature = temperature;
    }

    void ConfigManager::updateTemperatureControlEnabled([[maybe_unused]] bool enabled) {
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false);
        guard.get().putBool(TEMPERATURE_CONTROL_ENABLED, enabled);
#endif
        deviceConfig.temperature_control_enabled = enabled;
    }

    void ConfigManager::updateElevation([[maybe_unused]] float elevation) {
        // Validate — same logic as validateDeviceConfig()
        if (std::isnan(elevation) || elevation < -500.0f || elevation > 9000.0f) {
            elevation = 0.0f;
        }
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false);
        guard.get().putFloat(ELEVATION, elevation);
#endif
        deviceConfig.elevation = elevation;
    }

    void ConfigManager::updateSensorI2CAddress(uint8_t address) {
        // Validate: valid 7-bit I2C addresses are 0x08-0x77 (reserved: 0x00-0x07, 0x78-0x7F)
        if (address < MIN_SENSOR_I2C_ADDRESS || address > MAX_SENSOR_I2C_ADDRESS) {
            address = DEFAULT_SENSOR_I2C_ADDRESS;
        }
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false);
        guard.get().putUChar(SENSOR_I2C_ADDRESS, address);
#endif
        deviceConfig.sensor_i2c_address = address;
    }

    void ConfigManager::reset() {
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false); // Read-write mode
        guard.get().clear(); // Clear all keys in this namespace
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
        return String("000000");
#endif
    }

    uint8_t ConfigManager::incrementConnectionFailures() {
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false); // Read-write mode
        uint8_t failures = guard.get().getUChar("wifi_failures", 0);
        failures++;
        guard.get().putUChar("wifi_failures", failures);
        return failures;
#else
        return 0;
#endif
    }

    void ConfigManager::resetConnectionFailures() {
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false); // Read-write mode
        guard.get().putUChar("wifi_failures", 0);
#endif
    }

    uint8_t ConfigManager::getConnectionFailures() {
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, true); // Read-only mode
        return guard.get().getUChar("wifi_failures", 0);
#else
        return 0;
#endif
    }

    SensorConfig ConfigManager::loadSensorConfig() {
        SensorConfig sensorConfig;

#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, true); // Read-only mode

        String assign = guard.get().getString("sns_assign", "");

        strlcpy(sensorConfig.assignments, assign.c_str(), sizeof(sensorConfig.assignments));

        ESP_LOGD(TAG, "SensorConfig: Loaded assignments='%s'", sensorConfig.assignments);
#endif

        return sensorConfig;
    }

    void ConfigManager::saveSensorConfig([[maybe_unused]] const SensorConfig &config) {
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false); // Read-write mode

        guard.get().putString("sns_assign", config.assignments);

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
        PreferencesGuard guard(prefs, NAMESPACE, true); // Read-only mode

        mqttConfig.enabled = guard.get().getBool("mqtt_enabled", false);
        guard.get().getString("mqtt_host", mqttConfig.host, sizeof(mqttConfig.host));
        mqttConfig.port = guard.get().getUShort("mqtt_port", 1883);
        guard.get().getString("mqtt_user", mqttConfig.username, sizeof(mqttConfig.username));
        guard.get().getString("mqtt_pass", mqttConfig.password, sizeof(mqttConfig.password));
        guard.get().getString("mqtt_prefix", mqttConfig.prefix, sizeof(mqttConfig.prefix));
        mqttConfig.interval = guard.get().getUShort("mqtt_interval", 15);
#endif

        // Validate ranges — NVS may hold garbage after flash corruption
        validateMqttConfig(mqttConfig);

        return mqttConfig;
    }

    void ConfigManager::saveMqttConfig([[maybe_unused]] const MqttConfig &config) {
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false); // Read-write mode

        guard.get().putBool("mqtt_enabled", config.enabled);
        guard.get().putString("mqtt_host", config.host);
        guard.get().putUShort("mqtt_port", config.port);
        guard.get().putString("mqtt_user", config.username);
        guard.get().putString("mqtt_pass", config.password);
        guard.get().putString("mqtt_prefix", config.prefix);
        guard.get().putUShort("mqtt_interval", config.interval);

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
        PreferencesGuard guard(prefs, NAMESPACE, true);

        energyConfig.wifi_power = guard.get().getUChar(ENERGY_WIFI_PW, Constants::DEFAULT_WIFI_POWER);
#endif

        // Validate wifi_power is one of the known values
        validateEnergyConfig(energyConfig);

        return energyConfig;
    }

    void ConfigManager::saveEnergyConfig([[maybe_unused]] const EnergyConfig &config) {
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false);

        guard.get().putUChar(ENERGY_WIFI_PW, config.wifi_power);

        ESP_LOGD(TAG, "Saved energy configuration");
#endif
    }
    SyslogConfig ConfigManager::loadSyslogConfig() {
        SyslogConfig config;
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, true);

        config.enabled = guard.get().getBool("syslog_on", false);
        config.port = guard.get().getUShort("syslog_port", 514);
        guard.get().getString("syslog_host", config.host, sizeof(config.host));
#endif
        return config;
    }

    void ConfigManager::saveSyslogConfig([[maybe_unused]] const SyslogConfig &config) {
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false);

        guard.get().putBool("syslog_on", config.enabled);
        guard.get().putUShort("syslog_port", config.port);
        guard.get().putString("syslog_host", config.host);

        ESP_LOGD(TAG, "Saved syslog configuration");
#endif
    }
} // namespace Config
