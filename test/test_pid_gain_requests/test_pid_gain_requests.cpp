#include "unity.h"
#include <cmath>

#include "Config.h"
#include "SensorController.h"

void setUp() {}
void tearDown() {}

// These tests drive the real SensorController, which is what makes them worth
// having: the thing under test is the handover of a gain change from the web
// task to the control task, and a stand-in for that handover would be a
// stand-in for the entire bug it fixes.
//
// updateControl() takes its time from millis() rather than a parameter, so
// these cases assert only on what one tick does, never on cadence — the
// decimation arithmetic is covered in test_temperature_control against an
// injectable clock. The request is consumed at the very top of updateControl(),
// before the over-temperature shutoff, so a single tick applies it even with no
// sensors attached and no valid reading.

namespace {
    constexpr Control::PidGains TUNING = {1.5f, 0.002f, 0.0f};
}

void test_requested_gains_are_not_visible_before_a_tick() {
    Config::ConfigManager config;
    SensorController controller(config, nullptr);

    const Control::PidGains before = controller.getControlGains();
    controller.requestGains(TUNING, 60);

    // Persisted immediately — that is safe on the web task — but the running
    // controller must not have moved.
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, TUNING.kp, config.getDeviceConfig().kp);
    const Control::PidGains after = controller.getControlGains();
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, before.kp, after.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, before.ki, after.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, before.kd, after.kd);
}

void test_gains_are_in_force_after_one_tick() {
    Config::ConfigManager config;
    SensorController controller(config, nullptr);

    controller.requestGains(TUNING, 60);
    controller.updateControl();

    const Control::PidGains gains = controller.getControlGains();
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, TUNING.kp, gains.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, TUNING.ki, gains.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, TUNING.kd, gains.kd);
}

// A request must not be serviced twice, which is what exchange() buys.
void test_request_is_consumed_exactly_once() {
    Config::ConfigManager config;
    SensorController controller(config, nullptr);

    controller.requestGains(TUNING, 60);
    controller.updateControl();

    // Move the stored gains behind the controller's back. A second tick that
    // re-serviced the stale request would pick these up.
    config.updateTuning(9.0f, 0.03f, 5.0f, 60);
    controller.updateControl();

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, TUNING.kp, controller.getControlGains().kp);
}

void test_applying_gains_suspends_the_controller() {
    Config::ConfigManager config;
    SensorController controller(config, nullptr);

    controller.requestGains(TUNING, 60);
    controller.updateControl();

    // An integral accumulated under the old gains means something else under
    // the new ones, so the change is a discontinuity and the next computing
    // tick has to restart bumplessly.
    TEST_ASSERT_FALSE(controller.isControlRunning());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, controller.getControlIntegral());
}

// The values that reach the controller are the ones that were persisted, not
// the ones that were asked for: updateTuning() falls back per field, and a
// controller running gains that would not survive a restart is exactly the
// divergence this change exists to remove.
void test_gains_in_force_match_what_was_stored() {
    Config::ConfigManager config;
    SensorController controller(config, nullptr);

    // kp = 0 is refused and falls back; the rest are trustworthy.
    controller.requestGains(Control::PidGains{0.0f, 0.003f, 1.0f}, 60);
    controller.updateControl();

    const Control::PidGains gains = controller.getControlGains();
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KP, gains.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, config.getDeviceConfig().kp, gains.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0.003f, gains.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, gains.kd);
}

// A fresh device runs the documented defaults, and in particular kd = 0 rather
// than the 0.5 that used to be compiled in.
void test_default_gains_are_in_force_on_a_fresh_controller() {
    Config::ConfigManager config;
    SensorController controller(config, nullptr);

    const Control::PidGains gains = controller.getControlGains();
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KP, gains.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, Config::DEFAULT_PID_KI, gains.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, gains.kd);
}

// begin() is what makes a stored tuning survive a restart: the controller is a
// global constructed before config.begin() has read NVS, so the constructor can
// only ever see the compiled-in defaults.
void test_stored_gains_are_in_force_after_begin() {
    Config::ConfigManager config;
    SensorController controller(config, nullptr);

    config.updateTuning(TUNING.kp, TUNING.ki, TUNING.kd, 120);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KP, controller.getControlGains().kp);

    controller.begin();

    const Control::PidGains gains = controller.getControlGains();
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, TUNING.kp, gains.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, TUNING.ki, gains.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, TUNING.kd, gains.kd);
}

// Acceptance is explicit: nothing may adopt a derived result on its own, and
// with no converged run there is nothing to accept.
void test_accept_without_a_converged_result_is_refused() {
    Config::ConfigManager config;
    SensorController controller(config, nullptr);

    const Control::PidGains before = controller.getControlGains();
    TEST_ASSERT_FALSE(controller.acceptAutotuneResult());
    controller.updateControl();

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, before.kp, controller.getControlGains().kp);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KP, config.getDeviceConfig().kp);
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_requested_gains_are_not_visible_before_a_tick);
    RUN_TEST(test_gains_are_in_force_after_one_tick);
    RUN_TEST(test_request_is_consumed_exactly_once);
    RUN_TEST(test_applying_gains_suspends_the_controller);
    RUN_TEST(test_gains_in_force_match_what_was_stored);
    RUN_TEST(test_default_gains_are_in_force_on_a_fresh_controller);
    RUN_TEST(test_stored_gains_are_in_force_after_begin);
    RUN_TEST(test_accept_without_a_converged_result_is_refused);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
