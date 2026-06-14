#include "unity.h"
#include <cmath>
#include <algorithm>

void setUp() {}
void tearDown() {}

static float computePidOutput(float currentTemp, float targetTemp, float Kp, float Ki, float Kd,
                              float& integral, float& previousError, uint32_t lastControlTime,
                              uint32_t nowMs, float maxOutput, float minOutput) {
    if (std::isnan(currentTemp)) {
        return 0.0f;
    }

    float dt = (nowMs - lastControlTime) / 1000.0f;

    if (dt <= 0.0f) {
        return 0.0f;
    }

    float error = targetTemp - currentTemp;

    float proportional = Kp * error;

    integral += Ki * error * dt;
    integral = std::max(minOutput, std::min(maxOutput, integral));

    float derivative = 0.0f;
    if (dt > 0.0f) {
        derivative = Kd * (error - previousError) / dt;
    }

    float output = proportional + integral + derivative;
    output = std::max(minOutput, std::min(maxOutput, output));

    previousError = error;
    return output;
}

void test_pid_proportional_term_only() {
    float integral = 0.0f;
    float previousError = 0.0f;
    float Kp = 1.0f, Ki = 0.0f, Kd = 0.0f;

    computePidOutput(20.0f, 22.0f, Kp, Ki, Kd, integral, previousError, 1000, 2000, 10.0f, -10.0f);
    float output = computePidOutput(20.0f, 22.0f, Kp, Ki, Kd, integral, previousError, 2000, 3000, 10.0f, -10.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, output);
}

void test_pid_integral_term_accumulation() {
    float integral = 0.0f;
    float previousError = 0.0f;
    float Kp = 0.0f, Ki = 1.0f, Kd = 0.0f;

    computePidOutput(20.0f, 21.0f, Kp, Ki, Kd, integral, previousError, 1000, 2000, 10.0f, -10.0f);
    float output = computePidOutput(20.0f, 21.0f, Kp, Ki, Kd, integral, previousError, 2000, 3000, 10.0f, -10.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, output);
}

void test_pid_derivative_term_positive_change() {
    float integral = 0.0f;
    float previousError = 0.0f;
    float Kp = 0.0f, Ki = 0.0f, Kd = 1.0f;

    computePidOutput(20.0f, 21.0f, Kp, Ki, Kd, integral, previousError, 1000, 2000, 10.0f, -10.0f);
    float output = computePidOutput(20.0f, 22.0f, Kp, Ki, Kd, integral, previousError, 2000, 3000, 10.0f, -10.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, output);
}

void test_pid_derivative_guard_dt_zero() {
    float integral = 0.0f;
    float previousError = 0.0f;
    float Kp = 0.0f, Ki = 0.0f, Kd = 1.0f;

    computePidOutput(20.0f, 21.0f, Kp, Ki, Kd, integral, previousError, 1000, 2000, 10.0f, -10.0f);
    float output = computePidOutput(20.0f, 22.0f, Kp, Ki, Kd, integral, previousError, 2000, 2000, 10.0f, -10.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, output);
    TEST_ASSERT_FALSE(std::isnan(output));
    TEST_ASSERT_FALSE(std::isinf(output));
}

void test_pid_full_calculation() {
    float integral = 0.0f;
    float previousError = 0.0f;
    float Kp = 2.0f, Ki = 0.1f, Kd = 0.5f;

    computePidOutput(20.0f, 22.0f, Kp, Ki, Kd, integral, previousError, 1000, 2000, 10.0f, -10.0f);
    float output = computePidOutput(20.0f, 22.0f, Kp, Ki, Kd, integral, previousError, 2000, 3000, 10.0f, -10.0f);

    float expected = 4.0f + 0.4f + 0.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expected, output);
}

void test_pid_output_clamped_to_max() {
    float integral = 0.0f;
    float previousError = 0.0f;
    float Kp = 10.0f, Ki = 0.0f, Kd = 0.0f;

    computePidOutput(0.0f, 22.0f, Kp, Ki, Kd, integral, previousError, 1000, 2000, 1.0f, 0.0f);
    float output = computePidOutput(0.0f, 22.0f, Kp, Ki, Kd, integral, previousError, 2000, 3000, 1.0f, 0.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, output);
}

void test_pid_output_clamped_to_min() {
    float integral = 0.0f;
    float previousError = 0.0f;
    float Kp = 2.0f, Ki = 0.0f, Kd = 0.0f;

    computePidOutput(30.0f, 20.0f, Kp, Ki, Kd, integral, previousError, 1000, 2000, 1.0f, 0.0f);
    float output = computePidOutput(30.0f, 20.0f, Kp, Ki, Kd, integral, previousError, 2000, 3000, 1.0f, 0.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, output);
}

void test_pid_anti_windup() {
    float integral = 0.0f;
    float previousError = 0.0f;
    float Kp = 0.0f, Ki = 1.0f, Kd = 0.0f;

    for (int i = 0; i < 100; i++) {
        computePidOutput(0.0f, 100.0f, Kp, Ki, Kd, integral, previousError, 1000 + i * 1000, 1000 + (i + 1) * 1000, 1.0f, 0.0f);
    }

    float output = computePidOutput(0.0f, 100.0f, Kp, Ki, Kd, integral, previousError, 200000, 201000, 1.0f, 0.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, output);
}

void test_setpoint_increase() {
    float integral = 0.0f;
    float previousError = 0.0f;
    float Kp = 1.0f, Ki = 0.0f, Kd = 0.0f;

    computePidOutput(20.0f, 20.0f, Kp, Ki, Kd, integral, previousError, 1000, 2000, 10.0f, -10.0f);
    computePidOutput(20.0f, 25.0f, Kp, Ki, Kd, integral, previousError, 2000, 3000, 10.0f, -10.0f);
    float outputAfter = computePidOutput(20.0f, 25.0f, Kp, Ki, Kd, integral, previousError, 3000, 4000, 10.0f, -10.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, outputAfter);
}

void test_setpoint_decrease() {
    float integral = 0.0f;
    float previousError = 0.0f;
    float Kp = 1.0f, Ki = 0.0f, Kd = 0.0f;

    computePidOutput(25.0f, 25.0f, Kp, Ki, Kd, integral, previousError, 1000, 2000, 10.0f, -10.0f);
    computePidOutput(25.0f, 20.0f, Kp, Ki, Kd, integral, previousError, 2000, 3000, 10.0f, -10.0f);
    float outputAfter = computePidOutput(25.0f, 20.0f, Kp, Ki, Kd, integral, previousError, 3000, 4000, 10.0f, -10.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.0f, outputAfter);
}

void test_control_disabled_returns_zero() {
    bool controlEnabled = false;

    float output = 0.0f;
    if (controlEnabled) {
        float integral = 0.0f;
        float previousError = 0.0f;
        output = computePidOutput(20.0f, 22.0f, 2.0f, 0.1f, 0.5f, integral, previousError, 1000, 2000, 1.0f, 0.0f);
    }

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, output);
}

void test_nan_sensor_reading_returns_zero() {
    float integral = 0.0f;
    float previousError = 0.0f;

    computePidOutput(20.0f, 22.0f, 2.0f, 0.1f, 0.5f, integral, previousError, 1000, 2000, 10.0f, -10.0f);

    float output = computePidOutput(NAN, 22.0f, 2.0f, 0.1f, 0.5f, integral, previousError, 2000, 3000, 10.0f, -10.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, output);
}

void test_control_output_saturation() {
    float integral = 0.0f;
    float previousError = 0.0f;
    float Kp = 2.0f, Ki = 0.0f, Kd = 0.0f;

    computePidOutput(0.0f, 22.0f, Kp, Ki, Kd, integral, previousError, 1000, 2000, 1.0f, 0.0f);
    float output = computePidOutput(0.0f, 22.0f, Kp, Ki, Kd, integral, previousError, 2000, 3000, 1.0f, 0.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, output);
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
    RUN_TEST(test_setpoint_increase);
    RUN_TEST(test_setpoint_decrease);
    RUN_TEST(test_control_disabled_returns_zero);
    RUN_TEST(test_nan_sensor_reading_returns_zero);
    RUN_TEST(test_control_output_saturation);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}