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
    TEST_ASSERT_EQUAL(0ul, stats->get_count());
    TEST_ASSERT_EQUAL(0ul, stats->get_min());
    TEST_ASSERT_EQUAL(0ul, stats->get_max());
}

void test_one_value() {
    stats->add(10);
    TEST_ASSERT_EQUAL(10ul, stats->get_average());
    TEST_ASSERT_EQUAL(1ul, stats->get_count());
    TEST_ASSERT_EQUAL(10ul, stats->get_min());
    TEST_ASSERT_EQUAL(10ul, stats->get_max());
}

void test_two_values() {
    stats->add(10);
    stats->add(20);
    TEST_ASSERT_EQUAL(15ul, stats->get_average());
    TEST_ASSERT_EQUAL(2ul, stats->get_count());
    TEST_ASSERT_EQUAL(10ul, stats->get_min());
    TEST_ASSERT_EQUAL(20ul, stats->get_max());
}

void test_min_max_multiple_values() {
    stats->add(50);
    stats->add(5);
    stats->add(100);
    stats->add(25);
    TEST_ASSERT_EQUAL(5ul, stats->get_min());
    TEST_ASSERT_EQUAL(100ul, stats->get_max());
    TEST_ASSERT_EQUAL(45ul, stats->get_average());
    TEST_ASSERT_EQUAL(4ul, stats->get_count());
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_initial_state);
    RUN_TEST(test_one_value);
    RUN_TEST(test_two_values);
    RUN_TEST(test_min_max_multiple_values);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
