#include "unity.h"
#include "StatusLed.h"

// Mock Arduino functions for testing
static unsigned long mockMillis = 0;
unsigned long millis() {
    return mockMillis;
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
    mockMillis = 0;
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
        mockMillis += 50;
        testLed->update();
    }
    TEST_ASSERT_TRUE(true); // If we get here, no crash occurred
}

void test_set_state_mqtt_active() {
    mockMillis = 1000;
    testLed->setState(LedState::MQTT_ACTIVE);
    TEST_ASSERT_EQUAL(LedState::MQTT_ACTIVE, testLed->getState());
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

void test_mqtt_flash_returns_to_on() {
    mockMillis = 1000;
    testLed->setState(LedState::ON);
    testLed->setState(LedState::MQTT_ACTIVE);
    TEST_ASSERT_EQUAL(LedState::MQTT_ACTIVE, testLed->getState());

    // After flash duration, should auto-transition back to ON
    mockMillis = 1000 + 200; // > 150ms flash duration
    testLed->update();
    TEST_ASSERT_EQUAL(LedState::ON, testLed->getState());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, testLed->getProgress());
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
    RUN_TEST(test_set_state_mqtt_active);
    RUN_TEST(test_progress_method);
    RUN_TEST(test_mqtt_flash_returns_to_on);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}