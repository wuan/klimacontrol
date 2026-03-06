#include "unity.h"
#include "support/Stats.h"

Support::Stats *stats;

void setUp() {
    stats = new Support::Stats();
}

void tearDown() {
    delete stats;
}

void test_initial_state() {
    TEST_ASSERT_EQUAL(0ul, stats->get_average());
}

void test_one_value() {
    stats->add(10);
    TEST_ASSERT_EQUAL(10ul, stats->get_average());
}

void test_two_values() {
    stats->add(10);
    stats->add(20);
    TEST_ASSERT_EQUAL(15ul, stats->get_average());
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_initial_state);
    RUN_TEST(test_one_value);
    RUN_TEST(test_two_values);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}