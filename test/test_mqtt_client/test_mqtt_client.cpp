#include "unity.h"
#include "MqttClient.h"

void setUp() {}
void tearDown() {}

// --- isIpAddress ---

void test_is_ip_valid_ipv4() {
    TEST_ASSERT_TRUE(isIpAddress("192.168.1.1"));
}

void test_is_ip_localhost() {
    TEST_ASSERT_TRUE(isIpAddress("127.0.0.1"));
}

void test_is_ip_all_zeros() {
    TEST_ASSERT_TRUE(isIpAddress("0.0.0.0"));
}

void test_is_ip_hostname() {
    TEST_ASSERT_FALSE(isIpAddress("mqtt.example.com"));
}

void test_is_ip_hostname_with_numbers() {
    TEST_ASSERT_FALSE(isIpAddress("broker1.local"));
}

void test_is_ip_empty_string() {
    TEST_ASSERT_FALSE(isIpAddress(""));
}

void test_is_ip_null() {
    TEST_ASSERT_FALSE(isIpAddress(nullptr));
}

void test_is_ip_just_digits() {
    // Edge case: just digits, no dots — technically passes the char check
    TEST_ASSERT_TRUE(isIpAddress("12345"));
}

void test_is_ip_with_colon() {
    // IPv6-like — should not match
    TEST_ASSERT_FALSE(isIpAddress("::1"));
}

void test_is_ip_with_space() {
    TEST_ASSERT_FALSE(isIpAddress("192.168.1 .1"));
}

// --- MqttClient::recordPublishResult ---

void test_record_publish_all_success() {
    MqttClient client;
    client.recordPublishResult(5, 0);
    TEST_ASSERT_EQUAL(5, client.getPublishedCount());
    TEST_ASSERT_EQUAL(0, client.getFailedCount());
    TEST_ASSERT_EQUAL(1, client.getPublishCycles());
    TEST_ASSERT_EQUAL(0, client.getFailedCycles());
}

void test_record_publish_with_failures() {
    MqttClient client;
    client.recordPublishResult(3, 2);
    TEST_ASSERT_EQUAL(3, client.getPublishedCount());
    TEST_ASSERT_EQUAL(2, client.getFailedCount());
    TEST_ASSERT_EQUAL(1, client.getPublishCycles());
    TEST_ASSERT_EQUAL(1, client.getFailedCycles());
}

void test_record_publish_accumulates() {
    MqttClient client;
    client.recordPublishResult(5, 0);
    client.recordPublishResult(3, 1);
    client.recordPublishResult(4, 0);
    TEST_ASSERT_EQUAL(12, client.getPublishedCount());
    TEST_ASSERT_EQUAL(1, client.getFailedCount());
    TEST_ASSERT_EQUAL(3, client.getPublishCycles());
    TEST_ASSERT_EQUAL(1, client.getFailedCycles());
}

void test_record_publish_initial_counts_zero() {
    MqttClient client;
    TEST_ASSERT_EQUAL(0, client.getPublishedCount());
    TEST_ASSERT_EQUAL(0, client.getFailedCount());
    TEST_ASSERT_EQUAL(0, client.getPublishCycles());
    TEST_ASSERT_EQUAL(0, client.getFailedCycles());
}

// --- MqttClient::isEnabled ---

void test_is_enabled_default_false() {
    MqttClient client;
    TEST_ASSERT_FALSE(client.isEnabled());
}

void test_is_enabled_after_begin() {
    MqttClient client;
    Config::MqttConfig config;
    config.enabled = true;
    strlcpy(config.host, "192.168.1.1", sizeof(config.host));
    client.begin(config);
    TEST_ASSERT_TRUE(client.isEnabled());
}

void test_is_enabled_false_when_config_disabled() {
    MqttClient client;
    Config::MqttConfig config;
    config.enabled = false;
    client.begin(config);
    TEST_ASSERT_FALSE(client.isEnabled());
}

// --- MqttClient::getIntervalMs ---

void test_interval_ms_default() {
    MqttClient client;
    Config::MqttConfig config;
    // default interval is 15 seconds
    client.begin(config);
    TEST_ASSERT_EQUAL(15000, client.getIntervalMs());
}

void test_interval_ms_custom() {
    MqttClient client;
    Config::MqttConfig config;
    config.interval = 60;
    client.begin(config);
    TEST_ASSERT_EQUAL(60000, client.getIntervalMs());
}

// --- MqttClient::getPrefix ---

void test_prefix_default() {
    MqttClient client;
    Config::MqttConfig config;
    client.begin(config);
    TEST_ASSERT_EQUAL_STRING("sensors", client.getPrefix());
}

void test_prefix_custom() {
    MqttClient client;
    Config::MqttConfig config;
    strlcpy(config.prefix, "home/living", sizeof(config.prefix));
    client.begin(config);
    TEST_ASSERT_EQUAL_STRING("home/living", client.getPrefix());
}

int runUnityTests() {
    UNITY_BEGIN();
    // isIpAddress
    RUN_TEST(test_is_ip_valid_ipv4);
    RUN_TEST(test_is_ip_localhost);
    RUN_TEST(test_is_ip_all_zeros);
    RUN_TEST(test_is_ip_hostname);
    RUN_TEST(test_is_ip_hostname_with_numbers);
    RUN_TEST(test_is_ip_empty_string);
    RUN_TEST(test_is_ip_null);
    RUN_TEST(test_is_ip_just_digits);
    RUN_TEST(test_is_ip_with_colon);
    RUN_TEST(test_is_ip_with_space);
    // recordPublishResult
    RUN_TEST(test_record_publish_all_success);
    RUN_TEST(test_record_publish_with_failures);
    RUN_TEST(test_record_publish_accumulates);
    RUN_TEST(test_record_publish_initial_counts_zero);
    // isEnabled
    RUN_TEST(test_is_enabled_default_false);
    RUN_TEST(test_is_enabled_after_begin);
    RUN_TEST(test_is_enabled_false_when_config_disabled);
    // getIntervalMs
    RUN_TEST(test_interval_ms_default);
    RUN_TEST(test_interval_ms_custom);
    // getPrefix
    RUN_TEST(test_prefix_default);
    RUN_TEST(test_prefix_custom);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
