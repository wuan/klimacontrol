#ifndef KLIMACONTROL_PREFS_KEYS_H
#define KLIMACONTROL_PREFS_KEYS_H

/**
 * Global NVS/Preferences key constants
 * Used for persistent storage in ESP32 flash
 *
 * IMPORTANT: NVS keys are hard-limited to 15 characters
 * (NVS_KEY_NAME_MAX_SIZE is 16 including the terminator). A longer key makes
 * Preferences::putX() fail silently, and the matching getX() then returns the
 * supplied default — so the setting looks like it simply refuses to stick, with
 * no error anywhere. Config::nvsKeyFits() enforces this at compile time for the
 * ConfigManager key set; keep any key added here within the same limit.
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
    constexpr const char* TARGET_TEMPERATURE = "target_temp";
    constexpr const char* TEMPERATURE_CONTROL_ENABLED = "ctrl_enabled";
    constexpr const char* ELEVATION = "elevation";
    constexpr const char* SENSOR_I2C_ADDRESS = "sensor_i2c";
    constexpr const char* TIMEZONE = "timezone";

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

    // Display configuration
    // NOTE: "disp_interval" would be 13 characters and hit the NVS limit noted
    // above, so the interval is stored under the abbreviated "disp_intv".
    constexpr const char* DISPLAY_ENABLED = "disp_enabled";
    constexpr const char* DISPLAY_ROTATION = "disp_rot";
    constexpr const char* DISPLAY_INTERVAL = "disp_intv";

    // Syslog configuration
    constexpr const char* SYSLOG_ENABLED = "syslog_enabled";
    constexpr const char* SYSLOG_HOST = "syslog_host";
    constexpr const char* SYSLOG_PORT = "syslog_port";

    // Enforce the 15-character NVS key limit at compile time, matching the
    // discipline in Config.h. NVS keys longer than this fail silently in
    // Preferences::putX(); a future careless rename would otherwise re-introduce
    // the syslog "key mismatch" bug fixed alongside this assertion. Requires
    // Config.h to be included first so Config::nvsKeyFits is visible.
    static_assert(Config::nvsKeyFits(SYSLOG_ENABLED), "NVS key too long");
    static_assert(Config::nvsKeyFits(SYSLOG_HOST), "NVS key too long");
    static_assert(Config::nvsKeyFits(SYSLOG_PORT), "NVS key too long");
}

#endif // KLIMACONTROL_PREFS_KEYS_H
