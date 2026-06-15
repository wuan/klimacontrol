#include "StatusLed.h"
#include "unity.h"

void setUp() {}
void tearDown() {}

void test_default_state_is_off() {
    StatusLed led;
    TEST_ASSERT_EQUAL(LedState::OFF, led.getState());
}

void test_set_state_error() {
    StatusLed led;
    led.setState(LedState::ERROR);
    TEST_ASSERT_EQUAL(LedState::ERROR, led.getState());
}

void test_set_state_error_is_idempotent() {
    StatusLed led;
    led.setState(LedState::ERROR);
    led.setState(LedState::ERROR);
    TEST_ASSERT_EQUAL(LedState::ERROR, led.getState());
}

void test_transition_off_to_error() {
    StatusLed led;
    led.setState(LedState::OFF);
    led.setState(LedState::ERROR);
    TEST_ASSERT_EQUAL(LedState::ERROR, led.getState());
}

void test_transition_on_to_error() {
    StatusLed led;
    led.setState(LedState::ON);
    led.setState(LedState::ERROR);
    TEST_ASSERT_EQUAL(LedState::ERROR, led.getState());
}

void test_transition_startup_to_error() {
    StatusLed led;
    led.setState(LedState::STARTUP);
    led.setState(LedState::ERROR);
    TEST_ASSERT_EQUAL(LedState::ERROR, led.getState());
}

void test_update_in_error_state_does_not_crash_on_native() {
    StatusLed led;
    led.setState(LedState::ERROR);
    // On native (no ARDUINO), update() is a no-op for the pixel but must
    // dispatch through the switch and reach the new ERROR case. Just
    // verifying the call doesn't crash covers the dispatch path.
    led.update();
    TEST_ASSERT_EQUAL(LedState::ERROR, led.getState());
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_default_state_is_off);
    RUN_TEST(test_set_state_error);
    RUN_TEST(test_set_state_error_is_idempotent);
    RUN_TEST(test_transition_off_to_error);
    RUN_TEST(test_transition_on_to_error);
    RUN_TEST(test_transition_startup_to_error);
    RUN_TEST(test_update_in_error_state_does_not_crash_on_native);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
