#include "Config.h"
#include "support/LocalTime.h"
#include "PrefsKeys.h"
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
        const uint64_t deadline = static_cast<uint64_t>(millis()) + delayMs;
        lockRestart();
        restartState = packRestartState(true, deadline);
        unlockRestart();
        ESP_LOGI(TAG, "Restart requested in %u ms", delayMs);
#endif
    }

    void ConfigManager::checkRestart() {
#ifdef ARDUINO
        lockRestart();
        const uint64_t state = restartState;
        unlockRestart();
        if (!isRequestedOf(state)) return;
        const uint32_t deadline = static_cast<uint32_t>(deadlineOf(state));
        // Wrap-safe deadline comparison: signed difference handles the case where
        // deadline = millis() + delayMs has wrapped past UINT32_MAX while millis()
        // has not yet. Plain `millis() >= deadline` would fail to fire for ~49 days
        // after the wrap.
        if (static_cast<int32_t>(millis() - deadline) >= 0) {
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
        PreferencesGuard guard(prefs, PrefsKeys::NAMESPACE, true); // Read-only mode

        config.configured = guard.get().getBool(PrefsKeys::WIFI_CONFIGURED, false);
        config.connection_failures = guard.get().getUChar(PrefsKeys::WIFI_FAILURES, 0);

        if (config.configured) {
            guard.get().getString(PrefsKeys::WIFI_SSID, config.ssid, sizeof(config.ssid));
            guard.get().getString(PrefsKeys::WIFI_PASS, config.password, sizeof(config.password));
        }
#endif

        return config;
    }

    void ConfigManager::saveWiFiConfig([[maybe_unused]] const WiFiConfig &config) {
#ifdef ARDUINO
        PreferencesGuard guard(prefs, PrefsKeys::NAMESPACE, false); // Read-write mode

        guard.get().putString(PrefsKeys::WIFI_SSID, config.ssid);
        guard.get().putString(PrefsKeys::WIFI_PASS, config.password);
        guard.get().putBool(PrefsKeys::WIFI_CONFIGURED, config.configured);
#endif
    }

    void validateDeviceConfig(DeviceConfig &config) {
        if (std::isnan(config.target_temperature) ||
            config.target_temperature < TARGET_TEMPERATURE_MIN_C ||
            config.target_temperature > TARGET_TEMPERATURE_MAX_C) {
            config.target_temperature = TARGET_TEMPERATURE_DEFAULT_C;
        }
        if (std::isnan(config.elevation) || config.elevation < -500.0f || config.elevation > 9000.0f) {
            config.elevation = 0.0f;
        }

        // An empty or corrupted timezone degrades to UTC rather than to
        // undefined tzset() behaviour.
        if (!Support::isPlausibleTimezone(config.timezone)) {
            strlcpy(config.timezone, Support::DEFAULT_TIMEZONE, sizeof(config.timezone));
        }
        if (config.sensor_i2c_address < MIN_SENSOR_I2C_ADDRESS || config.sensor_i2c_address > MAX_SENSOR_I2C_ADDRESS) {
            config.sensor_i2c_address = DEFAULT_SENSOR_I2C_ADDRESS;
        }

        if (config.actuator_channel < 0 ||
            config.actuator_channel > static_cast<int8_t>(MAX_ACTUATOR_CHANNEL)) {
            config.actuator_channel = ACTUATOR_CHANNEL_UNASSIGNED;
        }

        // The cycle and the travel time are only meaningful as a pair: a cycle
        // that cannot fit several full strokes reduces the controller to
        // bang-bang without saying so. If either is out of range, or the pair
        // is inconsistent, both go back to defaults rather than leaving a
        // half-trusted combination in force.
        const bool travelSane = config.tpo_travel_s >= MIN_TPO_TRAVEL_S &&
                                config.tpo_travel_s <= MAX_TPO_TRAVEL_S;
        const bool cycleSane = config.tpo_cycle_s >= MIN_TPO_CYCLE_S &&
                               config.tpo_cycle_s <= MAX_TPO_CYCLE_S;
        const bool pairSane =
            travelSane && cycleSane &&
            config.tpo_cycle_s >= config.tpo_travel_s * TPO_MIN_STROKES_PER_CYCLE;
        if (!pairSane) {
            config.tpo_cycle_s = DEFAULT_TPO_CYCLE_S;
            config.tpo_travel_s = DEFAULT_TPO_TRAVEL_S;
        }

        if (std::isnan(config.safety_max_c) || config.safety_max_c < MIN_SAFETY_MAX_C ||
            config.safety_max_c > MAX_SAFETY_MAX_C) {
            config.safety_max_c = DEFAULT_SAFETY_MAX_C;
        }
        if (std::isnan(config.safety_hyst_c) || config.safety_hyst_c <= 0.0f ||
            config.safety_hyst_c > 10.0f) {
            config.safety_hyst_c = DEFAULT_SAFETY_HYST_C;
        }

        // Gains fall back per field rather than as a set: a partial NVS
        // corruption should cost the one field that was corrupted, not the
        // three that survived it. `isfinite` rather than `isnan` because an
        // infinity is equally untrustworthy and would pass a bare range check
        // on only one side. Zero `kp` is out of range by construction
        // (MIN_PID_KP is positive), so it falls back like any other bad value.
        if (!std::isfinite(config.kp) || config.kp < MIN_PID_KP || config.kp > MAX_PID_KP) {
            config.kp = DEFAULT_PID_KP;
        }
        if (!std::isfinite(config.ki) || config.ki < MIN_PID_KI || config.ki > MAX_PID_KI) {
            config.ki = DEFAULT_PID_KI;
        }
        if (!std::isfinite(config.kd) || config.kd < MIN_PID_KD || config.kd > MAX_PID_KD) {
            config.kd = DEFAULT_PID_KD;
        }
        if (config.control_interval_s < MIN_CONTROL_INTERVAL_S ||
            config.control_interval_s > MAX_CONTROL_INTERVAL_S) {
            config.control_interval_s = DEFAULT_CONTROL_INTERVAL_S;
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
        deviceConfig.target_temperature =
            guard.get().getFloat(TARGET_TEMPERATURE, TARGET_TEMPERATURE_DEFAULT_C);
        deviceConfig.temperature_control_enabled = guard.get().getBool(TEMPERATURE_CONTROL_ENABLED, false);
        deviceConfig.elevation = guard.get().getFloat(ELEVATION, 0.0f);
        guard.get().getString(TIMEZONE, deviceConfig.timezone, sizeof(deviceConfig.timezone));
        deviceConfig.sensor_i2c_address = guard.get().getUChar(SENSOR_I2C_ADDRESS, DEFAULT_SENSOR_I2C_ADDRESS);
        guard.get().getString(ACTUATOR_HOST, deviceConfig.actuator_host,
                              sizeof(deviceConfig.actuator_host));
        deviceConfig.actuator_channel =
            static_cast<int8_t>(guard.get().getChar(ACTUATOR_CHANNEL, ACTUATOR_CHANNEL_UNASSIGNED));
        deviceConfig.tpo_cycle_s = guard.get().getUShort(TPO_CYCLE, DEFAULT_TPO_CYCLE_S);
        deviceConfig.tpo_travel_s = guard.get().getUShort(TPO_TRAVEL, DEFAULT_TPO_TRAVEL_S);
        deviceConfig.safety_max_c = guard.get().getFloat(SAFETY_MAX, DEFAULT_SAFETY_MAX_C);
        deviceConfig.safety_hyst_c = guard.get().getFloat(SAFETY_HYST, DEFAULT_SAFETY_HYST_C);
        deviceConfig.kp = guard.get().getFloat(PID_KP, DEFAULT_PID_KP);
        deviceConfig.ki = guard.get().getFloat(PID_KI, DEFAULT_PID_KI);
        deviceConfig.kd = guard.get().getFloat(PID_KD, DEFAULT_PID_KD);
        deviceConfig.control_interval_s =
            guard.get().getUShort(CONTROL_INTERVAL, DEFAULT_CONTROL_INTERVAL_S);
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
        guard.get().putString(TIMEZONE, validated.timezone);
        guard.get().putUChar(SENSOR_I2C_ADDRESS, validated.sensor_i2c_address);
        guard.get().putString(ACTUATOR_HOST, validated.actuator_host);
        guard.get().putChar(ACTUATOR_CHANNEL, static_cast<int8_t>(validated.actuator_channel));
        guard.get().putUShort(TPO_CYCLE, validated.tpo_cycle_s);
        guard.get().putUShort(TPO_TRAVEL, validated.tpo_travel_s);
        guard.get().putFloat(SAFETY_MAX, validated.safety_max_c);
        guard.get().putFloat(SAFETY_HYST, validated.safety_hyst_c);
        guard.get().putFloat(PID_KP, validated.kp);
        guard.get().putFloat(PID_KI, validated.ki);
        guard.get().putFloat(PID_KD, validated.kd);
        guard.get().putUShort(CONTROL_INTERVAL, validated.control_interval_s);
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
        // Guards against a corrupt or absent NVS value, not against user input:
        // out of range here means "the stored setpoint is not trustworthy", and
        // the honest response is the documented default rather than whichever
        // bound happens to be nearer. User-supplied setpoints are rejected
        // outright by the route handler and never reach this fallback.
        // Validate — same logic as validateDeviceConfig()
        if (std::isnan(temperature) || temperature < TARGET_TEMPERATURE_MIN_C ||
            temperature > TARGET_TEMPERATURE_MAX_C) {
            temperature = TARGET_TEMPERATURE_DEFAULT_C;
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

    void ConfigManager::updateActuatorAssignment([[maybe_unused]] const char *actuatorHost,
                                                 [[maybe_unused]] int8_t actuatorChannel) {
        char hostBuf[sizeof(deviceConfig.actuator_host)] = "";
        int8_t ch = ACTUATOR_CHANNEL_UNASSIGNED;
        if (actuatorHost != nullptr && actuatorHost[0] != '\0' && actuatorChannel >= 0 &&
            actuatorChannel <= static_cast<int8_t>(MAX_ACTUATOR_CHANNEL)) {
            strlcpy(hostBuf, actuatorHost, sizeof(hostBuf));
            ch = actuatorChannel;
        }
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false);
        guard.get().putString(ACTUATOR_HOST, hostBuf);
        guard.get().putChar(ACTUATOR_CHANNEL, ch);
#endif
        strlcpy(deviceConfig.actuator_host, hostBuf, sizeof(deviceConfig.actuator_host));
        deviceConfig.actuator_channel = ch;
    }

    void ConfigManager::updateActuatorTiming([[maybe_unused]] uint16_t cycleS,
                                             [[maybe_unused]] uint16_t travelS,
                                             [[maybe_unused]] float safetyMaxC,
                                             [[maybe_unused]] float safetyHystC) {
        DeviceConfig candidate = deviceConfig;
        candidate.tpo_cycle_s = cycleS;
        candidate.tpo_travel_s = travelS;
        candidate.safety_max_c = safetyMaxC;
        candidate.safety_hyst_c = safetyHystC;
        // Reuse the load-time validator so a stored value can never be one the
        // firmware would have rejected on the way in.
        validateDeviceConfig(candidate);

#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false);
        guard.get().putUShort(TPO_CYCLE, candidate.tpo_cycle_s);
        guard.get().putUShort(TPO_TRAVEL, candidate.tpo_travel_s);
        guard.get().putFloat(SAFETY_MAX, candidate.safety_max_c);
        guard.get().putFloat(SAFETY_HYST, candidate.safety_hyst_c);
#endif
        deviceConfig.tpo_cycle_s = candidate.tpo_cycle_s;
        deviceConfig.tpo_travel_s = candidate.tpo_travel_s;
        deviceConfig.safety_max_c = candidate.safety_max_c;
        deviceConfig.safety_hyst_c = candidate.safety_hyst_c;
    }

    void ConfigManager::updateTuning([[maybe_unused]] float kp, [[maybe_unused]] float ki,
                                     [[maybe_unused]] float kd,
                                     [[maybe_unused]] uint16_t intervalS) {
        DeviceConfig candidate = deviceConfig;
        candidate.kp = kp;
        candidate.ki = ki;
        candidate.kd = kd;
        candidate.control_interval_s = intervalS;
        // Reuse the load-time validator so a stored value can never be one the
        // firmware would have rejected on the way in.
        validateDeviceConfig(candidate);

#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false);
        guard.get().putFloat(PID_KP, candidate.kp);
        guard.get().putFloat(PID_KI, candidate.ki);
        guard.get().putFloat(PID_KD, candidate.kd);
        guard.get().putUShort(CONTROL_INTERVAL, candidate.control_interval_s);
#endif
        deviceConfig.kp = candidate.kp;
        deviceConfig.ki = candidate.ki;
        deviceConfig.kd = candidate.kd;
        deviceConfig.control_interval_s = candidate.control_interval_s;
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

    void ConfigManager::updateTimezone([[maybe_unused]] const char* timezone) {
        // Validate — same logic as validateDeviceConfig()
        char validated[sizeof(deviceConfig.timezone)];
        if (Support::isPlausibleTimezone(timezone)) {
            strlcpy(validated, timezone, sizeof(validated));
        } else {
            strlcpy(validated, Support::DEFAULT_TIMEZONE, sizeof(validated));
        }
#ifdef ARDUINO
        PreferencesGuard guard(prefs, NAMESPACE, false);
        guard.get().putString(TIMEZONE, validated);
#endif
        strlcpy(deviceConfig.timezone, validated, sizeof(deviceConfig.timezone));
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
        // Validate wifi_sleep_mode: 0=NONE, 1=MIN_MODEM, 2=MAX_MODEM
        if (config.wifi_sleep_mode > 2) {
            config.wifi_sleep_mode = 0; // Default to WIFI_PS_NONE
        }
    }

    EnergyConfig ConfigManager::loadEnergyConfig() {
        EnergyConfig energyConfig;

#ifdef ARDUINO
        PreferencesGuard guard(prefs, PrefsKeys::NAMESPACE, true);

        energyConfig.wifi_power = guard.get().getUChar(PrefsKeys::ENERGY_WIFI_POWER, Constants::DEFAULT_WIFI_POWER);
        energyConfig.wifi_sleep_mode = guard.get().getUChar(PrefsKeys::ENERGY_WIFI_SLEEP_MODE, 0);

        ESP_LOGD(TAG, "Loaded energy config from NVS: power=%u, sleep=%u",
                 energyConfig.wifi_power, energyConfig.wifi_sleep_mode);
#endif

        // Validate configuration values
        validateEnergyConfig(energyConfig);

        return energyConfig;
    }

    void ConfigManager::saveEnergyConfig([[maybe_unused]] const EnergyConfig &config) {
        // Validate before persisting to keep NVS consistent
        EnergyConfig validated = config;
        validateEnergyConfig(validated);

#ifdef ARDUINO
        PreferencesGuard guard(prefs, PrefsKeys::NAMESPACE, false);

        ESP_LOGD(TAG, "Saving energy config: power=%u, sleep=%u", validated.wifi_power, validated.wifi_sleep_mode);
        guard.get().putUChar(PrefsKeys::ENERGY_WIFI_POWER, validated.wifi_power);
        guard.get().putUChar(PrefsKeys::ENERGY_WIFI_SLEEP_MODE, validated.wifi_sleep_mode);
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

    void validateDisplayConfig(DisplayConfig &config) {
        if (config.rotation > MAX_DISPLAY_ROTATION) {
            config.rotation = 0;
        }
        if (config.interval < MIN_DISPLAY_INTERVAL) {
            config.interval = MIN_DISPLAY_INTERVAL;
        } else if (config.interval > MAX_DISPLAY_INTERVAL) {
            config.interval = MAX_DISPLAY_INTERVAL;
        }
    }

    DisplayConfig ConfigManager::loadDisplayConfig() {
        DisplayConfig displayConfig;

#ifdef ARDUINO
        PreferencesGuard guard(prefs, PrefsKeys::NAMESPACE, true);

        displayConfig.enabled = guard.get().getBool(PrefsKeys::DISPLAY_ENABLED, false);
        displayConfig.rotation = guard.get().getUChar(PrefsKeys::DISPLAY_ROTATION, 0);
        displayConfig.interval = guard.get().getUShort(PrefsKeys::DISPLAY_INTERVAL, DEFAULT_DISPLAY_INTERVAL);

        ESP_LOGD(TAG, "Loaded display config from NVS: enabled=%d rotation=%u interval=%u",
                 displayConfig.enabled, displayConfig.rotation, displayConfig.interval);
#endif

        // Validate ranges — NVS may hold garbage after flash corruption
        validateDisplayConfig(displayConfig);

        return displayConfig;
    }

    void ConfigManager::saveDisplayConfig([[maybe_unused]] const DisplayConfig &config) {
        // Validate before persisting to keep NVS consistent
        DisplayConfig validated = config;
        validateDisplayConfig(validated);

#ifdef ARDUINO
        PreferencesGuard guard(prefs, PrefsKeys::NAMESPACE, false);

        guard.get().putBool(PrefsKeys::DISPLAY_ENABLED, validated.enabled);
        guard.get().putUChar(PrefsKeys::DISPLAY_ROTATION, validated.rotation);
        guard.get().putUShort(PrefsKeys::DISPLAY_INTERVAL, validated.interval);

        ESP_LOGD(TAG, "Saved display configuration: enabled=%d rotation=%u interval=%u",
                 validated.enabled, validated.rotation, validated.interval);
#endif
    }
} // namespace Config
