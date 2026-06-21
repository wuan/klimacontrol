#include <climits>
#include <cstdint>

#include "support/NtpEpoch.h"
#include "unity.h"

void setUp() {}
void tearDown() {}

// Boundary tests for isNtpEpochPlausible().
//
// The check is supposed to return true iff the epoch is in
// [NTP_MIN_VALID_EPOCH, NTP_MAX_VALID_EPOCH] (year 2020 to year 2100 UTC).
// 0 is the documented "not yet synced" sentinel and MUST be rejected;
// UINT32_MAX and the year-2100 boundary MUST be rejected; the two exact
// boundaries MUST be accepted (inclusive comparison).

void test_zero_rejected() {
    TEST_ASSERT_FALSE(isNtpEpochPlausible(0));
}

void test_below_min_rejected() {
    TEST_ASSERT_FALSE(isNtpEpochPlausible(NtpEpoch::MIN_VALID - 1));
}

void test_at_min_accepted() {
    TEST_ASSERT_TRUE(isNtpEpochPlausible(NtpEpoch::MIN_VALID));
}

void test_above_min_accepted() {
    TEST_ASSERT_TRUE(isNtpEpochPlausible(NtpEpoch::MIN_VALID + 1));
}

void test_typical_2026_accepted() {
    // Roughly 2026-01-01: ~1_767_225_600
    TEST_ASSERT_TRUE(isNtpEpochPlausible(1767225600));
}

void test_just_below_max_accepted() {
    TEST_ASSERT_TRUE(isNtpEpochPlausible(NtpEpoch::MAX_VALID - 1));
}

void test_at_max_accepted() {
    TEST_ASSERT_TRUE(isNtpEpochPlausible(NtpEpoch::MAX_VALID));
}

void test_above_max_rejected() {
    TEST_ASSERT_FALSE(isNtpEpochPlausible(NtpEpoch::MAX_VALID + 1));
}

void test_uint32_max_rejected() {
    TEST_ASSERT_FALSE(isNtpEpochPlausible(UINT32_MAX));
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_zero_rejected);
    RUN_TEST(test_below_min_rejected);
    RUN_TEST(test_at_min_accepted);
    RUN_TEST(test_above_min_accepted);
    RUN_TEST(test_typical_2026_accepted);
    RUN_TEST(test_just_below_max_accepted);
    RUN_TEST(test_at_max_accepted);
    RUN_TEST(test_above_max_rejected);
    RUN_TEST(test_uint32_max_rejected);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
