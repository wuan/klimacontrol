#include "Config.h"
#include <cmath>

#ifdef ARDUINO
#include <esp_system.h>
#include "Log.h"
#endif

static const char* TAG = "config";

namespace Config {
    ConfigManager::ConfigManager() {
    }

    void ConfigManager::requestRestart(uint32_t delayMs) {
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
        
        config.target_temperature = prefs.getFloat(TARGET_TEMPERATURE, 22.0f);
        config.temperature_control_enabled = prefs.getBool(TEMPERATURE_CONTROL_ENABLED, false);
        config.elevation = prefs.getFloat(ELEVATION, 0.0f);

        prefs.end();

        // Validate ranges — NVS may hold garbage after flash corruption
        if (std::isnan(config.target_temperature) || config.target_temperature < 10.0f || config.target_temperature > 30.0f) {
            config.target_temperature = 22.0f;
        }
        if (std::isnan(config.elevation) || config.elevation < -500.0f || config.elevation > 9000.0f) {
            config.elevation = 0.0f;
        }

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

        prefs.putFloat(TARGET_TEMPERATURE, config.target_temperature);
        prefs.putBool(TEMPERATURE_CONTROL_ENABLED, config.temperature_control_enabled);
        prefs.putFloat(ELEVATION, config.elevation);

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

    SensorConfig ConfigManager::loadSensorConfig() {
        SensorConfig sensorConfig;

#ifdef ARDUINO
        prefs.begin(NAMESPACE, true); // Read-only mode

        String assign = prefs.getString("sns_assign", "");

        strncpy(sensorConfig.assignments, assign.c_str(), sizeof(sensorConfig.assignments) - 1);
        sensorConfig.assignments[sizeof(sensorConfig.assignments) - 1] = '\0';

        prefs.end();

        ESP_LOGD(TAG, "SensorConfig: Loaded assignments='%s'", sensorConfig.assignments);
#endif

        return sensorConfig;
    }

    void ConfigManager::saveSensorConfig(const SensorConfig &config) {
#ifdef ARDUINO
        prefs.begin(NAMESPACE, false); // Read-write mode

        prefs.putString("sns_assign", config.assignments);

        prefs.end();

        ESP_LOGD(TAG, "SensorConfig: Saved assignments='%s'", config.assignments);
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

        // Validate ranges — NVS may hold garbage after flash corruption
        if (mqttConfig.port == 0) {
            mqttConfig.port = 1883;
        }
        if (mqttConfig.interval < 1 || mqttConfig.interval > 3600) {
            mqttConfig.interval = 15;
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

        ESP_LOGD(TAG, "Saved MQTT configuration");
#endif
    }
    EnergyConfig ConfigManager::loadEnergyConfig() {
        EnergyConfig energyConfig;

#ifdef ARDUINO
        prefs.begin(NAMESPACE, true);

        energyConfig.wifi_power = prefs.getUChar(ENERGY_WIFI_PW, Constants::DEFAULT_WIFI_POWER);

        prefs.end();

        // Validate wifi_power is one of the known values
        uint8_t wp = energyConfig.wifi_power;
        if (wp != 8 && wp != 34 && wp != 52 && wp != 68 && wp != 80) {
            energyConfig.wifi_power = Constants::DEFAULT_WIFI_POWER;
        }
#endif

        return energyConfig;
    }

    void ConfigManager::saveEnergyConfig(const EnergyConfig &config) {
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

    void ConfigManager::saveSyslogConfig(const SyslogConfig &config) {
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
