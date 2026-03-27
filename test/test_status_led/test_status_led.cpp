#include "unity.h"
#include "StatusLed.h"

StatusLed* testLed;

void setUp() {
    testLed = new StatusLed();
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

void test_set_state_startup() {
    testLed->setState(LedState::STARTUP);
    TEST_ASSERT_EQUAL(LedState::STARTUP, testLed->getState());
}

void test_set_state_transmit_data() {
    testLed->setState(LedState::TRANSMIT_DATA);
    TEST_ASSERT_EQUAL(LedState::TRANSMIT_DATA, testLed->getState());
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
    testLed->setState(LedState::STARTUP);
    for (int i = 0; i < 10; i++) {
        testLed->update();
    }
    TEST_ASSERT_TRUE(true); // If we get here, no crash occurred
}

void test_set_state_transmit_data_from_on() {
    testLed->setState(LedState::ON);
    testLed->setState(LedState::TRANSMIT_DATA);
    TEST_ASSERT_EQUAL(LedState::TRANSMIT_DATA, testLed->getState());
}

void test_progress_method() {
    testLed->setProgress(0.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, testLed->getProgress());

    // Clamps to 0.0-1.0
    testLed->setProgress(-0.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, testLed->getProgress());

    testLed->setProgress(2.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, testLed->getProgress());
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_initial_state);
    RUN_TEST(test_set_state_on);
    RUN_TEST(test_set_state_off);
    RUN_TEST(test_set_state_startup);
    RUN_TEST(test_set_state_transmit_data);
    RUN_TEST(test_on_method);
    RUN_TEST(test_off_method);
    RUN_TEST(test_toggle_method);
    RUN_TEST(test_update_method_no_crash);
    RUN_TEST(test_set_state_transmit_data_from_on);
    RUN_TEST(test_progress_method);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}