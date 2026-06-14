#include "unity.h"
#include "sensor/Sensor.h"

using SensorBase = ::Sensor::Sensor;
using SensorStatus = ::Sensor::SensorStatus;
using SensorReading = ::Sensor::SensorReading;
using MeasurementType = ::Sensor::MeasurementType;
using TypeSpan = ::Sensor::TypeSpan;
using ReadConfig = ::Sensor::ReadConfig;
using Measurement = ::Sensor::Measurement;

void setUp() {}
void tearDown() {}

namespace {
    struct MockSHT4x : public SensorBase {
        bool beginResult = true;
        bool readValid = true;
        float temperature = 22.0f;
        float humidity = 50.0f;

        bool begin() override { return beginResult; }

        SensorReading read(const ReadConfig&, const std::vector<Measurement>&) override {
            SensorReading r;
            r.valid = readValid;
            if (readValid) {
                r.measurements.push_back(
                    {MeasurementType::Temperature, temperature, "SHT4x", false});
                r.measurements.push_back(
                    {MeasurementType::RelativeHumidity, humidity, "SHT4x", false});
            }
            return r;
        }

        const char* getType() const override { return "SHT4x"; }

        [[nodiscard]] TypeSpan providesMeasurements() const override {
            static constexpr MeasurementType types[] = {
                MeasurementType::Temperature,
                MeasurementType::RelativeHumidity,
                MeasurementType::DewPoint
            };
            return {types, 3};
        }
    };
}

void test_sht4x_provides_measurements() {
    MockSHT4x sensor;
    TypeSpan provided = sensor.providesMeasurements();

    TEST_ASSERT_EQUAL(3, provided.count);
    TEST_ASSERT_EQUAL(MeasurementType::Temperature, provided.data[0]);
    TEST_ASSERT_EQUAL(MeasurementType::RelativeHumidity, provided.data[1]);
    TEST_ASSERT_EQUAL(MeasurementType::DewPoint, provided.data[2]);
}

void test_sht4x_read_returns_valid_data() {
    MockSHT4x sensor;
    sensor.beginResult = true;
    sensor.readValid = true;
    sensor.temperature = 23.5f;
    sensor.humidity = 60.0f;

    ReadConfig config;
    std::vector<Measurement> prior;
    SensorReading reading = sensor.read(config, prior);

    TEST_ASSERT_TRUE(reading.valid);
    TEST_ASSERT_EQUAL(2, reading.measurements.size());

    TEST_ASSERT_EQUAL(MeasurementType::Temperature, reading.measurements[0].type);
    float temp = std::get<float>(reading.measurements[0].value);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 23.5f, temp);

    TEST_ASSERT_EQUAL(MeasurementType::RelativeHumidity, reading.measurements[1].type);
    float hum = std::get<float>(reading.measurements[1].value);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 60.0f, hum);
}

void test_sht4x_read_returns_invalid_when_flagged() {
    MockSHT4x sensor;
    sensor.beginResult = true;
    sensor.readValid = false;

    ReadConfig config;
    std::vector<Measurement> prior;
    SensorReading reading = sensor.read(config, prior);

    TEST_ASSERT_FALSE(reading.valid);
    TEST_ASSERT_EQUAL(0, reading.measurements.size());
}

void test_sht4x_begin_success() {
    MockSHT4x sensor;
    sensor.beginResult = true;

    TEST_ASSERT_TRUE(sensor.tryBegin());
    TEST_ASSERT_EQUAL(SensorStatus::Online, sensor.getStatus());
}

void test_sht4x_begin_failure() {
    MockSHT4x sensor;
    sensor.beginResult = false;

    TEST_ASSERT_FALSE(sensor.tryBegin());
    TEST_ASSERT_EQUAL(SensorStatus::InitFailed, sensor.getStatus());
}

void test_sht4x_measurement_types_are_distinct() {
    MockSHT4x sensor;
    TypeSpan provided = sensor.providesMeasurements();

    bool hasTemp = false, hasHum = false, hasDew = false;
    for (uint8_t i = 0; i < provided.count; i++) {
        if (provided.data[i] == MeasurementType::Temperature) hasTemp = true;
        if (provided.data[i] == MeasurementType::RelativeHumidity) hasHum = true;
        if (provided.data[i] == MeasurementType::DewPoint) hasDew = true;
    }

    TEST_ASSERT_TRUE(hasTemp);
    TEST_ASSERT_TRUE(hasHum);
    TEST_ASSERT_TRUE(hasDew);
}

void test_sht4x_get_type() {
    MockSHT4x sensor;
    TEST_ASSERT_EQUAL_STRING("SHT4x", sensor.getType());
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_sht4x_provides_measurements);
    RUN_TEST(test_sht4x_read_returns_valid_data);
    RUN_TEST(test_sht4x_read_returns_invalid_when_flagged);
    RUN_TEST(test_sht4x_begin_success);
    RUN_TEST(test_sht4x_begin_failure);
    RUN_TEST(test_sht4x_measurement_types_are_distinct);
    RUN_TEST(test_sht4x_get_type);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}