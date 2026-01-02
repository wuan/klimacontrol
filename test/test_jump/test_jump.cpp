#include "unity.h"
#include "show/Jump.h"

Show::Jump::Ball ball(1.0f, 0xff0000);

void setUp() {
}

void tearDown() {
}

void test_jump_basic_position() {
    auto position = ball.get_position(0, 100);
    TEST_ASSERT_TRUE(position >= 0);
}

void test_jump_start_of_peak() {
    auto position = ball.get_position(0, 100);
    TEST_ASSERT_EQUAL_INT(0, position);
}

void test_jump_mid_of_peak() {
    // Peak should be highest around the middle
    auto pos_start = ball.get_position(0, 100);
    auto pos_mid = ball.get_position(50, 100);
    TEST_ASSERT_TRUE(pos_mid > pos_start);
}

void test_jump_end_of_peak() {
    auto position = ball.get_position(110, 100);
    TEST_ASSERT_EQUAL_INT(99, position);
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_jump_basic_position);
    RUN_TEST(test_jump_start_of_peak);
    RUN_TEST(test_jump_mid_of_peak);
    RUN_TEST(test_jump_end_of_peak);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
