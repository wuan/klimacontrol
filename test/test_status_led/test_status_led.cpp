#include "unity.h"
#include "StatusLed.h"

// Mock Arduino functions for testing
unsigned long millis() {
    static unsigned long counter = 0;
    return counter += 50; // Increment by 50ms each call
}

void pinMode(uint8_t, uint8_t) {
    // Mock implementation
}

void digitalWrite(uint8_t, uint8_t) {
    // Mock implementation
}

void analogWrite(uint8_t, int) {
    // Mock implementation
}

StatusLed* testLed;

void setUp() {
    testLed = new StatusLed(13, 1); // Use pin 13, 1 pixel for testing
    testLed->begin();
}

void tearDown() {
    delete testLed;
}

void test_initial_state() {
    TEST_ASSERT_EQUAL(LedState::OFF, testLed->getState());
}

void test_set_state_on() {
    testLed->setState(LedState::ON);
    TEST_ASSERT_EQUAL(LedState::ON, testLed->getState());
}

void test_set_state_off() {
    testLed->setState(LedState::ON);
    testLed->setState(LedState::OFF);
    TEST_ASSERT_EQUAL(LedState::OFF, testLed->getState());
}

void test_set_state_blink_slow() {
    testLed->setState(LedState::BLINK_SLOW);
    TEST_ASSERT_EQUAL(LedState::BLINK_SLOW, testLed->getState());
}

void test_set_state_blink_fast() {
    testLed->setState(LedState::BLINK_FAST);
    TEST_ASSERT_EQUAL(LedState::BLINK_FAST, testLed->getState());
}

void test_set_state_pulse() {
    testLed->setState(LedState::PULSE);
    TEST_ASSERT_EQUAL(LedState::PULSE, testLed->getState());
}

void test_on_method() {
    testLed->on();
    TEST_ASSERT_EQUAL(LedState::ON, testLed->getState());
}

void test_off_method() {
    testLed->on();
    testLed->off();
    TEST_ASSERT_EQUAL(LedState::OFF, testLed->getState());
}

void test_toggle_method() {
    testLed->off();
    testLed->toggle();
    TEST_ASSERT_EQUAL(LedState::ON, testLed->getState());
    
    testLed->toggle();
    TEST_ASSERT_EQUAL(LedState::OFF, testLed->getState());
}

void test_update_method_no_crash() {
    // Test that update doesn't crash
    testLed->setState(LedState::BLINK_SLOW);
    for (int i = 0; i < 10; i++) {
        testLed->update();
    }
    TEST_ASSERT_TRUE(true); // If we get here, no crash occurred
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_initial_state);
    RUN_TEST(test_set_state_on);
    RUN_TEST(test_set_state_off);
    RUN_TEST(test_set_state_blink_slow);
    RUN_TEST(test_set_state_blink_fast);
    RUN_TEST(test_set_state_pulse);
    RUN_TEST(test_on_method);
    RUN_TEST(test_off_method);
    RUN_TEST(test_toggle_method);
    RUN_TEST(test_update_method_no_crash);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}