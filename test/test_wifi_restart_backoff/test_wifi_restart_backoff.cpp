#include "support/WifiBackoff.h"
#include "unity.h"

void setUp() {}
void tearDown() {}

// 3.3 — doubling portion of the curve: failures 1..8.
void test_backoff_doubles_each_failure() {
    TEST_ASSERT_EQUAL_UINT32(2000,   Support::staFailureBackoffMs(1));
    TEST_ASSERT_EQUAL_UINT32(4000,   Support::staFailureBackoffMs(2));
    TEST_ASSERT_EQUAL_UINT32(8000,   Support::staFailureBackoffMs(3));
    TEST_ASSERT_EQUAL_UINT32(16000,  Support::staFailureBackoffMs(4));
    TEST_ASSERT_EQUAL_UINT32(32000,  Support::staFailureBackoffMs(5));
    TEST_ASSERT_EQUAL_UINT32(64000,  Support::staFailureBackoffMs(6));
    TEST_ASSERT_EQUAL_UINT32(128000, Support::staFailureBackoffMs(7));
    TEST_ASSERT_EQUAL_UINT32(256000, Support::staFailureBackoffMs(8));
}

// 3.4 — cap: failures 9 and beyond all return the 5-minute cap.
// Also asserts no overflow on very large failure counts.
void test_backoff_saturates_at_cap() {
    constexpr uint32_t CAP_MS = 300000; // 5 minutes
    TEST_ASSERT_EQUAL_UINT32(CAP_MS, Support::staFailureBackoffMs(9));
    TEST_ASSERT_EQUAL_UINT32(CAP_MS, Support::staFailureBackoffMs(10));
    TEST_ASSERT_EQUAL_UINT32(CAP_MS, Support::staFailureBackoffMs(12));
    TEST_ASSERT_EQUAL_UINT32(CAP_MS, Support::staFailureBackoffMs(32));
    TEST_ASSERT_EQUAL_UINT32(CAP_MS, Support::staFailureBackoffMs(200));
    TEST_ASSERT_EQUAL_UINT32(CAP_MS, Support::staFailureBackoffMs(255));
}

// 3.5 — monotonicity: for failures = 1..16, the delay is non-decreasing.
// Guards against a future regression that re-orders the curve.
void test_backoff_is_monotonic_for_first_sixteen_failures() {
    uint32_t prev = 0;
    for (uint8_t f = 1; f <= 16; ++f) {
        uint32_t cur = Support::staFailureBackoffMs(f);
        TEST_ASSERT_TRUE(cur >= prev);
        prev = cur;
    }
}

// Edge case: failures == 0 is clamped to 1 internally so the helper
// still returns the 2 s base. (A future caller that forgot to increment
// before calling should not produce nonsense.)
void test_backoff_zero_failures_returns_base() {
    TEST_ASSERT_EQUAL_UINT32(2000, Support::staFailureBackoffMs(0));
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_backoff_doubles_each_failure);
    RUN_TEST(test_backoff_saturates_at_cap);
    RUN_TEST(test_backoff_is_monotonic_for_first_sixteen_failures);
    RUN_TEST(test_backoff_zero_failures_returns_base);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
