#ifndef KLIMACONTROL_PREFS_KEYS_H
#define KLIMACONTROL_PREFS_KEYS_H

/**
 * Global NVS/Preferences key constants
 * Used for persistent storage in ESP32 flash
 *
 * IMPORTANT: Keep key names concise (ideally ≤12 characters).
 * Long key names (17+ characters) cause NVS persistence issues
 * regardless of data type. This is an ESP32 NVS limitation.
 */
namespace PrefsKeys {
    // NVS namespace
    constexpr const char* NAMESPACE = "klima";

    // WiFi configuration
    constexpr const char* WIFI_SSID = "wifi_ssid";
    constexpr const char* WIFI_PASS = "wifi_pass";
    constexpr const char* WIFI_CONFIGURED = "configured";
    constexpr const char* WIFI_FAILURES = "wifi_failures";

    // Device configuration
    constexpr const char* DEVICE_NAME = "device_name";
    constexpr const char* TARGET_TEMPERATURE = "target_temperature";
    constexpr const char* TEMPERATURE_CONTROL_ENABLED = "temperature_control_enabled";
    constexpr const char* ELEVATION = "elevation";
    constexpr const char* SENSOR_I2C_ADDRESS = "sensor_i2c_address";

    // Energy configuration
    constexpr const char* ENERGY_WIFI_POWER = "energy_wifi_pw";
    constexpr const char* ENERGY_WIFI_SLEEP_MODE = "wifi_sleep";  // Concise key name for NVS reliability

    // Sensor configuration
    constexpr const char* SENSOR_ASSIGNMENTS = "sns_assign";

    // MQTT configuration
    constexpr const char* MQTT_ENABLED = "mqtt_enabled";
    constexpr const char* MQTT_HOST = "mqtt_host";
    constexpr const char* MQTT_PORT = "mqtt_port";
    constexpr const char* MQTT_USERNAME = "mqtt_user";
    constexpr const char* MQTT_PASSWORD = "mqtt_pass";
    constexpr const char* MQTT_PREFIX = "mqtt_prefix";
    constexpr const char* MQTT_INTERVAL = "mqtt_interval";

    // Syslog configuration
    constexpr const char* SYSLOG_ENABLED = "syslog_enabled";
    constexpr const char* SYSLOG_HOST = "syslog_host";
    constexpr const char* SYSLOG_PORT = "syslog_port";
}

#endif // KLIMACONTROL_PREFS_KEYS_H
