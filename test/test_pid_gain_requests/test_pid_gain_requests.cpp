#include "unity.h"
#include <cmath>

#include "Config.h"
#include "control/TemperatureController.h"

using Control::TemperatureController;

void setUp() {}
void tearDown() {}

// These tests drive the real Control::TemperatureController, which is what
// makes them worth having: the thing under test is the handover of a gain
// change from the web task to the control task, and a stand-in for that
// handover would be a stand-in for the entire bug it fixes.
//
// The loop takes its process value and its clock as parameters, so a tick with
// no data is `update(NAN, false, now)`. The request is consumed at the very top
// of update(), before the over-temperature shutoff, so a single such tick
// applies it even with no valid reading. Because the clock is injectable the
// cadence cases at the bottom can also say *which* computation first uses the
// new gains.

namespace {
    constexpr Control::PidGains TUNING = {1.5f, 0.002f, 0.0f};
}

void test_requested_gains_are_not_visible_before_a_tick() {
    Config::ConfigManager config;
    TemperatureController controller(config);

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
    TemperatureController controller(config);

    controller.requestGains(TUNING, 60);
    controller.update(NAN, false, 1000);

    const Control::PidGains gains = controller.getControlGains();
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, TUNING.kp, gains.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, TUNING.ki, gains.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, TUNING.kd, gains.kd);
}

// A request must not be serviced twice, which is what exchange() buys.
void test_request_is_consumed_exactly_once() {
    Config::ConfigManager config;
    TemperatureController controller(config);

    controller.requestGains(TUNING, 60);
    controller.update(NAN, false, 1000);

    // Move the stored gains behind the controller's back. A second tick that
    // re-serviced the stale request would pick these up.
    config.updateTuning(9.0f, 0.03f, 5.0f, 60);
    controller.update(NAN, false, 2000);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, TUNING.kp, controller.getControlGains().kp);
}

void test_applying_gains_suspends_the_controller() {
    Config::ConfigManager config;
    TemperatureController controller(config);

    controller.requestGains(TUNING, 60);
    controller.update(NAN, false, 1000);

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
    TemperatureController controller(config);

    // kp = 0 is refused and falls back; the rest are trustworthy.
    controller.requestGains(Control::PidGains{0.0f, 0.003f, 1.0f}, 60);
    controller.update(NAN, false, 1000);

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
    TemperatureController controller(config);

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
    TemperatureController controller(config);

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
    TemperatureController controller(config);

    const Control::PidGains before = controller.getControlGains();
    TEST_ASSERT_FALSE(controller.acceptAutotuneResult());
    controller.update(NAN, false, 1000);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, before.kp, controller.getControlGains().kp);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, Config::DEFAULT_PID_KP, config.getDeviceConfig().kp);
}

// --- Cadence ---
//
// Possible only because the clock is a parameter: these say on which tick a
// requested change actually reaches the computed output.

namespace {
    // A running loop: enabled, 22 °C setpoint, proportional-only gains and a
    // 60 s control interval, ticked once a second with the room 0.5 K below
    // target so the output is kp * 0.5 and therefore reads the gain directly.
    struct RunningLoop {
        Config::ConfigManager config;
        TemperatureController controller{config};

        RunningLoop(Control::PidGains gains) {
            config.updateTemperatureControlEnabled(true);
            config.updateTargetTemperature(22.0f);
            config.updateTuning(gains.kp, gains.ki, gains.kd, 60);
            controller.begin();
        }

        float tick(uint32_t nowMs) { return controller.update(21.5f, true, nowMs); }
    };
}

void test_gains_requested_mid_interval_are_used_by_the_next_computation() {
    RunningLoop loop(Control::PidGains{1.0f, 0.0f, 0.0f});

    // The baseline starts at zero, so the first computation is at 60 s.
    for (uint32_t t = 1000; t <= 60000; t += 1000) {
        loop.tick(t);
    }
    TEST_ASSERT_TRUE(loop.controller.isControlRunning());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, loop.controller.getControlOutput());

    // Web task doubles kp one second later. The tick that consumes the request
    // reports the new gains at once but does not compute: the decimation
    // baseline was reseated by the change, and the last output is held rather
    // than zeroed so the actuator sees no pulse.
    loop.controller.requestGains(Control::PidGains{2.0f, 0.0f, 0.0f}, 60);
    const float held = loop.tick(61000);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, loop.controller.getControlGains().kp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, held);
    TEST_ASSERT_FALSE(loop.controller.isControlRunning());

    // Still held for the rest of the interval measured from the change.
    for (uint32_t t = 62000; t <= 120000; t += 1000) {
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, loop.tick(t));
        TEST_ASSERT_FALSE(loop.controller.isControlRunning());
    }

    // 60 s after the change: the first computation under the new gains.
    const float computed = loop.tick(121000);
    TEST_ASSERT_TRUE(loop.controller.isControlRunning());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, computed);
}

void test_gain_change_mid_interval_restarts_proportional_only() {
    // ki large enough that the integral is clearly non-zero after two
    // computations one interval apart (0.5 K * 60 s * 0.01 = 0.3).
    RunningLoop loop(Control::PidGains{1.0f, 0.01f, 0.0f});

    for (uint32_t t = 1000; t <= 120000; t += 1000) {
        loop.tick(t);
    }
    TEST_ASSERT_TRUE(loop.controller.isControlRunning());
    TEST_ASSERT_TRUE(loop.controller.getControlIntegral() > 0.0f);

    // A gain change halfway through the next interval suspends the loop.
    loop.controller.requestGains(Control::PidGains{1.0f, 0.01f, 0.0f}, 60);
    loop.tick(150000);
    TEST_ASSERT_FALSE(loop.controller.isControlRunning());

    // Ticks until the interval since the change has elapsed do not compute.
    for (uint32_t t = 151000; t <= 209000; t += 1000) {
        loop.tick(t);
        TEST_ASSERT_FALSE(loop.controller.isControlRunning());
    }

    // The next computing tick is a bumpless restart: the integral accumulated
    // under the old gains is discarded and the output is the proportional term
    // alone, kp * 0.5.
    const float output = loop.tick(210000);
    TEST_ASSERT_TRUE(loop.controller.isControlRunning());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, loop.controller.getControlIntegral());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, output);
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
    RUN_TEST(test_gains_requested_mid_interval_are_used_by_the_next_computation);
    RUN_TEST(test_gain_change_mid_interval_restarts_proportional_only);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
