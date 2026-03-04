#include "unity.h"
#include "sensor/Sensor.h"

void setUp() {}
void tearDown() {}

// --- calcDewPoint ---

void test_dew_point_warm_humid() {
    // At 20°C and 65% RH, dew point ≈ 13.2°C (Magnus formula)
    float dp = Sensor::calcDewPoint(20.0f, 65.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 13.2f, dp);
}

void test_dew_point_saturated() {
    // At 0°C and 100% RH, dew point equals temperature
    float dp = Sensor::calcDewPoint(0.0f, 100.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, dp);
}

void test_dew_point_dry() {
    // At 30°C and 20% RH, dew point ≈ 4.6°C (Magnus formula)
    float dp = Sensor::calcDewPoint(30.0f, 20.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 4.6f, dp);
}

void test_dew_point_below_temperature() {
    // Dew point must always be <= temperature
    float dp = Sensor::calcDewPoint(25.0f, 50.0f);
    TEST_ASSERT_LESS_OR_EQUAL(25.0f, dp);
}

// --- calcSeaLevelPressure ---

void test_sea_level_pressure_zero_elevation() {
    // At zero elevation, station pressure equals sea-level pressure
    float p = Sensor::calcSeaLevelPressure(1013.25f, 15.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1013.25f, p);
}

void test_sea_level_pressure_positive_elevation() {
    // At elevation, sea-level pressure is higher than station pressure
    float station = 954.6f;
    float p = Sensor::calcSeaLevelPressure(station, 15.0f, 500.0f);
    TEST_ASSERT_GREATER_THAN(station, p);
}

void test_sea_level_pressure_known_value() {
    // Approximate check at 500 m, 15°C: station 954.6 hPa → sea level ~1013 hPa
    float p = Sensor::calcSeaLevelPressure(954.6f, 15.0f, 500.0f);
    TEST_ASSERT_FLOAT_WITHIN(3.0f, 1013.25f, p);
}

// --- measurementTypeLabel ---

void test_label_temperature() {
    TEST_ASSERT_EQUAL_STRING("temperature",
        Sensor::measurementTypeLabel(Sensor::MeasurementType::Temperature));
}

void test_label_relative_humidity() {
    TEST_ASSERT_EQUAL_STRING("relative humidity",
        Sensor::measurementTypeLabel(Sensor::MeasurementType::RelativeHumidity));
}

void test_label_co2() {
    TEST_ASSERT_EQUAL_STRING("CO2",
        Sensor::measurementTypeLabel(Sensor::MeasurementType::CO2));
}

void test_label_voc_index() {
    TEST_ASSERT_EQUAL_STRING("VOC index",
        Sensor::measurementTypeLabel(Sensor::MeasurementType::VocIndex));
}

void test_label_pressure() {
    TEST_ASSERT_EQUAL_STRING("pressure",
        Sensor::measurementTypeLabel(Sensor::MeasurementType::Pressure));
}

// --- measurementTypeUnit ---

void test_unit_temperature() {
    TEST_ASSERT_EQUAL_STRING("°C",
        Sensor::measurementTypeUnit(Sensor::MeasurementType::Temperature));
}

void test_unit_relative_humidity() {
    TEST_ASSERT_EQUAL_STRING("%",
        Sensor::measurementTypeUnit(Sensor::MeasurementType::RelativeHumidity));
}

void test_unit_pressure() {
    TEST_ASSERT_EQUAL_STRING("hPa",
        Sensor::measurementTypeUnit(Sensor::MeasurementType::Pressure));
}

void test_unit_co2() {
    TEST_ASSERT_EQUAL_STRING("ppm",
        Sensor::measurementTypeUnit(Sensor::MeasurementType::CO2));
}

void test_unit_illuminance() {
    TEST_ASSERT_EQUAL_STRING("lux",
        Sensor::measurementTypeUnit(Sensor::MeasurementType::Illuminance));
}

// --- findMeasurement ---

void test_find_measurement_found() {
    std::vector<Sensor::Measurement> measurements = {
        {Sensor::MeasurementType::Temperature, 22.5f, "SHT4x", false},
        {Sensor::MeasurementType::RelativeHumidity, 65.0f, "SHT4x", false},
    };
    const Sensor::Measurement *m =
        Sensor::findMeasurement(measurements, Sensor::MeasurementType::Temperature);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.5f, std::get<float>(m->value));
}

void test_find_measurement_second_entry() {
    std::vector<Sensor::Measurement> measurements = {
        {Sensor::MeasurementType::Temperature, 22.5f, "SHT4x", false},
        {Sensor::MeasurementType::RelativeHumidity, 65.0f, "SHT4x", false},
    };
    const Sensor::Measurement *m =
        Sensor::findMeasurement(measurements, Sensor::MeasurementType::RelativeHumidity);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 65.0f, std::get<float>(m->value));
}

void test_find_measurement_not_found() {
    std::vector<Sensor::Measurement> measurements = {
        {Sensor::MeasurementType::Temperature, 22.5f, "SHT4x", false},
    };
    const Sensor::Measurement *m =
        Sensor::findMeasurement(measurements, Sensor::MeasurementType::CO2);
    TEST_ASSERT_NULL(m);
}

void test_find_measurement_empty_vector() {
    std::vector<Sensor::Measurement> measurements;
    const Sensor::Measurement *m =
        Sensor::findMeasurement(measurements, Sensor::MeasurementType::Temperature);
    TEST_ASSERT_NULL(m);
}

void test_find_measurement_int_value() {
    std::vector<Sensor::Measurement> measurements = {
        {Sensor::MeasurementType::VocIndex, (int32_t)150, "SGP40", false},
    };
    const Sensor::Measurement *m =
        Sensor::findMeasurement(measurements, Sensor::MeasurementType::VocIndex);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL(150, std::get<int32_t>(m->value));
}

// --- measurementCount() ---

// Minimal concrete sensor for testing the default measurementCount() implementation
namespace {
    struct ThreeMeasurementSensor : public Sensor::Sensor {
        bool begin() override { return true; }
        ::Sensor::SensorReading read() override { return {}; }
        const char* getType() const override { return "Test3"; }
        bool isConnected() override { return true; }
        [[nodiscard]] ::Sensor::TypeSpan provides() const override {
            static constexpr ::Sensor::MeasurementType types[] = {
                ::Sensor::MeasurementType::Temperature,
                ::Sensor::MeasurementType::RelativeHumidity,
                ::Sensor::MeasurementType::DewPoint
            };
            return {types, 3};
        }
    };

    struct OneMeasurementSensor : public Sensor::Sensor {
        bool begin() override { return true; }
        ::Sensor::SensorReading read() override { return {}; }
        const char* getType() const override { return "Test1"; }
        bool isConnected() override { return true; }
        [[nodiscard]] ::Sensor::TypeSpan provides() const override {
            static constexpr ::Sensor::MeasurementType types[] = {
                ::Sensor::MeasurementType::Illuminance
            };
            return {types, 1};
        }
    };

    struct NineMeasurementSensor : public Sensor::Sensor {
        bool begin() override { return true; }
        ::Sensor::SensorReading read() override { return {}; }
        const char* getType() const override { return "Test9"; }
        bool isConnected() override { return true; }
        [[nodiscard]] ::Sensor::TypeSpan provides() const override {
            static constexpr ::Sensor::MeasurementType types[] = {
                ::Sensor::MeasurementType::Particles03,
                ::Sensor::MeasurementType::Particles05,
                ::Sensor::MeasurementType::Particles10,
                ::Sensor::MeasurementType::Particles25,
                ::Sensor::MeasurementType::Particles50,
                ::Sensor::MeasurementType::Particles100,
                ::Sensor::MeasurementType::PM10Concentration,
                ::Sensor::MeasurementType::PM25Concentration,
                ::Sensor::MeasurementType::PM100Concentration
            };
            return {types, 9};
        }
    };
} // namespace

void test_measurement_count_matches_provides_count_three() {
    ThreeMeasurementSensor sensor;
    TEST_ASSERT_EQUAL(3u, sensor.measurementCount());
    TEST_ASSERT_EQUAL(sensor.provides().count, sensor.measurementCount());
}

void test_measurement_count_matches_provides_count_one() {
    OneMeasurementSensor sensor;
    TEST_ASSERT_EQUAL(1u, sensor.measurementCount());
    TEST_ASSERT_EQUAL(sensor.provides().count, sensor.measurementCount());
}

void test_measurement_count_matches_provides_count_nine() {
    NineMeasurementSensor sensor;
    TEST_ASSERT_EQUAL(9u, sensor.measurementCount());
    TEST_ASSERT_EQUAL(sensor.provides().count, sensor.measurementCount());
}

// Mirrors the allMeasurements.reserve() logic in SensorController::readSensors():
// total = sum of (sensor->measurementCount() + 1) over all sensors
static size_t computeTotalExpected(const std::vector<size_t>& counts) {
    size_t total = 0;
    for (auto c : counts) {
        total += c + 1; // +1 for the Time measurement added per valid sensor
    }
    return total;
}

void test_total_expected_single_sensor() {
    // One sensor with 3 measurements → 3 + 1 = 4
    TEST_ASSERT_EQUAL(4u, computeTotalExpected({3}));
}

void test_total_expected_multiple_sensors() {
    // SHT4x(3) + SGP40(1) + DeviceSensor(5) → (3+1)+(1+1)+(5+1) = 12
    TEST_ASSERT_EQUAL(12u, computeTotalExpected({3, 1, 5}));
}

void test_total_expected_no_sensors() {
    TEST_ASSERT_EQUAL(0u, computeTotalExpected({}));
}

void test_total_expected_pm25_sensor() {
    // PM25 provides 9 measurements → 9 + 1 = 10
    TEST_ASSERT_EQUAL(10u, computeTotalExpected({9}));
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_dew_point_warm_humid);
    RUN_TEST(test_dew_point_saturated);
    RUN_TEST(test_dew_point_dry);
    RUN_TEST(test_dew_point_below_temperature);
    RUN_TEST(test_sea_level_pressure_zero_elevation);
    RUN_TEST(test_sea_level_pressure_positive_elevation);
    RUN_TEST(test_sea_level_pressure_known_value);
    RUN_TEST(test_label_temperature);
    RUN_TEST(test_label_relative_humidity);
    RUN_TEST(test_label_co2);
    RUN_TEST(test_label_voc_index);
    RUN_TEST(test_label_pressure);
    RUN_TEST(test_unit_temperature);
    RUN_TEST(test_unit_relative_humidity);
    RUN_TEST(test_unit_pressure);
    RUN_TEST(test_unit_co2);
    RUN_TEST(test_unit_illuminance);
    RUN_TEST(test_find_measurement_found);
    RUN_TEST(test_find_measurement_second_entry);
    RUN_TEST(test_find_measurement_not_found);
    RUN_TEST(test_find_measurement_empty_vector);
    RUN_TEST(test_find_measurement_int_value);
    RUN_TEST(test_measurement_count_matches_provides_count_three);
    RUN_TEST(test_measurement_count_matches_provides_count_one);
    RUN_TEST(test_measurement_count_matches_provides_count_nine);
    RUN_TEST(test_total_expected_single_sensor);
    RUN_TEST(test_total_expected_multiple_sensors);
    RUN_TEST(test_total_expected_no_sensors);
    RUN_TEST(test_total_expected_pm25_sensor);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
