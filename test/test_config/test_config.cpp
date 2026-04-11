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
    TEST_ASSERT_EQUAL(0x44, config.sensor_i2c_address);
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

int runUnityTests() {
    UNITY_BEGIN();
    // Struct defaults
    RUN_TEST(test_wifi_config_defaults);
    RUN_TEST(test_device_config_defaults);
    RUN_TEST(test_mqtt_config_defaults);
    RUN_TEST(test_sensor_config_defaults);
    RUN_TEST(test_energy_config_defaults);
    RUN_TEST(test_syslog_config_defaults);
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
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
