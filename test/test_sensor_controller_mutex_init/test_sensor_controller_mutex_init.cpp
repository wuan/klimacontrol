#include "Config.h"
#include "SensorController.h"
#include "StatusLed.h"
#include "unity.h"

void setUp() {}
void tearDown() {}

// Construction with a null StatusLed* must succeed on native (the failure path
// is `#ifdef ARDUINO`-gated and only restarts on the device).
void test_construct_with_null_led_does_not_crash() {
    Config::ConfigManager config;
    SensorController controller(config, nullptr);
    TEST_ASSERT_FALSE(controller.didFailMutexInit());
}

// Construction with a non-null StatusLed* must also succeed.
void test_construct_with_valid_led_does_not_crash() {
    Config::ConfigManager config;
    StatusLed led;
    SensorController controller(config, &led);
    TEST_ASSERT_FALSE(controller.didFailMutexInit());
    TEST_ASSERT_EQUAL(LedState::OFF, led.getState());
}

// The didFailMutexInit accessor is unconditionally defined and observable
// from both native and ARDUINO builds.
void test_did_fail_mutex_init_returns_false_on_successful_init() {
    Config::ConfigManager config;
    SensorController controller(config, nullptr);
    TEST_ASSERT_EQUAL(false, controller.didFailMutexInit());
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_construct_with_null_led_does_not_crash);
    RUN_TEST(test_construct_with_valid_led_does_not_crash);
    RUN_TEST(test_did_fail_mutex_init_returns_false_on_successful_init);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
