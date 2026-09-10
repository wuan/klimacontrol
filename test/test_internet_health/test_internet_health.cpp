#include "network/InternetHealth.h"
#include "unity.h"

using Net::InternetHealth;

void setUp() {}
void tearDown() {}

void test_starts_healthy() {
    InternetHealth h;
    TEST_ASSERT_EQUAL_UINT32(0, h.failures());
    TEST_ASSERT_FALSE(h.shouldForceReconnect(1000000));
}

void test_below_threshold_never_reconnects() {
    InternetHealth h;
    for (uint32_t i = 0; i + 1 < InternetHealth::FAILURE_THRESHOLD; i++) h.reportFailure();
    TEST_ASSERT_FALSE(h.shouldForceReconnect(1000000));
}

void test_threshold_reached_reconnects_once_per_window() {
    InternetHealth h;
    for (uint32_t i = 0; i < InternetHealth::FAILURE_THRESHOLD; i++) h.reportFailure();
    const uint32_t t = 1000000;
    TEST_ASSERT_TRUE(h.shouldForceReconnect(t));
    // Failures keep coming but the window has not elapsed.
    h.reportFailure();
    TEST_ASSERT_FALSE(h.shouldForceReconnect(t + InternetHealth::FAILURE_WINDOW_MS - 1));
    TEST_ASSERT_TRUE(h.shouldForceReconnect(t + InternetHealth::FAILURE_WINDOW_MS));
}

void test_success_clears_failures() {
    InternetHealth h;
    for (uint32_t i = 0; i < InternetHealth::FAILURE_THRESHOLD; i++) h.reportFailure();
    h.reportSuccess();
    TEST_ASSERT_EQUAL_UINT32(0, h.failures());
    TEST_ASSERT_FALSE(h.shouldForceReconnect(1000000));
}

void test_reset_clears_failures_and_restarts_window() {
    InternetHealth h;
    for (uint32_t i = 0; i < InternetHealth::FAILURE_THRESHOLD; i++) h.reportFailure();
    const uint32_t t = 500000;
    h.reset(t);
    TEST_ASSERT_EQUAL_UINT32(0, h.failures());
    for (uint32_t i = 0; i < InternetHealth::FAILURE_THRESHOLD; i++) h.reportFailure();
    // Post-reset window: no action until a full window has passed since reset.
    TEST_ASSERT_FALSE(h.shouldForceReconnect(t + InternetHealth::FAILURE_WINDOW_MS - 1));
    TEST_ASSERT_TRUE(h.shouldForceReconnect(t + InternetHealth::FAILURE_WINDOW_MS));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_starts_healthy);
    RUN_TEST(test_below_threshold_never_reconnects);
    RUN_TEST(test_threshold_reached_reconnects_once_per_window);
    RUN_TEST(test_success_clears_failures);
    RUN_TEST(test_reset_clears_failures_and_restarts_window);
    return UNITY_END();
}
