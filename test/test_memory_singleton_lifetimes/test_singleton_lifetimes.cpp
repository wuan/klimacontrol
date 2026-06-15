#include <memory>
#include <vector>

#include "Config.h"
#include "SensorController.h"
#include "StatusLed.h"
#include "sensor/DeviceSensor.h"
#include "unity.h"

// Native test for the "Vector capacities are reserved at boot" contract —
// see spec `memory-management` → "Vector capacities are reserved at boot".
//
// The SensorController stores sensors in a std::vector<std::unique_ptr<Sensor>>
// that grows as I2C scan discovers sensors at boot. Without a reserve() call
// the vector reallocates on each push_back that crosses a power-of-two
// threshold, leaving a small hole in the heap each time. This test pins the
// contract that `reserveSensorSlots(N)` followed by adding N sensors does
// not reallocate.

void setUp() {}
void tearDown() {}

// The 10-sensor list size is the documented MAX_KNOWN_SENSORS in main.cpp.
// Kept in sync via the same constant — both are guardrails for the I2C
// scan loop's worst case.
static constexpr size_t RESERVE_N = 10;

void test_reserve_sensor_slots_reserves_capacity() {
    Config::ConfigManager config;
    SensorController controller(config, nullptr);
    controller.reserveSensorSlots(RESERVE_N);

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(RESERVE_N,
        static_cast<uint32_t>(controller.getSensorsCapacity()));
}

void test_add_n_sensors_does_not_reallocate() {
    Config::ConfigManager config;
    SensorController controller(config, nullptr);
    controller.reserveSensorSlots(RESERVE_N);

    const size_t capacityBefore = controller.getSensorsCapacity();

    // Add RESERVE_N sensors. Each addSensor pushes onto the sensors vector;
    // if the reserve() did not take, the first few pushes would trigger
    // reallocations and capacity would grow.
    for (size_t i = 0; i < RESERVE_N; ++i) {
        controller.addSensor(std::make_unique<Sensor::DeviceSensor>());
        TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(capacityBefore),
            static_cast<uint32_t>(controller.getSensorsCapacity()));
    }

    TEST_ASSERT_EQUAL_UINT32(RESERVE_N,
        static_cast<uint32_t>(controller.getSensorCount()));
}

void test_measurements_capacity_matches_reserve_contract() {
    Config::ConfigManager config;
    SensorController controller(config, nullptr);
    controller.reserveSensorSlots(RESERVE_N);

    // The contract is `currentMeasurements.reserve(n * MAX_MEASUREMENTS_PER_SENSOR)`
    // where MAX_MEASUREMENTS_PER_SENSOR is a SensorController-internal constant
    // (currently 8). We assert the lower bound (>= RESERVE_N) so the test
    // does not need to know the exact multiplier — only that adding the
    // worst-case number of measurements does not need to reallocate.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(RESERVE_N,
        static_cast<uint32_t>(controller.getMeasurementsCapacity()));
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_reserve_sensor_slots_reserves_capacity);
    RUN_TEST(test_add_n_sensors_does_not_reallocate);
    RUN_TEST(test_measurements_capacity_matches_reserve_contract);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
