#include "unity.h"
#include <cmath>

#include "control/PidController.h"

using Control::PidController;
using Control::PidGains;

void setUp() {}
void tearDown() {}

// These tests drive the real Control::PidController. They used to drive a
// hand-copied reimplementation of SensorController::updateControl() that lived
// in this file, which meant the suite could stay green while the shipped PID
// was wrong. The controller takes its clock as a parameter precisely so it can
// be exercised here.
//
// One convention runs through the whole file: the FIRST update() after
// construction or suspend() is a bumpless restart. It reseats the timestamp, so
// its dt is zero and it contributes nothing to the integral or the derivative.
// Tests that care about accumulated terms therefore need one more tick than a
// naive reading suggests.

namespace {
    constexpr float WIDE_MIN = -10.0f;
    constexpr float WIDE_MAX = 10.0f;

    // The gains and clamps the firmware actually ships (SensorController.cpp).
    constexpr PidGains SHIPPED_GAINS = {2.0f, 0.1f, 0.5f};
    constexpr float OUTPUT_MIN = 0.0f;
    constexpr float OUTPUT_MAX = 1.0f;
}

// --- Individual terms ---

void test_pid_proportional_term_only() {
    PidController pid({1.0f, 0.0f, 0.0f}, WIDE_MIN, WIDE_MAX);

    pid.update(2.0f, 1000);
    float output = pid.update(2.0f, 2000);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, output);
}

void test_pid_integral_term_accumulation() {
    PidController pid({0.0f, 1.0f, 0.0f}, WIDE_MIN, WIDE_MAX);

    pid.update(1.0f, 1000); // restart tick, dt = 0, contributes nothing
    pid.update(1.0f, 2000); // integral = 1.0
    float output = pid.update(1.0f, 3000);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, output);
}

void test_pid_derivative_term_positive_change() {
    PidController pid({0.0f, 0.0f, 1.0f}, WIDE_MIN, WIDE_MAX);

    pid.update(1.0f, 1000);
    float output = pid.update(2.0f, 2000);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, output);
}

void test_pid_derivative_guard_dt_zero() {
    PidController pid({0.0f, 0.0f, 1.0f}, WIDE_MIN, WIDE_MAX);

    pid.update(1.0f, 1000);
    pid.update(2.0f, 2000);

    // Two ticks at the same timestamp: dividing by dt would be a division by
    // zero. The guard must keep the output finite rather than NaN or Inf.
    float output = pid.update(2.0f, 2000);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, output);
    TEST_ASSERT_FALSE(std::isnan(output));
    TEST_ASSERT_FALSE(std::isinf(output));
}

void test_pid_full_calculation() {
    PidController pid(SHIPPED_GAINS, WIDE_MIN, WIDE_MAX);

    pid.update(2.0f, 1000); // restart
    pid.update(2.0f, 2000); // integral = 0.1 * 2 * 1 = 0.2
    float output = pid.update(2.0f, 3000);

    // P = 2.0 * 2 = 4.0, I = 0.4 after two accumulating ticks, D = 0 (error
    // steady).
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.4f, output);
}

// --- Clamping ---

void test_pid_output_clamped_to_max() {
    PidController pid({10.0f, 0.0f, 0.0f}, OUTPUT_MIN, OUTPUT_MAX);

    pid.update(22.0f, 1000);
    float output = pid.update(22.0f, 2000);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, output);
}

void test_pid_output_clamped_to_min() {
    PidController pid({2.0f, 0.0f, 0.0f}, OUTPUT_MIN, OUTPUT_MAX);

    // Negative error: the room is above the setpoint and this controller can
    // only heat, so the output floor is zero rather than a cooling demand.
    pid.update(-10.0f, 1000);
    float output = pid.update(-10.0f, 2000);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, output);
}

void test_pid_anti_windup() {
    PidController pid({0.0f, 1.0f, 0.0f}, OUTPUT_MIN, OUTPUT_MAX);

    for (int i = 0; i < 100; i++) {
        pid.update(100.0f, 1000 + (i + 1) * 1000);
    }

    float output = pid.update(100.0f, 201000);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, output);
    // The accumulator itself must be clamped, not merely the output. If it were
    // allowed to bank 100x its useful value, returning to setpoint would take
    // as long to unwind as it took to build.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, OUTPUT_MAX, pid.getIntegral());
}

void test_control_output_saturation() {
    PidController pid({2.0f, 0.0f, 0.0f}, OUTPUT_MIN, OUTPUT_MAX);

    pid.update(22.0f, 1000);
    float output = pid.update(22.0f, 2000);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, output);
}

// --- Setpoint changes ---

void test_setpoint_increase() {
    PidController pid({1.0f, 0.0f, 0.0f}, WIDE_MIN, WIDE_MAX);

    pid.update(0.0f, 1000);  // at setpoint
    pid.update(5.0f, 2000);  // setpoint raised by 5
    float outputAfter = pid.update(5.0f, 3000);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, outputAfter);
}

void test_setpoint_decrease() {
    PidController pid({1.0f, 0.0f, 0.0f}, WIDE_MIN, WIDE_MAX);

    pid.update(0.0f, 1000);
    pid.update(-5.0f, 2000);
    float outputAfter = pid.update(-5.0f, 3000);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.0f, outputAfter);
}

// --- Bumpless restart ---
//
// The defect these cover: updateControl() returns early whenever control is
// disabled, sensor data is invalid, or the device has just booted, and those
// early returns used to leave the last-computation timestamp untouched. The
// next tick that did run saw a dt spanning the whole gap and slammed the
// integral into its clamp — full output with the room a tenth of a degree off
// target.

void test_first_tick_after_construction_does_not_charge_integral() {
    PidController pid({0.0f, 1.0f, 0.0f}, OUTPUT_MIN, OUTPUT_MAX);

    // Device has been up for an hour before control first runs. That uptime is
    // not elapsed control time and must not be integrated.
    float output = pid.update(0.1f, 3600000);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pid.getIntegral());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, output);
}

void test_resume_after_long_disabled_period_is_not_saturated() {
    PidController pid(SHIPPED_GAINS, OUTPUT_MIN, OUTPUT_MAX);

    // Run normally for a few ticks with a large error, so there is real state
    // to discard.
    for (uint32_t t = 1000; t <= 5000; t += 1000) {
        pid.update(4.0f, t);
    }
    TEST_ASSERT_TRUE(pid.getIntegral() > 0.0f);

    // Control switched off for an hour.
    pid.suspend();

    // Re-enabled with the room 0.1 C below target. Without the reseat, dt would
    // be ~3600 s and the integral would clamp to OUTPUT_MAX immediately.
    float output = pid.update(0.1f, 3605000);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pid.getIntegral());
    // P only: 2.0 * 0.1 = 0.2, nowhere near saturation.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.2f, output);
    TEST_ASSERT_TRUE(output < OUTPUT_MAX);
}

void test_resume_after_sensor_dropout_is_not_saturated() {
    PidController pid(SHIPPED_GAINS, OUTPUT_MIN, OUTPUT_MAX);

    pid.update(2.0f, 1000);
    pid.update(2.0f, 2000);

    // Sensor off the bus for five minutes while control stayed enabled. No user
    // action involved — this path must self-heal too.
    for (uint32_t t = 3000; t <= 300000; t += 1000) {
        pid.suspend();
    }

    float output = pid.update(0.1f, 302000);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pid.getIntegral());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.2f, output);
}

void test_resumed_tick_has_no_derivative_spike() {
    PidController pid({0.0f, 0.0f, 1.0f}, WIDE_MIN, WIDE_MAX);

    pid.update(1.0f, 1000);
    pid.update(1.0f, 2000);
    pid.suspend();

    // A resumed tick has dt == 0, so the derivative term is suppressed by the
    // guard. Were it computed against the pre-suspend error it would be a
    // meaningless spike across a gap the controller did not observe.
    float output = pid.update(8.0f, 60000);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, output);
}

void test_consecutive_running_ticks_do_not_reset() {
    PidController pid({0.0f, 1.0f, 0.0f}, WIDE_MIN, WIDE_MAX);

    pid.update(1.0f, 1000);
    pid.update(1.0f, 2000);
    float integralAfterTwo = pid.getIntegral();
    float output = pid.update(1.0f, 3000);

    // Accumulation continues across ticks; only a suspend() interrupts it.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, integralAfterTwo);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, output);
}

void test_is_running_tracks_suspension() {
    PidController pid(SHIPPED_GAINS, OUTPUT_MIN, OUTPUT_MAX);

    TEST_ASSERT_FALSE(pid.isRunning());

    pid.update(1.0f, 1000);
    TEST_ASSERT_TRUE(pid.isRunning());

    pid.suspend();
    TEST_ASSERT_FALSE(pid.isRunning());

    pid.update(1.0f, 2000);
    TEST_ASSERT_TRUE(pid.isRunning());
}

void test_repeated_suspend_is_idempotent() {
    PidController pid(SHIPPED_GAINS, OUTPUT_MIN, OUTPUT_MAX);

    pid.update(2.0f, 1000);
    // The control loop calls suspend() on every tick it skips, not once per
    // transition, so this is the normal case rather than an edge case.
    for (int i = 0; i < 50; i++) {
        pid.suspend();
    }

    float output = pid.update(0.1f, 60000);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pid.getIntegral());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.2f, output);
}

void test_instances_do_not_share_state() {
    // Regression guard: the accumulators used to be function-local statics, so
    // every controller in the process shared one set. Harmless on the firmware
    // with its single instance, but it leaked state between native test cases.
    PidController a({0.0f, 1.0f, 0.0f}, WIDE_MIN, WIDE_MAX);
    PidController b({0.0f, 1.0f, 0.0f}, WIDE_MIN, WIDE_MAX);

    a.update(1.0f, 1000);
    a.update(1.0f, 2000);
    a.update(1.0f, 3000);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, a.getIntegral());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, b.getIntegral());
    TEST_ASSERT_FALSE(b.isRunning());
}

void test_millis_rollover_does_not_produce_huge_dt() {
    PidController pid({0.0f, 1.0f, 0.0f}, WIDE_MIN, WIDE_MAX);

    // Straddle the ~49.7 day wrap. Unsigned subtraction must still yield the
    // true 1 s interval rather than ~4.29e6 s.
    const uint32_t beforeWrap = 0xFFFFFC18u; // 1000 ms before wrap
    pid.update(1.0f, beforeWrap);
    float output = pid.update(1.0f, beforeWrap + 1000u);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, output);
}

// --- Control loop gating ---
//
// A thin stand-in for SensorController::updateControl()'s enable/validity
// gating and its stored-output bookkeeping, which cannot be constructed here
// without a full ConfigManager and sensor fixture. Unlike the PID mirror this
// replaced, it copies no arithmetic — it drives the real controller — so there
// is no duplicated formula to drift out of sync.
struct ControlLoop {
    PidController pid{SHIPPED_GAINS, OUTPUT_MIN, OUTPUT_MAX};
    float lastControlOutput = 0.0f;

    bool isControlActive() const { return lastControlOutput > 0.0f; }

    float updateControl(bool controlEnabled, bool dataValid, float currentTemp, float targetTemp,
                        uint32_t nowMs) {
        if (!controlEnabled || !dataValid || std::isnan(currentTemp)) {
            pid.suspend();
            lastControlOutput = 0.0f;
            return 0.0f;
        }
        lastControlOutput = pid.update(targetTemp - currentTemp, nowMs);
        return lastControlOutput;
    }
};

void test_stored_output_is_positive_while_heating() {
    ControlLoop c;
    c.updateControl(true, true, 18.0f, 22.0f, 1000);
    c.updateControl(true, true, 18.0f, 22.0f, 2000);

    TEST_ASSERT_TRUE(c.isControlActive());
}

void test_stored_output_cleared_when_data_becomes_invalid() {
    ControlLoop c;
    c.updateControl(true, true, 18.0f, 22.0f, 1000);
    c.updateControl(true, true, 18.0f, 22.0f, 2000);
    TEST_ASSERT_TRUE(c.isControlActive());

    // Sensor drops off the bus: the real output is zero, so control must stop
    // reporting itself as active.
    c.updateControl(true, false, 18.0f, 22.0f, 3000);

    TEST_ASSERT_FALSE(c.isControlActive());
}

void test_stored_output_cleared_when_control_disabled() {
    ControlLoop c;
    c.updateControl(true, true, 18.0f, 22.0f, 1000);
    c.updateControl(true, true, 18.0f, 22.0f, 2000);
    TEST_ASSERT_TRUE(c.isControlActive());

    c.updateControl(false, true, 18.0f, 22.0f, 3000);

    TEST_ASSERT_FALSE(c.isControlActive());
}

void test_stored_output_cleared_on_nan_reading() {
    ControlLoop c;
    c.updateControl(true, true, 18.0f, 22.0f, 1000);
    c.updateControl(true, true, 18.0f, 22.0f, 2000);
    TEST_ASSERT_TRUE(c.isControlActive());

    c.updateControl(true, true, NAN, 22.0f, 3000);

    TEST_ASSERT_FALSE(c.isControlActive());
}

void test_control_disabled_returns_zero() {
    ControlLoop c;

    float output = c.updateControl(false, true, 20.0f, 22.0f, 1000);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, output);
}

void test_nan_sensor_reading_returns_zero() {
    ControlLoop c;
    c.updateControl(true, true, 20.0f, 22.0f, 1000);

    float output = c.updateControl(true, true, NAN, 22.0f, 2000);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, output);
}

void test_loop_resumes_bumplessly_after_disabled_gap() {
    ControlLoop c;

    // Heating hard for a while.
    for (uint32_t t = 1000; t <= 10000; t += 1000) {
        c.updateControl(true, true, 18.0f, 22.0f, t);
    }
    TEST_ASSERT_TRUE(c.isControlActive());

    // Switched off for an hour.
    for (uint32_t t = 11000; t <= 3600000; t += 60000) {
        c.updateControl(false, true, 21.9f, 22.0f, t);
    }
    TEST_ASSERT_FALSE(c.isControlActive());

    // Back on, 0.1 C below target.
    float output = c.updateControl(true, true, 21.9f, 22.0f, 3660000);

    TEST_ASSERT_TRUE(output < OUTPUT_MAX);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.2f, output);
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_pid_proportional_term_only);
    RUN_TEST(test_pid_integral_term_accumulation);
    RUN_TEST(test_pid_derivative_term_positive_change);
    RUN_TEST(test_pid_derivative_guard_dt_zero);
    RUN_TEST(test_pid_full_calculation);
    RUN_TEST(test_pid_output_clamped_to_max);
    RUN_TEST(test_pid_output_clamped_to_min);
    RUN_TEST(test_pid_anti_windup);
    RUN_TEST(test_control_output_saturation);
    RUN_TEST(test_setpoint_increase);
    RUN_TEST(test_setpoint_decrease);
    RUN_TEST(test_first_tick_after_construction_does_not_charge_integral);
    RUN_TEST(test_resume_after_long_disabled_period_is_not_saturated);
    RUN_TEST(test_resume_after_sensor_dropout_is_not_saturated);
    RUN_TEST(test_resumed_tick_has_no_derivative_spike);
    RUN_TEST(test_consecutive_running_ticks_do_not_reset);
    RUN_TEST(test_is_running_tracks_suspension);
    RUN_TEST(test_repeated_suspend_is_idempotent);
    RUN_TEST(test_instances_do_not_share_state);
    RUN_TEST(test_millis_rollover_does_not_produce_huge_dt);
    RUN_TEST(test_stored_output_is_positive_while_heating);
    RUN_TEST(test_stored_output_cleared_when_data_becomes_invalid);
    RUN_TEST(test_stored_output_cleared_when_control_disabled);
    RUN_TEST(test_stored_output_cleared_on_nan_reading);
    RUN_TEST(test_control_disabled_returns_zero);
    RUN_TEST(test_nan_sensor_reading_returns_zero);
    RUN_TEST(test_loop_resumes_bumplessly_after_disabled_gap);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}