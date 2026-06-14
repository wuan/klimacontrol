#include "unity.h"
#include <cstdint>
#include <cstring>
#include <cstdio>

void setUp() {}
void tearDown() {}

// --- WiFi reconnection failure counter logic ---

static constexpr uint8_t MAX_WIFI_RECONNECT_FAILURES = 10;

static bool shouldRestartOnReconnectFailure(uint8_t failures) {
    return failures >= MAX_WIFI_RECONNECT_FAILURES;
}

void test_wifi_reconnect_failure_below_threshold() {
    TEST_ASSERT_FALSE(shouldRestartOnReconnectFailure(9));
}

void test_wifi_reconnect_failure_at_threshold() {
    TEST_ASSERT_TRUE(shouldRestartOnReconnectFailure(MAX_WIFI_RECONNECT_FAILURES));
}

void test_wifi_reconnect_failure_above_threshold() {
    TEST_ASSERT_TRUE(shouldRestartOnReconnectFailure(15));
}

void test_wifi_reconnect_failure_reset_on_success() {
    uint8_t failures = MAX_WIFI_RECONNECT_FAILURES - 1;
    failures = 0;
    TEST_ASSERT_FALSE(shouldRestartOnReconnectFailure(failures));
}

// --- NTP epoch guard logic ---

static bool shouldUpdateNtp(uint32_t currentEpoch, uint32_t lastNtpUpdate) {
    return currentEpoch > 0 && lastNtpUpdate > 0
           && currentEpoch - lastNtpUpdate > 3600;
}

void test_ntp_epoch_guard_both_zero_no_update() {
    TEST_ASSERT_FALSE(shouldUpdateNtp(0u, 0u));
}

void test_ntp_epoch_guard_time_elapsed_triggers_update() {
    uint32_t last = 1700000000u;
    uint32_t current = last + 3601u;
    TEST_ASSERT_TRUE(shouldUpdateNtp(current, last));
}

void test_ntp_epoch_guard_current_zero_no_spurious_update() {
    uint32_t last = 1700000000u;
    TEST_ASSERT_FALSE(shouldUpdateNtp(0u, last));
}

void test_ntp_epoch_guard_last_zero_current_nonzero_no_update() {
    uint32_t current = 1700000000u;
    TEST_ASSERT_FALSE(shouldUpdateNtp(current, 0u));
}

void test_ntp_epoch_guard_exactly_one_hour_no_update() {
    uint32_t last = 1700000000u;
    uint32_t current = last + 3600u;
    TEST_ASSERT_FALSE(shouldUpdateNtp(current, last));
}

// --- mDNS hostname generation (simulated for native) ---

static const char* PROJECT_NAME = "klima";

static const char* generateHostname(const char* deviceId) {
    static char hostname[32];
    if (deviceId && deviceId[0]) {
        snprintf(hostname, sizeof(hostname), "klima-%s", deviceId);
    } else {
        snprintf(hostname, sizeof(hostname), "%s", PROJECT_NAME);
    }
    return hostname;
}

void test_hostname_generation_with_device_id() {
    const char* hostname = generateHostname("AABBCC");
    TEST_ASSERT_EQUAL_STRING("klima-AABBCC", hostname);
}

void test_hostname_generation_lowercase() {
    const char* hostname = generateHostname("aabbcc");
    TEST_ASSERT_EQUAL_STRING("klima-aabbcc", hostname);
}

void test_hostname_generation_empty_device_id() {
    const char* hostname = generateHostname("");
    TEST_ASSERT_EQUAL_STRING("klima", hostname);
}

void test_hostname_generation_null_device_id() {
    const char* hostname = generateHostname(nullptr);
    TEST_ASSERT_EQUAL_STRING("klima", hostname);
}

int runUnityTests() {
    UNITY_BEGIN();
    // WiFi reconnection tests
    RUN_TEST(test_wifi_reconnect_failure_below_threshold);
    RUN_TEST(test_wifi_reconnect_failure_at_threshold);
    RUN_TEST(test_wifi_reconnect_failure_above_threshold);
    RUN_TEST(test_wifi_reconnect_failure_reset_on_success);
    // NTP epoch tests
    RUN_TEST(test_ntp_epoch_guard_both_zero_no_update);
    RUN_TEST(test_ntp_epoch_guard_time_elapsed_triggers_update);
    RUN_TEST(test_ntp_epoch_guard_current_zero_no_spurious_update);
    RUN_TEST(test_ntp_epoch_guard_last_zero_current_nonzero_no_update);
    RUN_TEST(test_ntp_epoch_guard_exactly_one_hour_no_update);
    // mDNS hostname tests
    RUN_TEST(test_hostname_generation_with_device_id);
    RUN_TEST(test_hostname_generation_lowercase);
    RUN_TEST(test_hostname_generation_empty_device_id);
    RUN_TEST(test_hostname_generation_null_device_id);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}