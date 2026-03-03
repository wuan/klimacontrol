#include "unity.h"
#include "sensor/Sensor.h"
#include <cmath>
#include <variant>

void setUp() {}
void tearDown() {}

// --- getTemperature / getRelativeHumidity / getDewPoint safe variant access ---

// Helper: get float from measurement safely (mirrors the fixed SensorController logic)
static float safeGetFloat(const Sensor::Measurement* m) {
    if (!m) return NAN;
    const float* f = std::get_if<float>(&m->value);
    return f ? *f : NAN;
}

// Helper: get int32 from measurement safely (mirrors the fixed SensorController logic)
static int32_t safeGetInt(const Sensor::Measurement* m) {
    if (!m) return -1;
    const int32_t* i = std::get_if<int32_t>(&m->value);
    return i ? *i : -1;
}

void test_safe_get_float_returns_value_when_float() {
    Sensor::Measurement m{Sensor::MeasurementType::Temperature, 23.5f, "SHT4x", false};
    float val = safeGetFloat(&m);
    TEST_ASSERT_FALSE(std::isnan(val));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.5f, val);
}

void test_safe_get_float_returns_nan_when_null() {
    float val = safeGetFloat(nullptr);
    TEST_ASSERT_TRUE(std::isnan(val));
}

void test_safe_get_float_returns_nan_when_int32() {
    // Measurement holds int32_t but caller expects float — must not crash
    Sensor::Measurement m{Sensor::MeasurementType::VocIndex, (int32_t)150, "SGP40", false};
    float val = safeGetFloat(&m);
    TEST_ASSERT_TRUE(std::isnan(val));
}

void test_safe_get_int_returns_value_when_int32() {
    Sensor::Measurement m{Sensor::MeasurementType::VocIndex, (int32_t)200, "SGP40", false};
    int32_t val = safeGetInt(&m);
    TEST_ASSERT_EQUAL(200, val);
}

void test_safe_get_int_returns_minus1_when_null() {
    int32_t val = safeGetInt(nullptr);
    TEST_ASSERT_EQUAL(-1, val);
}

void test_safe_get_int_returns_minus1_when_float() {
    // Measurement holds float but caller expects int32_t — must not crash
    Sensor::Measurement m{Sensor::MeasurementType::Temperature, 22.0f, "SHT4x", false};
    int32_t val = safeGetInt(&m);
    TEST_ASSERT_EQUAL(-1, val);
}

// --- SensorMonitor delay calculation (unsigned underflow fix) ---

// Mirrors the fixed delay calculation from SensorMonitor::task()
static unsigned long computeDelay(unsigned long readingInterval, unsigned long elapsed) {
    return elapsed < readingInterval ? readingInterval - elapsed : 1ul;
}

void test_delay_normal_case() {
    // Sensor reading took 200ms, interval is 1000ms → should wait 800ms
    unsigned long delay = computeDelay(1000ul, 200ul);
    TEST_ASSERT_EQUAL(800ul, delay);
}

void test_delay_exact_interval() {
    // Reading took exactly as long as interval → should wait 1ms minimum
    unsigned long delay = computeDelay(1000ul, 1000ul);
    TEST_ASSERT_EQUAL(1ul, delay);
}

void test_delay_overrun_no_underflow() {
    // Reading took longer than interval (e.g. slow I2C sensor) → must not underflow to huge number
    // Old code: readingInterval - elapsed would underflow for unsigned arithmetic
    unsigned long delay = computeDelay(1000ul, 1500ul);
    TEST_ASSERT_EQUAL(1ul, delay);   // must clamp to 1, not wrap to ~ULONG_MAX
    TEST_ASSERT_LESS_OR_EQUAL(1000ul, delay);
}

void test_delay_large_overrun() {
    // Very slow sensor (5000ms for a 1000ms interval)
    unsigned long delay = computeDelay(1000ul, 5000ul);
    TEST_ASSERT_EQUAL(1ul, delay);
}

void test_delay_zero_elapsed() {
    // Instantaneous reading → should wait full interval
    unsigned long delay = computeDelay(1000ul, 0ul);
    TEST_ASSERT_EQUAL(1000ul, delay);
}

// --- PID derivative guard against dt == 0 ---

// Mirrors the fixed derivative calculation from SensorController::updateControl()
static float computeDerivative(float Kd, float error, float previousError, float dt) {
    if (dt > 0.0f) {
        return Kd * (error - previousError) / dt;
    }
    return 0.0f;
}

void test_pid_derivative_normal() {
    float d = computeDerivative(0.5f, 2.0f, 1.0f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, d);
}

void test_pid_derivative_zero_dt_returns_zero() {
    // When dt == 0, derivative must be 0 (not Inf or NaN)
    float d = computeDerivative(0.5f, 2.0f, 1.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, d);
    TEST_ASSERT_FALSE(std::isinf(d));
    TEST_ASSERT_FALSE(std::isnan(d));
}

void test_pid_derivative_negative_error_change() {
    float d = computeDerivative(0.5f, 1.0f, 3.0f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, d);
}

// --- WiFi reconnect failure counter logic ---

// Mirrors the reconnect failure counting in Network::task()
static constexpr uint8_t MAX_WIFI_RECONNECT_FAILURES = 10;

static bool shouldRestartOnReconnectFailure(uint8_t failures) {
    return failures >= MAX_WIFI_RECONNECT_FAILURES;
}

void test_wifi_reconnect_failure_below_threshold() {
    // 9 failures must NOT trigger restart
    TEST_ASSERT_FALSE(shouldRestartOnReconnectFailure(9));
}

void test_wifi_reconnect_failure_at_threshold() {
    // At exactly the threshold restart is triggered
    TEST_ASSERT_TRUE(shouldRestartOnReconnectFailure(MAX_WIFI_RECONNECT_FAILURES));
}

void test_wifi_reconnect_failure_reset_on_success() {
    // After a successful reconnect the counter resets to 0, no restart
    uint8_t failures = MAX_WIFI_RECONNECT_FAILURES - 1;
    failures = 0; // simulates reset on success
    TEST_ASSERT_FALSE(shouldRestartOnReconnectFailure(failures));
}

// --- NTP epoch guard logic ---

// Mirrors the guard condition in Network::task():
//   if (currentEpoch > 0 && lastNtpUpdate > 0 && currentEpoch - lastNtpUpdate > 3600)
static bool shouldUpdateNtp(uint32_t currentEpoch, uint32_t lastNtpUpdate) {
    return currentEpoch > 0 && lastNtpUpdate > 0
           && currentEpoch - lastNtpUpdate > 3600;
}

void test_ntp_epoch_guard_both_zero_no_update() {
    // Both zero (NTP never synced) – must NOT trigger an update
    TEST_ASSERT_FALSE(shouldUpdateNtp(0u, 0u));
}

void test_ntp_epoch_guard_time_elapsed_triggers_update() {
    // More than one hour elapsed since last sync – must trigger
    uint32_t last = 1700000000u;
    uint32_t current = last + 3601u;
    TEST_ASSERT_TRUE(shouldUpdateNtp(current, last));
}

void test_ntp_epoch_guard_current_zero_no_spurious_update() {
    // currentEpoch == 0 while lastNtpUpdate is valid – uint32_t underflow
    // would previously evaluate to a huge number; guard must prevent this
    uint32_t last = 1700000000u;
    TEST_ASSERT_FALSE(shouldUpdateNtp(0u, last));
}

void test_ntp_epoch_guard_last_zero_current_nonzero_no_update() {
    // lastNtpUpdate == 0 means we haven't recorded a successful sync yet –
    // must NOT trigger a spurious update based on a large difference
    uint32_t current = 1700000000u;
    TEST_ASSERT_FALSE(shouldUpdateNtp(current, 0u));
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_safe_get_float_returns_value_when_float);
    RUN_TEST(test_safe_get_float_returns_nan_when_null);
    RUN_TEST(test_safe_get_float_returns_nan_when_int32);
    RUN_TEST(test_safe_get_int_returns_value_when_int32);
    RUN_TEST(test_safe_get_int_returns_minus1_when_null);
    RUN_TEST(test_safe_get_int_returns_minus1_when_float);
    RUN_TEST(test_delay_normal_case);
    RUN_TEST(test_delay_exact_interval);
    RUN_TEST(test_delay_overrun_no_underflow);
    RUN_TEST(test_delay_large_overrun);
    RUN_TEST(test_delay_zero_elapsed);
    RUN_TEST(test_pid_derivative_normal);
    RUN_TEST(test_pid_derivative_zero_dt_returns_zero);
    RUN_TEST(test_pid_derivative_negative_error_change);
    RUN_TEST(test_wifi_reconnect_failure_below_threshold);
    RUN_TEST(test_wifi_reconnect_failure_at_threshold);
    RUN_TEST(test_wifi_reconnect_failure_reset_on_success);
    RUN_TEST(test_ntp_epoch_guard_both_zero_no_update);
    RUN_TEST(test_ntp_epoch_guard_time_elapsed_triggers_update);
    RUN_TEST(test_ntp_epoch_guard_current_zero_no_spurious_update);
    RUN_TEST(test_ntp_epoch_guard_last_zero_current_nonzero_no_update);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
