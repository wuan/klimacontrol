#include "unity.h"
#include "sensor/Sensor.h"

// Bring namespace types into scope to avoid Sensor::Sensor ambiguity
using SensorBase = ::Sensor::Sensor;
using SensorStatus = ::Sensor::SensorStatus;
using SensorReading = ::Sensor::SensorReading;
using MeasurementType = ::Sensor::MeasurementType;
using TypeSpan = ::Sensor::TypeSpan;

void setUp() {}
void tearDown() {}

// Concrete test sensor with controllable begin()/read() results
namespace {
    struct MockSensor : public SensorBase {
        bool beginResult = true;
        bool readValid = true;

        bool begin() override { return beginResult; }

        SensorReading read() override {
            SensorReading r;
            r.valid = readValid;
            if (readValid) {
                r.measurements.push_back(
                    {MeasurementType::Temperature, 22.0f, "Mock", false});
            }
            return r;
        }

        const char* getType() const override { return "Mock"; }
        bool isConnected() override { return sensorStatus == SensorStatus::Online; }

        [[nodiscard]] TypeSpan provides() const override {
            static constexpr MeasurementType types[] = {
                MeasurementType::Temperature
            };
            return {types, 1};
        }
    };
} // namespace

// --- Initial state ---

void test_initial_status_uninitialized() {
    MockSensor sensor;
    TEST_ASSERT_EQUAL(SensorStatus::Uninitialized, sensor.getStatus());
}

void test_initial_consecutive_failures_zero() {
    MockSensor sensor;
    TEST_ASSERT_EQUAL(0, sensor.getConsecutiveReadFailures());
}

// --- tryBegin ---

void test_trybegin_success_sets_online() {
    MockSensor sensor;
    sensor.beginResult = true;
    TEST_ASSERT_TRUE(sensor.tryBegin());
    TEST_ASSERT_EQUAL(SensorStatus::Online, sensor.getStatus());
}

void test_trybegin_failure_sets_init_failed() {
    MockSensor sensor;
    sensor.beginResult = false;
    TEST_ASSERT_FALSE(sensor.tryBegin());
    TEST_ASSERT_EQUAL(SensorStatus::InitFailed, sensor.getStatus());
}

void test_trybegin_resets_failure_counter() {
    MockSensor sensor;
    sensor.beginResult = true;
    sensor.tryBegin();
    sensor.recordReadResult(false);
    sensor.recordReadResult(false);
    TEST_ASSERT_EQUAL(2, sensor.getConsecutiveReadFailures());

    sensor.tryBegin();
    TEST_ASSERT_EQUAL(0, sensor.getConsecutiveReadFailures());
}

// --- recordReadResult state transitions ---

void test_valid_read_keeps_online() {
    MockSensor sensor;
    sensor.beginResult = true;
    sensor.tryBegin();
    sensor.recordReadResult(true);
    TEST_ASSERT_EQUAL(SensorStatus::Online, sensor.getStatus());
    TEST_ASSERT_EQUAL(0, sensor.getConsecutiveReadFailures());
}

void test_single_failure_stays_online() {
    MockSensor sensor;
    sensor.beginResult = true;
    sensor.tryBegin();
    sensor.recordReadResult(false);
    TEST_ASSERT_EQUAL(SensorStatus::Online, sensor.getStatus());
    TEST_ASSERT_EQUAL(1, sensor.getConsecutiveReadFailures());
}

void test_threshold_failures_transitions_to_read_failing() {
    MockSensor sensor;
    sensor.beginResult = true;
    sensor.tryBegin();

    // READ_FAILURE_THRESHOLD is 10
    for (int i = 0; i < 9; i++) {
        sensor.recordReadResult(false);
        TEST_ASSERT_EQUAL(SensorStatus::Online, sensor.getStatus());
    }
    // 10th failure triggers transition
    sensor.recordReadResult(false);
    TEST_ASSERT_EQUAL(SensorStatus::ReadFailing, sensor.getStatus());
    TEST_ASSERT_EQUAL(10, sensor.getConsecutiveReadFailures());
}

void test_valid_read_recovers_from_read_failing() {
    MockSensor sensor;
    sensor.beginResult = true;
    sensor.tryBegin();

    for (int i = 0; i < 10; i++) {
        sensor.recordReadResult(false);
    }
    TEST_ASSERT_EQUAL(SensorStatus::ReadFailing, sensor.getStatus());

    sensor.recordReadResult(true);
    TEST_ASSERT_EQUAL(SensorStatus::Online, sensor.getStatus());
    TEST_ASSERT_EQUAL(0, sensor.getConsecutiveReadFailures());
}

void test_failure_while_not_online_does_not_increment() {
    MockSensor sensor;
    // Status is Uninitialized — failures should not count
    sensor.recordReadResult(false);
    TEST_ASSERT_EQUAL(0, sensor.getConsecutiveReadFailures());
    TEST_ASSERT_EQUAL(SensorStatus::Uninitialized, sensor.getStatus());
}

void test_failure_while_init_failed_does_not_increment() {
    MockSensor sensor;
    sensor.beginResult = false;
    sensor.tryBegin();
    TEST_ASSERT_EQUAL(SensorStatus::InitFailed, sensor.getStatus());

    sensor.recordReadResult(false);
    TEST_ASSERT_EQUAL(0, sensor.getConsecutiveReadFailures());
}

// --- tryBegin recovery from InitFailed ---

void test_trybegin_recovers_from_init_failed() {
    MockSensor sensor;
    sensor.beginResult = false;
    sensor.tryBegin();
    TEST_ASSERT_EQUAL(SensorStatus::InitFailed, sensor.getStatus());

    sensor.beginResult = true;
    sensor.tryBegin();
    TEST_ASSERT_EQUAL(SensorStatus::Online, sensor.getStatus());
}

// --- sensorStatusLabel ---

void test_status_label_online() {
    TEST_ASSERT_EQUAL_STRING("online",
        ::Sensor::sensorStatusLabel(SensorStatus::Online));
}

void test_status_label_uninitialized() {
    TEST_ASSERT_EQUAL_STRING("uninitialized",
        ::Sensor::sensorStatusLabel(SensorStatus::Uninitialized));
}

void test_status_label_init_failed() {
    TEST_ASSERT_EQUAL_STRING("init_failed",
        ::Sensor::sensorStatusLabel(SensorStatus::InitFailed));
}

void test_status_label_read_failing() {
    TEST_ASSERT_EQUAL_STRING("read_failing",
        ::Sensor::sensorStatusLabel(SensorStatus::ReadFailing));
}

// --- Default provides/requires ---

void test_default_provides_empty() {
    struct MinimalSensor : public SensorBase {
        bool begin() override { return true; }
        SensorReading read() override { return {}; }
        const char* getType() const override { return "Minimal"; }
        bool isConnected() override { return true; }
    };
    MinimalSensor sensor;
    TEST_ASSERT_EQUAL(0, sensor.provides().count);
    TEST_ASSERT_NULL(sensor.provides().data);
    TEST_ASSERT_EQUAL(0, sensor.requires().count);
    TEST_ASSERT_EQUAL(0, sensor.measurementCount());
}

int runUnityTests() {
    UNITY_BEGIN();
    // Initial state
    RUN_TEST(test_initial_status_uninitialized);
    RUN_TEST(test_initial_consecutive_failures_zero);
    // tryBegin
    RUN_TEST(test_trybegin_success_sets_online);
    RUN_TEST(test_trybegin_failure_sets_init_failed);
    RUN_TEST(test_trybegin_resets_failure_counter);
    // recordReadResult
    RUN_TEST(test_valid_read_keeps_online);
    RUN_TEST(test_single_failure_stays_online);
    RUN_TEST(test_threshold_failures_transitions_to_read_failing);
    RUN_TEST(test_valid_read_recovers_from_read_failing);
    RUN_TEST(test_failure_while_not_online_does_not_increment);
    RUN_TEST(test_failure_while_init_failed_does_not_increment);
    // Recovery
    RUN_TEST(test_trybegin_recovers_from_init_failed);
    // Status labels
    RUN_TEST(test_status_label_online);
    RUN_TEST(test_status_label_uninitialized);
    RUN_TEST(test_status_label_init_failed);
    RUN_TEST(test_status_label_read_failing);
    // Default provides/requires
    RUN_TEST(test_default_provides_empty);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
