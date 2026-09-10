#include <cstring>

#include "network/MqttPayload.h"
#include "unity.h"

void setUp() {}
void tearDown() {}

void test_float_measurement_has_two_decimals() {
    Sensor::Measurement m{Sensor::MeasurementType::Temperature, 21.456f, "SHT4x", false};
    char out[256];
    const int n = Net::formatMeasurementPayload(out, sizeof(out), m, 1700000000);
    TEST_ASSERT_EQUAL_STRING(
        "{\"time\":1700000000,\"value\":21.46,\"unit\":\"°C\",\"sensor\":\"SHT4x\",\"calculated\":false}", out);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(strlen(out)), n);
}

void test_integer_measurement_prints_as_integer() {
    Sensor::Measurement m{Sensor::MeasurementType::CO2, int32_t{812}, "SCD4x", false};
    char out[256];
    Net::formatMeasurementPayload(out, sizeof(out), m, 5);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"value\":812,"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"time\":5,"));
}

void test_calculated_flag_and_unsynced_epoch() {
    Sensor::Measurement m{Sensor::MeasurementType::DewPoint, 9.0f, "BME680", true};
    char out[256];
    Net::formatMeasurementPayload(out, sizeof(out), m, 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"time\":0,"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"calculated\":true}"));
}

void test_truncation_reports_full_length() {
    Sensor::Measurement m{Sensor::MeasurementType::Temperature, 1.0f, "SHT4x", false};
    char out[16];
    const int n = Net::formatMeasurementPayload(out, sizeof(out), m, 1700000000);
    TEST_ASSERT_GREATER_THAN_INT(static_cast<int>(sizeof(out)), n);
    TEST_ASSERT_EQUAL_UINT(sizeof(out) - 1, strlen(out));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_float_measurement_has_two_decimals);
    RUN_TEST(test_integer_measurement_prints_as_integer);
    RUN_TEST(test_calculated_flag_and_unsynced_epoch);
    RUN_TEST(test_truncation_reports_full_length);
    return UNITY_END();
}
