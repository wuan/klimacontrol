#include "network/LowHeapGuard.h"
#include "unity.h"

using Net::LowHeapGuard;

void setUp() {}
void tearDown() {}

namespace {
    constexpr uint32_t LOW = LowHeapGuard::MIN_FREE_INTERNAL_BYTES - 1;
    constexpr uint32_t OK = LowHeapGuard::MIN_FREE_INTERNAL_BYTES;
}

void test_healthy_heap_never_restarts() {
    LowHeapGuard g;
    for (int i = 0; i < 100; i++) TEST_ASSERT_FALSE(g.sample(OK));
    TEST_ASSERT_EQUAL_UINT8(0, g.streak());
}

void test_restart_only_after_streak() {
    LowHeapGuard g;
    for (uint8_t i = 1; i < LowHeapGuard::RESTART_STREAK; i++) {
        TEST_ASSERT_FALSE(g.sample(LOW));
        TEST_ASSERT_EQUAL_UINT8(i, g.streak());
    }
    TEST_ASSERT_TRUE(g.sample(LOW));
}

void test_transient_dip_resets_streak() {
    LowHeapGuard g;
    for (uint8_t i = 1; i < LowHeapGuard::RESTART_STREAK; i++) g.sample(LOW);
    TEST_ASSERT_FALSE(g.sample(OK));
    TEST_ASSERT_EQUAL_UINT8(0, g.streak());
    TEST_ASSERT_FALSE(g.sample(LOW));
    TEST_ASSERT_EQUAL_UINT8(1, g.streak());
}

void test_reset_clears_streak_after_ota() {
    LowHeapGuard g;
    for (uint8_t i = 1; i < LowHeapGuard::RESTART_STREAK; i++) g.sample(LOW);
    TEST_ASSERT_EQUAL_UINT8(LowHeapGuard::RESTART_STREAK - 1, g.streak());
    g.reset();
    TEST_ASSERT_EQUAL_UINT8(0, g.streak());
    TEST_ASSERT_FALSE(g.sample(LOW));
    TEST_ASSERT_EQUAL_UINT8(1, g.streak());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_healthy_heap_never_restarts);
    RUN_TEST(test_restart_only_after_streak);
    RUN_TEST(test_transient_dip_resets_streak);
    RUN_TEST(test_reset_clears_streak_after_ota);
    return UNITY_END();
}
