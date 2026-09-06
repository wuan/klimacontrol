#include "unity.h"
#include <cmath>

#include "Config.h"
#include "control/PidController.h"
#include "control/TemperatureController.h"

using Control::PidController;
using Control::PidGains;
using Control::TemperatureController;

void setUp() {}
void tearDown() {}

// These tests drive the real Control::PidController and, further down, the real
// Control::TemperatureController. They used to drive hand-copied stand-ins that
// lived in this file, which meant the suite could stay green while the shipped
// code was wrong. Both classes take their clock as a parameter precisely so
// they can be exercised here.
//
// One convention runs through the whole file: the FIRST update() after
// construction or suspend() is a bumpless restart. It reseats the timestamp, so
// its dt is zero and it contributes nothing to the integral or the derivative.
// Tests that care about accumulated terms therefore need one more tick than a
// naive reading suggests.

namespace {
    constexpr float WIDE_MIN = -10.0f;
    constexpr float WIDE_MAX = 10.0f;

    // A representative gain set and the output clamps the firmware ships, for
    // the pure-PidController cases below.
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
// The defect these cover: the control loop returns early whenever control is
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
// From here on the thing under test is the real Control::TemperatureController,
// configured the same way the firmware configures it: through a
// Config::ConfigManager. No sensor object exists anywhere in this file — the
// process value is pushed in through update(temperature, valid, nowMs), which is
// exactly how the Sensor Monitor task feeds it.
namespace {
    // Gains the loop fixture runs. `ki` is within Config::MAX_PID_KI (0.05), so
    // updateTuning() stores it as given rather than falling back; with an error
    // of 0.1 K it accumulates 0.06 per 60 s interval, which keeps successive
    // computations distinguishable without saturating the output.
    constexpr PidGains LOOP_GAINS = {2.0f, 0.01f, 0.5f};
    constexpr float SETPOINT = 22.0f;
    constexpr float SAFETY_MAX_C = 35.0f;
    constexpr float SAFETY_HYST_C = 1.0f;

    struct Fixture {
        Config::ConfigManager config;
        TemperatureController ctrl{config};

        explicit Fixture(uint16_t intervalS = 1, PidGains gains = LOOP_GAINS) {
            config.updateTemperatureControlEnabled(true);
            config.updateTargetTemperature(SETPOINT);
            config.updateTuning(gains.kp, gains.ki, gains.kd, intervalS);
            config.updateActuatorTiming(Config::DEFAULT_TPO_CYCLE_S, Config::DEFAULT_TPO_TRAVEL_S,
                                        SAFETY_MAX_C, SAFETY_HYST_C);
            // As setup() does: adopt the stored tuning before the first tick.
            ctrl.begin();
        }

        void setEnabled(bool enabled) { config.updateTemperatureControlEnabled(enabled); }

        // The real class exposes no computation counter, so a computation is
        // observed as "the loop is running and the returned output moved". A
        // held tick returns the previous output unchanged; a skipped tick
        // leaves the loop not running. Callers keep the error small enough
        // that successive computations produce distinct outputs.
        float lastSeen = 0.0f;
        unsigned computations = 0;

        float tick(float temperature, bool valid, uint32_t nowMs) {
            const float out = ctrl.update(temperature, valid, nowMs);
            if (ctrl.isControlRunning() && out != lastSeen) {
                ++computations;
            }
            lastSeen = out;
            return out;
        }
    };
}

void test_stored_output_is_positive_while_heating() {
    Fixture f;
    f.tick(18.0f, true, 1000);
    f.tick(18.0f, true, 2000);

    TEST_ASSERT_TRUE(f.ctrl.isControlActive());
}

void test_stored_output_cleared_when_data_becomes_invalid() {
    Fixture f;
    f.tick(18.0f, true, 1000);
    f.tick(18.0f, true, 2000);
    TEST_ASSERT_TRUE(f.ctrl.isControlActive());

    // Sensor drops off the bus: the real output is zero, so control must stop
    // reporting itself as active.
    f.tick(18.0f, false, 3000);

    TEST_ASSERT_FALSE(f.ctrl.isControlActive());
}

void test_stored_output_cleared_when_control_disabled() {
    Fixture f;
    f.tick(18.0f, true, 1000);
    f.tick(18.0f, true, 2000);
    TEST_ASSERT_TRUE(f.ctrl.isControlActive());

    f.setEnabled(false);
    f.tick(18.0f, true, 3000);

    TEST_ASSERT_FALSE(f.ctrl.isControlActive());
}

void test_stored_output_cleared_on_nan_reading() {
    Fixture f;
    f.tick(18.0f, true, 1000);
    f.tick(18.0f, true, 2000);
    TEST_ASSERT_TRUE(f.ctrl.isControlActive());

    f.tick(NAN, true, 3000);

    TEST_ASSERT_FALSE(f.ctrl.isControlActive());
}

void test_control_disabled_returns_zero() {
    Fixture f;
    f.setEnabled(false);

    float output = f.tick(20.0f, true, 1000);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, output);
}

void test_nan_sensor_reading_returns_zero() {
    Fixture f;
    f.tick(20.0f, true, 1000);

    float output = f.tick(NAN, true, 2000);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, output);
}

void test_loop_resumes_bumplessly_after_disabled_gap() {
    Fixture f;

    // Heating hard for a while.
    for (uint32_t t = 1000; t <= 10000; t += 1000) {
        f.tick(18.0f, true, t);
    }
    TEST_ASSERT_TRUE(f.ctrl.isControlActive());

    // Switched off for an hour.
    f.setEnabled(false);
    for (uint32_t t = 11000; t <= 3600000; t += 60000) {
        f.tick(21.9f, true, t);
    }
    TEST_ASSERT_FALSE(f.ctrl.isControlActive());

    // Back on, 0.1 C below target.
    f.setEnabled(true);
    float output = f.tick(21.9f, true, 3660000);

    TEST_ASSERT_TRUE(output < OUTPUT_MAX);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.2f, output);
}

// --- Control loop decimation ---

void test_pid_computes_once_per_control_interval() {
    Fixture f(60);

    // 180 one-second sensor ticks with a 60 s control interval.
    for (uint32_t t = 1000; t <= 180000; t += 1000) {
        f.tick(21.9f, true, t);
    }

    // The baseline starts at zero, so the computations fall at 60 s, 120 s and
    // 180 s — one interval after boot, not on the first tick.
    TEST_ASSERT_EQUAL_UINT(3, f.computations);
}

// The point of holding rather than zeroing: the actuator reads the stored
// output continuously from the Network task, so a zeroed non-computing tick
// would make the valve see one pulse per interval.
void test_output_is_held_between_computations() {
    Fixture f(60);

    // The baseline starts at zero, so the first computation is one interval
    // after boot rather than on the first tick.
    for (uint32_t t = 1000; t <= 60000; t += 1000) {
        f.tick(21.9f, true, t);
    }
    const float computed = f.ctrl.getControlOutput();
    TEST_ASSERT_EQUAL_UINT(1, f.computations);
    TEST_ASSERT_TRUE(computed > 0.0f);

    for (uint32_t t = 61000; t <= 119000; t += 1000) {
        const float held = f.tick(21.9f, true, t);
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, computed, held);
        TEST_ASSERT_TRUE(f.ctrl.isControlActive());
    }
    TEST_ASSERT_EQUAL_UINT(1, f.computations);
}

// The integral must accumulate across the interval. If a non-computing tick
// suspended the controller, every computation would be a bumpless restart and
// ki would have no effect at all whatever it was set to.
void test_integral_accumulates_across_the_interval() {
    Fixture f(60);

    for (uint32_t t = 1000; t <= 180000; t += 1000) {
        f.tick(18.0f, true, t);
    }

    TEST_ASSERT_TRUE(f.ctrl.isControlRunning());
    TEST_ASSERT_TRUE(f.ctrl.getControlIntegral() > 0.0f);
}

void test_safety_shutoff_not_delayed_by_control_interval() {
    Fixture f(60);

    for (uint32_t t = 1000; t <= 60000; t += 1000) {
        f.tick(18.0f, true, t);
    }
    TEST_ASSERT_TRUE(f.ctrl.isControlActive());
    TEST_ASSERT_FALSE(f.ctrl.isSafetyShutoffEngaged());

    // The very next sensor tick, 59 s before the PID would next compute.
    const float output = f.tick(40.0f, true, 61000);

    TEST_ASSERT_TRUE(f.ctrl.isSafetyShutoffEngaged());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, output);
    TEST_ASSERT_FALSE(f.ctrl.isControlActive());
    TEST_ASSERT_FALSE(f.ctrl.isHeatingPermitted());
}

void test_safety_shutoff_releases_on_a_sensor_tick() {
    Fixture f(60);
    f.tick(40.0f, true, 1000);
    TEST_ASSERT_TRUE(f.ctrl.isSafetyShutoffEngaged());

    // Hysteresis: still latched just below the limit, released a band below it.
    f.tick(34.5f, true, 2000);
    TEST_ASSERT_TRUE(f.ctrl.isSafetyShutoffEngaged());

    f.tick(33.0f, true, 3000);
    TEST_ASSERT_FALSE(f.ctrl.isSafetyShutoffEngaged());
}

void test_autotuner_ticks_every_sensor_cycle() {
    Fixture f(60);

    // Requested from the web task; honoured by the loop on its next tick.
    TEST_ASSERT_TRUE(f.ctrl.requestAutotuneStart());
    TEST_ASSERT_FALSE(f.ctrl.isAutotuneActive());

    f.tick(SETPOINT, true, 1000);
    TEST_ASSERT_TRUE(f.ctrl.isAutotuneActive());
    TEST_ASSERT_EQUAL_UINT32(0, f.ctrl.getAutotuneElapsedMs(1000));

    // Every one of the 120 ticks advances the run by one second, not one in
    // sixty: a coarser sampling would under-estimate the oscillation amplitude
    // and over-estimate Ku. The PID never computes while the run owns the
    // output.
    for (uint32_t t = 2000; t <= 120000; t += 1000) {
        f.tick(SETPOINT, true, t);
        TEST_ASSERT_TRUE(f.ctrl.isAutotuneActive());
        TEST_ASSERT_EQUAL_UINT32(t - 1000, f.ctrl.getAutotuneElapsedMs(t));
        TEST_ASSERT_FALSE(f.ctrl.isControlRunning());
    }
    TEST_ASSERT_EQUAL_UINT(0, f.computations);
}

void test_decimation_survives_millis_rollover() {
    Fixture f(60);

    // Last computation 1 s before the wrap.
    const uint32_t beforeWrap = 0xFFFFFC18u;
    f.tick(21.9f, true, beforeWrap);
    TEST_ASSERT_EQUAL_UINT(1, f.computations);

    // 30 s past the wrap: not yet eligible. Signed arithmetic here would see a
    // vast negative elapsed time; a naive `now >= last + interval` would stall
    // for the length of the counter.
    f.tick(21.9f, true, 29000u);
    TEST_ASSERT_EQUAL_UINT(1, f.computations);

    // 60 s past the last computation, across the wrap.
    f.tick(21.9f, true, 59000u);
    TEST_ASSERT_EQUAL_UINT(2, f.computations);
}

// A skip path reseats the baseline, so the interval stays a genuine floor on
// the spacing between computations rather than being satisfied by a gap the
// controller spent suspended.
void test_resumed_controller_computes_on_first_eligible_tick() {
    Fixture f(60);

    for (uint32_t t = 1000; t <= 60000; t += 1000) {
        f.tick(18.0f, true, t);
    }
    TEST_ASSERT_EQUAL_UINT(1, f.computations);

    // Switched off for an hour.
    f.setEnabled(false);
    for (uint32_t t = 61000; t <= 3600000; t += 1000) {
        f.tick(18.0f, true, t);
    }
    TEST_ASSERT_EQUAL_UINT(1, f.computations);

    // Back on. The baseline was reseated by the last skipped tick, so the
    // first tick after resuming is not yet eligible.
    f.setEnabled(true);
    f.tick(18.0f, true, 3601000);
    TEST_ASSERT_EQUAL_UINT(1, f.computations);
    TEST_ASSERT_FALSE(f.ctrl.isControlRunning());

    // One interval after the resume it computes, and bumplessly: the
    // proportional term alone, with no integral charged from the hour off.
    const float output = f.tick(21.9f, true, 3660001);
    TEST_ASSERT_EQUAL_UINT(2, f.computations);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.2f, output);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, f.ctrl.getControlIntegral());
}

// An interval of 1 s is the documented way to keep the pre-decimation
// behaviour, so it must not accidentally skip a tick.
void test_one_second_interval_computes_every_tick() {
    Fixture f(1);

    for (uint32_t t = 1000; t <= 10000; t += 1000) {
        f.tick(21.9f, true, t);
    }

    TEST_ASSERT_EQUAL_UINT(10, f.computations);
}

// --- Decoupled from sensor acquisition ---
//
// The scenarios from the "Control loop is decoupled from sensor acquisition"
// requirement in the temperature-control spec.

void test_loop_computes_without_any_sensor_object() {
    // Nothing in this translation unit constructs a SensorController or a
    // Sensor::Sensor; the loop's only dependency is the ConfigManager.
    Fixture f;

    const float output = f.tick(18.0f, true, 1000);

    TEST_ASSERT_TRUE(output > 0.0f);
    TEST_ASSERT_TRUE(f.ctrl.isControlRunning());
}

void test_heating_not_permitted_before_first_tick() {
    Fixture f;

    // Control is enabled and nothing has engaged the shutoff, yet no tick has
    // reported a valid reading: false is the safe default.
    TEST_ASSERT_TRUE(f.ctrl.isControlEnabled());
    TEST_ASSERT_FALSE(f.ctrl.isSafetyShutoffEngaged());
    TEST_ASSERT_FALSE(f.ctrl.isHeatingPermitted());
}

void test_heating_permission_follows_last_ticks_inputs() {
    Fixture f;

    f.tick(18.0f, true, 1000);
    TEST_ASSERT_TRUE(f.ctrl.isHeatingPermitted());

    // The last tick had no valid reading. Whatever the sensor cache holds now
    // is irrelevant: the answer is what the loop was last told.
    f.tick(18.0f, false, 2000);
    TEST_ASSERT_FALSE(f.ctrl.isHeatingPermitted());

    f.tick(NAN, true, 3000);
    TEST_ASSERT_FALSE(f.ctrl.isHeatingPermitted());

    // A valid reading below the release band re-permits on that same tick.
    f.tick(18.0f, true, 4000);
    TEST_ASSERT_TRUE(f.ctrl.isHeatingPermitted());
}

void test_cadence_is_testable_natively() {
    Fixture f(60);

    // One-second ticks over two full control intervals. The decimation
    // baseline starts at zero, so the computations fall at 60 s and 120 s.
    for (uint32_t t = 1000; t <= 121000; t += 1000) {
        f.tick(21.9f, true, t);
    }

    TEST_ASSERT_EQUAL_UINT(2, f.computations);
    TEST_ASSERT_TRUE(f.ctrl.isControlRunning());
    // Two computations, one interval apart: P plus one interval's integral.
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.26f, f.ctrl.getControlOutput());
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

    RUN_TEST(test_pid_computes_once_per_control_interval);
    RUN_TEST(test_output_is_held_between_computations);
    RUN_TEST(test_integral_accumulates_across_the_interval);
    RUN_TEST(test_safety_shutoff_not_delayed_by_control_interval);
    RUN_TEST(test_safety_shutoff_releases_on_a_sensor_tick);
    RUN_TEST(test_autotuner_ticks_every_sensor_cycle);
    RUN_TEST(test_decimation_survives_millis_rollover);
    RUN_TEST(test_resumed_controller_computes_on_first_eligible_tick);
    RUN_TEST(test_one_second_interval_computes_every_tick);

    RUN_TEST(test_loop_computes_without_any_sensor_object);
    RUN_TEST(test_heating_not_permitted_before_first_tick);
    RUN_TEST(test_heating_permission_follows_last_ticks_inputs);
    RUN_TEST(test_cadence_is_testable_natively);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}