#include "unity.h"
#include "Config.h"
#include <cmath>
#include <cstring>

void setUp() {}
void tearDown() {}

// --- WiFiConfig defaults ---

void test_wifi_config_defaults() {
    Config::WiFiConfig config;
    TEST_ASSERT_FALSE(config.configured);
    TEST_ASSERT_EQUAL(0, config.connection_failures);
    TEST_ASSERT_EQUAL_STRING("", config.ssid);
    TEST_ASSERT_EQUAL_STRING("", config.password);
}

// --- DeviceConfig defaults ---

void test_device_config_defaults() {
    Config::DeviceConfig config;
    TEST_ASSERT_EQUAL(Config::DEFAULT_SENSOR_I2C_ADDRESS, config.sensor_i2c_address);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.0f, config.target_temperature);
    TEST_ASSERT_FALSE(config.temperature_control_enabled);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, config.elevation);
}

// --- MqttConfig defaults ---

void test_mqtt_config_defaults() {
    Config::MqttConfig config;
    TEST_ASSERT_EQUAL(1883, config.port);
    TEST_ASSERT_EQUAL(15, config.interval);
    TEST_ASSERT_FALSE(config.enabled);
    TEST_ASSERT_EQUAL_STRING("sensors", config.prefix);
    TEST_ASSERT_EQUAL_STRING("", config.host);
    TEST_ASSERT_EQUAL_STRING("", config.username);
    TEST_ASSERT_EQUAL_STRING("", config.password);
}

// --- SensorConfig defaults ---

void test_sensor_config_defaults() {
    Config::SensorConfig config;
    TEST_ASSERT_EQUAL_STRING("", config.assignments);
}

// --- EnergyConfig defaults ---

void test_energy_config_defaults() {
    Config::EnergyConfig config;
    TEST_ASSERT_EQUAL(Constants::DEFAULT_WIFI_POWER, config.wifi_power);
}

// --- SyslogConfig defaults ---

void test_syslog_config_defaults() {
    Config::SyslogConfig config;
    TEST_ASSERT_EQUAL(514, config.port);
    TEST_ASSERT_FALSE(config.enabled);
    TEST_ASSERT_EQUAL_STRING("", config.host);
}

void test_display_config_defaults() {
    Config::DisplayConfig config;
    TEST_ASSERT_FALSE(config.enabled);
    TEST_ASSERT_EQUAL(0, config.rotation);
    TEST_ASSERT_EQUAL(60, config.interval);
}

void test_device_config_timezone_default() {
    Config::DeviceConfig config;
    TEST_ASSERT_EQUAL_STRING("UTC0", config.timezone);
}

void test_validate_device_config_empty_timezone_reset() {
    Config::DeviceConfig config;
    config.timezone[0] = '\0';
    Config::validateDeviceConfig(config);
    TEST_ASSERT_EQUAL_STRING("UTC0", config.timezone);
}

void test_validate_device_config_nonprintable_timezone_reset() {
    Config::DeviceConfig config;
    strcpy(config.timezone, "CET\t-1");
    Config::validateDeviceConfig(config);
    TEST_ASSERT_EQUAL_STRING("UTC0", config.timezone);
}

void test_validate_device_config_valid_timezone_preserved() {
    Config::DeviceConfig config;
    strcpy(config.timezone, "CET-1CEST,M3.5.0,M10.5.0/3");
    Config::validateDeviceConfig(config);
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", config.timezone);
}

// --- validateDisplayConfig ---

void test_validate_display_config_defaults_unchanged() {
    Config::DisplayConfig config;
    Config::validateDisplayConfig(config);
    TEST_ASSERT_FALSE(config.enabled);
    TEST_ASSERT_EQUAL(0, config.rotation);
    TEST_ASSERT_EQUAL(60, config.interval);
}

void test_validate_display_config_rotation_valid_unchanged() {
    for (uint8_t r = 0; r <= 3; r++) {
        Config::DisplayConfig config;
        config.rotation = r;
        Config::validateDisplayConfig(config);
        TEST_ASSERT_EQUAL(r, config.rotation);
    }
}

void test_validate_display_config_rotation_too_high_reset() {
    Config::DisplayConfig config;
    config.rotation = 4;
    Config::validateDisplayConfig(config);
    TEST_ASSERT_EQUAL(0, config.rotation);

    config.rotation = 255;
    Config::validateDisplayConfig(config);
    TEST_ASSERT_EQUAL(0, config.rotation);
}

void test_validate_display_config_interval_below_floor_clamped() {
    Config::DisplayConfig config;
    config.interval = 0;
    Config::validateDisplayConfig(config);
    TEST_ASSERT_EQUAL(Config::MIN_DISPLAY_INTERVAL, config.interval);

    config.interval = 9;
    Config::validateDisplayConfig(config);
    TEST_ASSERT_EQUAL(Config::MIN_DISPLAY_INTERVAL, config.interval);
}

void test_validate_display_config_interval_at_bounds_unchanged() {
    Config::DisplayConfig config;
    config.interval = Config::MIN_DISPLAY_INTERVAL;
    Config::validateDisplayConfig(config);
    TEST_ASSERT_EQUAL(Config::MIN_DISPLAY_INTERVAL, config.interval);

    config.interval = 60;
    Config::validateDisplayConfig(config);
    TEST_ASSERT_EQUAL(60, config.interval);

    config.interval = Config::MAX_DISPLAY_INTERVAL;
    Config::validateDisplayConfig(config);
    TEST_ASSERT_EQUAL(Config::MAX_DISPLAY_INTERVAL, config.interval);
}

void test_validate_display_config_interval_above_ceiling_clamped() {
    Config::DisplayConfig config;
    config.interval = 3601;
    Config::validateDisplayConfig(config);
    TEST_ASSERT_EQUAL(Config::MAX_DISPLAY_INTERVAL, config.interval);

    config.interval = 65535;
    Config::validateDisplayConfig(config);
    TEST_ASSERT_EQUAL(Config::MAX_DISPLAY_INTERVAL, config.interval);
}

void test_validate_display_config_preserves_enabled() {
    // Validation clamps ranges only; it must never flip the enable flag.
    Config::DisplayConfig config;
    config.enabled = true;
    config.rotation = 200;
    config.interval = 1;
    Config::validateDisplayConfig(config);
    TEST_ASSERT_TRUE(config.enabled);
}

// --- validateDeviceConfig ---

void test_validate_device_config_valid_values_unchanged() {
    Config::DeviceConfig config;
    config.target_temperature = 25.0f;
    config.elevation = 500.0f;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, config.target_temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 500.0f, config.elevation);
}

void test_validate_device_config_nan_temperature_reset() {
    Config::DeviceConfig config;
    config.target_temperature = NAN;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.0f, config.target_temperature);
}

void test_validate_device_config_temperature_too_low() {
    Config::DeviceConfig config;
    config.target_temperature = 5.0f;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.0f, config.target_temperature);
}

void test_validate_device_config_temperature_too_high() {
    Config::DeviceConfig config;
    config.target_temperature = 35.0f;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.0f, config.target_temperature);
}

void test_validate_device_config_temperature_at_lower_bound() {
    Config::DeviceConfig config;
    config.target_temperature = 10.0f;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, config.target_temperature);
}

void test_validate_device_config_temperature_at_upper_bound() {
    Config::DeviceConfig config;
    config.target_temperature = 30.0f;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, config.target_temperature);
}

void test_validate_device_config_nan_elevation_reset() {
    Config::DeviceConfig config;
    config.elevation = NAN;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, config.elevation);
}

void test_validate_device_config_elevation_too_low() {
    Config::DeviceConfig config;
    config.elevation = -600.0f;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, config.elevation);
}

void test_validate_device_config_elevation_too_high() {
    Config::DeviceConfig config;
    config.elevation = 10000.0f;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, config.elevation);
}

void test_validate_device_config_elevation_at_bounds() {
    Config::DeviceConfig config;
    config.elevation = -500.0f;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -500.0f, config.elevation);

    config.elevation = 9000.0f;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 9000.0f, config.elevation);
}

void test_validate_device_config_i2c_address_valid_unchanged() {
    Config::DeviceConfig config;
    config.sensor_i2c_address = Config::DEFAULT_SENSOR_I2C_ADDRESS;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_EQUAL(Config::DEFAULT_SENSOR_I2C_ADDRESS, config.sensor_i2c_address);

    config.sensor_i2c_address = 0x10;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_EQUAL(0x10, config.sensor_i2c_address);
}

void test_validate_device_config_i2c_address_too_low() {
    Config::DeviceConfig config;
    config.sensor_i2c_address = 0x03;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_EQUAL(Config::DEFAULT_SENSOR_I2C_ADDRESS, config.sensor_i2c_address);
}

void test_validate_device_config_i2c_address_too_high() {
    Config::DeviceConfig config;
    config.sensor_i2c_address = 0x78;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_EQUAL(Config::DEFAULT_SENSOR_I2C_ADDRESS, config.sensor_i2c_address);
}

void test_validate_device_config_i2c_address_at_bounds() {
    Config::DeviceConfig config;
    config.sensor_i2c_address = Config::MIN_SENSOR_I2C_ADDRESS;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_EQUAL(Config::MIN_SENSOR_I2C_ADDRESS, config.sensor_i2c_address);

    config.sensor_i2c_address = Config::MAX_SENSOR_I2C_ADDRESS;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_EQUAL(Config::MAX_SENSOR_I2C_ADDRESS, config.sensor_i2c_address);
}

// --- validateDeviceConfig: PID gains and control interval ---

void test_device_config_tuning_defaults() {
    Config::DeviceConfig config;
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, config.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0.0001f, config.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, config.kd);
    TEST_ASSERT_EQUAL(60, config.control_interval_s);
}

void test_validate_device_config_tuning_valid_unchanged() {
    Config::DeviceConfig config;
    config.kp = 1.25f;
    config.ki = 0.002f;
    config.kd = 30.0f;
    config.control_interval_s = 120;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.25f, config.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0.002f, config.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 30.0f, config.kd);
    TEST_ASSERT_EQUAL(120, config.control_interval_s);
}

void test_validate_device_config_tuning_at_bounds_preserved() {
    Config::DeviceConfig config;
    config.kp = Config::MIN_PID_KP;
    config.ki = Config::MIN_PID_KI;
    config.kd = Config::MIN_PID_KD;
    config.control_interval_s = Config::MIN_CONTROL_INTERVAL_S;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::MIN_PID_KP, config.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::MIN_PID_KI, config.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::MIN_PID_KD, config.kd);
    TEST_ASSERT_EQUAL(Config::MIN_CONTROL_INTERVAL_S, config.control_interval_s);

    config.kp = Config::MAX_PID_KP;
    config.ki = Config::MAX_PID_KI;
    config.kd = Config::MAX_PID_KD;
    config.control_interval_s = Config::MAX_CONTROL_INTERVAL_S;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::MAX_PID_KP, config.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::MAX_PID_KI, config.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::MAX_PID_KD, config.kd);
    TEST_ASSERT_EQUAL(Config::MAX_CONTROL_INTERVAL_S, config.control_interval_s);
}

// Zero integral and derivative action are legitimate choices — a P-only
// controller is droopy, not broken — so they must survive validation rather
// than being "corrected" to the default.
void test_validate_device_config_zero_ki_and_kd_preserved() {
    Config::DeviceConfig config;
    config.ki = 0.0f;
    config.kd = 0.0f;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, config.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, config.kd);
}

// A zero kp disables control while control still reports itself as enabled,
// which is the one gain value that must not be kept.
void test_validate_device_config_zero_kp_falls_back() {
    Config::DeviceConfig config;
    config.kp = 0.0f;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KP, config.kp);
}

void test_validate_device_config_negative_gains_fall_back() {
    Config::DeviceConfig config;
    config.kp = -1.0f;
    config.ki = -0.001f;
    config.kd = -5.0f;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KP, config.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KI, config.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KD, config.kd);
}

void test_validate_device_config_gains_above_max_fall_back() {
    Config::DeviceConfig config;
    config.kp = 1000.0f;
    config.ki = 0.1f; // the old shipped default, now above the maximum
    config.kd = 5000.0f;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KP, config.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KI, config.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KD, config.kd);
}

void test_validate_device_config_nan_gains_fall_back() {
    Config::DeviceConfig config;
    config.kp = NAN;
    config.ki = NAN;
    config.kd = NAN;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KP, config.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KI, config.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KD, config.kd);
}

void test_validate_device_config_infinite_gains_fall_back() {
    Config::DeviceConfig config;
    config.kp = INFINITY;
    config.ki = -INFINITY;
    config.kd = INFINITY;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KP, config.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KI, config.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KD, config.kd);
}

// Per-field fallback: a partial corruption costs the corrupted field only.
void test_validate_device_config_one_bad_gain_leaves_others() {
    Config::DeviceConfig config;
    config.kp = 2.5f;
    config.ki = 999.0f; // out of range
    config.kd = 12.0f;
    config.control_interval_s = 90;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.5f, config.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KI, config.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 12.0f, config.kd);
    TEST_ASSERT_EQUAL(90, config.control_interval_s);
}

void test_validate_device_config_interval_out_of_range_falls_back() {
    Config::DeviceConfig config;
    config.control_interval_s = 0;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_EQUAL(Config::DEFAULT_CONTROL_INTERVAL_S, config.control_interval_s);

    config.control_interval_s = 5000;
    Config::validateDeviceConfig(config);
    TEST_ASSERT_EQUAL(Config::DEFAULT_CONTROL_INTERVAL_S, config.control_interval_s);
}

// --- ConfigManager::updateTuning ---

void test_update_tuning_round_trip() {
    Config::ConfigManager manager;
    manager.updateTuning(1.5f, 0.003f, 20.0f, 30);
    const Config::DeviceConfig &stored = manager.getDeviceConfig();
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.5f, stored.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0.003f, stored.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 20.0f, stored.kd);
    TEST_ASSERT_EQUAL(30, stored.control_interval_s);
}

void test_update_tuning_validates_before_storing() {
    Config::ConfigManager manager;
    manager.updateTuning(0.0f, 0.001f, 0.0f, 45);
    const Config::DeviceConfig &stored = manager.getDeviceConfig();
    // kp was refused and fell back; the other three were trustworthy and kept.
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KP, stored.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0.001f, stored.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, stored.kd);
    TEST_ASSERT_EQUAL(45, stored.control_interval_s);
}

// updateTuning is a partial update: it must not disturb the fields it does not
// name, the way updateActuatorTiming() does not disturb the gains.
void test_update_tuning_leaves_other_fields_alone() {
    Config::ConfigManager manager;
    manager.updateTargetTemperature(24.0f);
    manager.updateTuning(3.0f, 0.004f, 1.0f, 15);
    const Config::DeviceConfig &stored = manager.getDeviceConfig();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 24.0f, stored.target_temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.0f, stored.kp);
}

// --- validateMqttConfig ---

void test_validate_mqtt_config_valid_values_unchanged() {
    Config::MqttConfig config;
    config.port = 8883;
    config.interval = 60;
    strlcpy(config.prefix, "home/bedroom", sizeof(config.prefix));
    Config::validateMqttConfig(config);
    TEST_ASSERT_EQUAL(8883, config.port);
    TEST_ASSERT_EQUAL(60, config.interval);
    TEST_ASSERT_EQUAL_STRING("home/bedroom", config.prefix);
}

void test_validate_mqtt_config_zero_port_reset() {
    Config::MqttConfig config;
    config.port = 0;
    Config::validateMqttConfig(config);
    TEST_ASSERT_EQUAL(1883, config.port);
}

void test_validate_mqtt_config_zero_interval_reset() {
    Config::MqttConfig config;
    config.interval = 0;
    Config::validateMqttConfig(config);
    TEST_ASSERT_EQUAL(15, config.interval);
}

void test_validate_mqtt_config_interval_too_high() {
    Config::MqttConfig config;
    config.interval = 7200;
    Config::validateMqttConfig(config);
    TEST_ASSERT_EQUAL(15, config.interval);
}

void test_validate_mqtt_config_interval_at_bounds() {
    Config::MqttConfig config;
    config.interval = 1;
    Config::validateMqttConfig(config);
    TEST_ASSERT_EQUAL(1, config.interval);

    config.interval = 3600;
    Config::validateMqttConfig(config);
    TEST_ASSERT_EQUAL(3600, config.interval);
}

void test_validate_mqtt_config_empty_prefix_gets_default() {
    Config::MqttConfig config;
    config.prefix[0] = '\0';
    Config::validateMqttConfig(config);
    TEST_ASSERT_EQUAL_STRING("sensors", config.prefix);
}

// --- validateSensorConfig (if exists, test it; otherwise test struct defaults) ---

void test_sensor_config_assignments_can_be_copied() {
    Config::SensorConfig config;
    strlcpy(config.assignments, "44=SHT4x,77=BME680", sizeof(config.assignments));
    TEST_ASSERT_EQUAL_STRING("44=SHT4x,77=BME680", config.assignments);
}

void test_sensor_config_empty_assignments() {
    Config::SensorConfig config;
    TEST_ASSERT_EQUAL_STRING("", config.assignments);
}

// --- SyslogConfig defaults ---

void test_syslog_config_port_default() {
    Config::SyslogConfig config;
    TEST_ASSERT_EQUAL(514, config.port);
}

void test_syslog_config_empty_host() {
    Config::SyslogConfig config;
    TEST_ASSERT_EQUAL_STRING("", config.host);
}

// --- WiFiConfig defaults ---

void test_wifi_config_connection_failures_default() {
    Config::WiFiConfig config;
    TEST_ASSERT_EQUAL(0, config.connection_failures);
}

void test_wifi_config_not_configured_by_default() {
    Config::WiFiConfig config;
    TEST_ASSERT_FALSE(config.configured);
}

// --- Factory reset behavior (simulated struct reset) ---

void test_factory_reset_device_config_creates_defaults() {
    Config::DeviceConfig config;
    config.target_temperature = 25.0f;
    config.temperature_control_enabled = true;
    config.elevation = 500.0f;

    config.target_temperature = 22.0f;
    config.temperature_control_enabled = false;
    config.elevation = 0.0f;

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.0f, config.target_temperature);
    TEST_ASSERT_FALSE(config.temperature_control_enabled);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, config.elevation);
}

// --- validateEnergyConfig ---

void test_validate_energy_config_valid_power_levels() {
    uint8_t validPowers[] = {8, 34, 52, 68, 80};
    for (uint8_t wp : validPowers) {
        Config::EnergyConfig config;
        config.wifi_power = wp;
        Config::validateEnergyConfig(config);
        TEST_ASSERT_EQUAL(wp, config.wifi_power);
    }
}

void test_validate_energy_config_invalid_power_reset() {
    Config::EnergyConfig config;
    config.wifi_power = 42; // not a valid power level
    Config::validateEnergyConfig(config);
    TEST_ASSERT_EQUAL(Constants::DEFAULT_WIFI_POWER, config.wifi_power);
}

void test_validate_energy_config_zero_power_reset() {
    Config::EnergyConfig config;
    config.wifi_power = 0;
    Config::validateEnergyConfig(config);
    TEST_ASSERT_EQUAL(Constants::DEFAULT_WIFI_POWER, config.wifi_power);
}

void test_validate_energy_config_max_uint8_reset() {
    Config::EnergyConfig config;
    config.wifi_power = 255;
    Config::validateEnergyConfig(config);
    TEST_ASSERT_EQUAL(Constants::DEFAULT_WIFI_POWER, config.wifi_power);
}

void test_get_device_id() {
    Config::ConfigManager config;
    String id = config.getDeviceId();
    TEST_ASSERT_EQUAL(6, id.length());
    // In native mode it should return "000000"
    TEST_ASSERT_EQUAL_STRING("000000", id.c_str());
}

int runUnityTests() {
    UNITY_BEGIN();
    // Struct defaults
    RUN_TEST(test_wifi_config_defaults);
    RUN_TEST(test_device_config_defaults);
    RUN_TEST(test_mqtt_config_defaults);
    RUN_TEST(test_sensor_config_defaults);
    RUN_TEST(test_energy_config_defaults);
    RUN_TEST(test_syslog_config_defaults);
    RUN_TEST(test_display_config_defaults);
    // DeviceConfig timezone
    RUN_TEST(test_device_config_timezone_default);
    RUN_TEST(test_validate_device_config_empty_timezone_reset);
    RUN_TEST(test_validate_device_config_nonprintable_timezone_reset);
    RUN_TEST(test_validate_device_config_valid_timezone_preserved);
    RUN_TEST(test_get_device_id);
    // DisplayConfig validation
    RUN_TEST(test_validate_display_config_defaults_unchanged);
    RUN_TEST(test_validate_display_config_rotation_valid_unchanged);
    RUN_TEST(test_validate_display_config_rotation_too_high_reset);
    RUN_TEST(test_validate_display_config_interval_below_floor_clamped);
    RUN_TEST(test_validate_display_config_interval_at_bounds_unchanged);
    RUN_TEST(test_validate_display_config_interval_above_ceiling_clamped);
    RUN_TEST(test_validate_display_config_preserves_enabled);
    // DeviceConfig validation
    RUN_TEST(test_validate_device_config_valid_values_unchanged);
    RUN_TEST(test_validate_device_config_nan_temperature_reset);
    RUN_TEST(test_validate_device_config_temperature_too_low);
    RUN_TEST(test_validate_device_config_temperature_too_high);
    RUN_TEST(test_validate_device_config_temperature_at_lower_bound);
    RUN_TEST(test_validate_device_config_temperature_at_upper_bound);
    RUN_TEST(test_validate_device_config_nan_elevation_reset);
    RUN_TEST(test_validate_device_config_elevation_too_low);
    RUN_TEST(test_validate_device_config_elevation_too_high);
    RUN_TEST(test_validate_device_config_elevation_at_bounds);
    RUN_TEST(test_validate_device_config_i2c_address_valid_unchanged);
    RUN_TEST(test_validate_device_config_i2c_address_too_low);
    RUN_TEST(test_validate_device_config_i2c_address_too_high);
    RUN_TEST(test_validate_device_config_i2c_address_at_bounds);

    RUN_TEST(test_device_config_tuning_defaults);
    RUN_TEST(test_validate_device_config_tuning_valid_unchanged);
    RUN_TEST(test_validate_device_config_tuning_at_bounds_preserved);
    RUN_TEST(test_validate_device_config_zero_ki_and_kd_preserved);
    RUN_TEST(test_validate_device_config_zero_kp_falls_back);
    RUN_TEST(test_validate_device_config_negative_gains_fall_back);
    RUN_TEST(test_validate_device_config_gains_above_max_fall_back);
    RUN_TEST(test_validate_device_config_nan_gains_fall_back);
    RUN_TEST(test_validate_device_config_infinite_gains_fall_back);
    RUN_TEST(test_validate_device_config_one_bad_gain_leaves_others);
    RUN_TEST(test_validate_device_config_interval_out_of_range_falls_back);
    RUN_TEST(test_update_tuning_round_trip);
    RUN_TEST(test_update_tuning_validates_before_storing);
    RUN_TEST(test_update_tuning_leaves_other_fields_alone);
    // MqttConfig validation
    RUN_TEST(test_validate_mqtt_config_valid_values_unchanged);
    RUN_TEST(test_validate_mqtt_config_zero_port_reset);
    RUN_TEST(test_validate_mqtt_config_zero_interval_reset);
    RUN_TEST(test_validate_mqtt_config_interval_too_high);
    RUN_TEST(test_validate_mqtt_config_interval_at_bounds);
    RUN_TEST(test_validate_mqtt_config_empty_prefix_gets_default);
    // EnergyConfig validation
    RUN_TEST(test_validate_energy_config_valid_power_levels);
    RUN_TEST(test_validate_energy_config_invalid_power_reset);
    RUN_TEST(test_validate_energy_config_zero_power_reset);
    RUN_TEST(test_validate_energy_config_max_uint8_reset);
    // SensorConfig
    RUN_TEST(test_sensor_config_assignments_can_be_copied);
    RUN_TEST(test_sensor_config_empty_assignments);
    // SyslogConfig defaults
    RUN_TEST(test_syslog_config_port_default);
    RUN_TEST(test_syslog_config_empty_host);
    // WiFiConfig defaults
    RUN_TEST(test_wifi_config_connection_failures_default);
    RUN_TEST(test_wifi_config_not_configured_by_default);
    // Factory reset simulation
    RUN_TEST(test_factory_reset_device_config_creates_defaults);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
