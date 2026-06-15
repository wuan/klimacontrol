#include <algorithm>
#include <cstddef>

#include "sensor/DeviceSensor.h"
#include "sensor/Sensor.h"
#include "unity.h"

// Native test for the "Largest free block is a measurement type" contract —
// see spec `sensor-management` (delta in change
// `largest-free-block-as-measurement`) → "Largest free block is a
// measurement type".
//
// The new MeasurementType is added to the enum so the MQTT publish loop can
// pick it up, and DeviceSensor lists it in providesMeasurements() so the
// sensor-monitor task produces it on each cycle. The test pins the label,
// the unit, the count, and the membership in the providesMeasurements() list.
// On the native build the read() body is empty (the push is guarded by
// #ifdef ARDUINO), so the empty-measurements assertion is also part of the
// contract: the test must not accidentally become a "feeds a heap value into
// the native test" pathway that would diverge from the device behaviour.

using SensorType = ::Sensor::Sensor;
using MeasurementType = ::Sensor::MeasurementType;

void setUp() {}
void tearDown() {}

void test_largest_free_block_label_is_snake_case() {
    TEST_ASSERT_EQUAL_STRING(
        "largest_free_block",
        ::Sensor::measurementTypeLabel(MeasurementType::LargestFreeBlock));
}

void test_largest_free_block_unit_is_kb() {
    // Matches the existing FreeHeap convention so the MQTT topic tree
    // reports device-internal measurements in a single unit.
    TEST_ASSERT_EQUAL_STRING(
        "kB",
        ::Sensor::measurementTypeUnit(MeasurementType::LargestFreeBlock));
}

void test_device_sensor_provides_largest_free_block() {
    ::Sensor::DeviceSensor sensor;
    const auto span = sensor.providesMeasurements();
    TEST_ASSERT_NOT_NULL_MESSAGE(span.data,
        "DeviceSensor::providesMeasurements() returned a null data pointer");
    TEST_ASSERT_GREATER_THAN_UINT32(0, static_cast<uint32_t>(span.count));

    const auto *end = span.data + span.count;
    const auto *found = std::find(span.data, end, MeasurementType::LargestFreeBlock);
    TEST_ASSERT_NOT_NULL_MESSAGE(found,
        "DeviceSensor::providesMeasurements() must list LargestFreeBlock");
}

void test_device_sensor_measurement_count_is_six() {
    // The count is bumped from 5 to 6 when LargestFreeBlock is added. Pinning
    // it here means a future addition that forgets to bump the count (or a
    // regression that drops one type) is caught at test time.
    ::Sensor::DeviceSensor sensor;
    const auto span = sensor.providesMeasurements();
    TEST_ASSERT_EQUAL_UINT32(6, static_cast<uint32_t>(span.count));
}

void test_device_sensor_read_is_empty_on_native() {
    // The read() body that pushes measurements is guarded by
    // #ifdef ARDUINO, so on the native build (where ARDUINO is not
    // defined) the returned SensorReading has no measurements even
    // though `reading.valid` is set to true before the guard. This
    // matches the behaviour of every other device-internal metric
    // (FreeHeap, Rssi, Uptime, System, Channel) on native and keeps
    // the test side honest — the heap_caps_* value never leaks into
    // the host test environment.
    ::Sensor::DeviceSensor sensor;
    const auto reading = sensor.read({}, {});
    TEST_ASSERT_TRUE(reading.valid);
    TEST_ASSERT_EQUAL_UINT32(0,
        static_cast<uint32_t>(reading.measurements.size()));
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_largest_free_block_label_is_snake_case);
    RUN_TEST(test_largest_free_block_unit_is_kb);
    RUN_TEST(test_device_sensor_provides_largest_free_block);
    RUN_TEST(test_device_sensor_measurement_count_is_six);
    RUN_TEST(test_device_sensor_read_is_empty_on_native);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
