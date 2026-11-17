#include <unity.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "display/DisplayField.h"

using Display::DisplayField;
using Display::FieldReading;
using Sensor::Measurement;
using Sensor::MeasurementType;

void setUp() {}
void tearDown() {}

namespace {
    Measurement floatMeasurement(MeasurementType type, float value) {
        return Measurement{type, value, "test", false};
    }

    Measurement intMeasurement(MeasurementType type, int32_t value) {
        return Measurement{type, value, "test", false};
    }

    std::vector<Measurement> measurementVector(std::initializer_list<Measurement> values) {
        return std::vector<Measurement>(values);
    }

    FieldReading readOrDefault(DisplayField field, const std::vector<Measurement>& measurements, bool valid,
                               float elevation = 0.0f) {
        return Display::readDisplayField(field, measurements, valid, elevation);
    }
} // namespace

// --- metadata mapping ---

void test_field_spec_measurement_types() {
    TEST_ASSERT_EQUAL(static_cast<int>(MeasurementType::RelativeHumidity),
                      static_cast<int>(Display::fieldSpec(DisplayField::Humidity)->measurement));
    TEST_ASSERT_EQUAL(static_cast<int>(MeasurementType::DewPoint),
                      static_cast<int>(Display::fieldSpec(DisplayField::DewPoint)->measurement));
    TEST_ASSERT_EQUAL(static_cast<int>(MeasurementType::Pressure),
                      static_cast<int>(Display::fieldSpec(DisplayField::Pressure)->measurement));
    TEST_ASSERT_EQUAL(static_cast<int>(MeasurementType::SeaLevelPressure),
                      static_cast<int>(Display::fieldSpec(DisplayField::SeaLevelPressure)->measurement));
    TEST_ASSERT_EQUAL(static_cast<int>(MeasurementType::CO2),
                      static_cast<int>(Display::fieldSpec(DisplayField::CO2)->measurement));
    TEST_ASSERT_EQUAL(static_cast<int>(MeasurementType::Illuminance),
                      static_cast<int>(Display::fieldSpec(DisplayField::Illuminance)->measurement));
    TEST_ASSERT_EQUAL(static_cast<int>(MeasurementType::VocIndex),
                      static_cast<int>(Display::fieldSpec(DisplayField::VocIndex)->measurement));
    TEST_ASSERT_EQUAL(static_cast<int>(MeasurementType::PM25Concentration),
                      static_cast<int>(Display::fieldSpec(DisplayField::PM25)->measurement));
}

void test_field_spec_none_and_invalid_have_no_spec() {
    TEST_ASSERT_NULL(Display::fieldSpec(DisplayField::None));
    TEST_ASSERT_NULL(Display::fieldSpec(static_cast<DisplayField>(200)));
}

void test_field_hysteresis_values() {
    TEST_ASSERT_EQUAL_FLOAT(1.0f, Display::fieldHysteresis(DisplayField::Humidity));
    TEST_ASSERT_EQUAL_FLOAT(0.1f, Display::fieldHysteresis(DisplayField::DewPoint));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, Display::fieldHysteresis(DisplayField::Pressure));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, Display::fieldHysteresis(DisplayField::SeaLevelPressure));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, Display::fieldHysteresis(DisplayField::CO2));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, Display::fieldHysteresis(DisplayField::Illuminance));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, Display::fieldHysteresis(DisplayField::VocIndex));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, Display::fieldHysteresis(DisplayField::PM25));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, Display::fieldHysteresis(DisplayField::None));
}

void test_only_dew_point_needs_degree_ring() {
    TEST_ASSERT_TRUE(Display::fieldNeedsDegreeRing(DisplayField::DewPoint));
    TEST_ASSERT_FALSE(Display::fieldNeedsDegreeRing(DisplayField::Humidity));
    TEST_ASSERT_FALSE(Display::fieldNeedsDegreeRing(DisplayField::Pressure));
    TEST_ASSERT_FALSE(Display::fieldNeedsDegreeRing(DisplayField::SeaLevelPressure));
    TEST_ASSERT_FALSE(Display::fieldNeedsDegreeRing(DisplayField::CO2));
    TEST_ASSERT_FALSE(Display::fieldNeedsDegreeRing(DisplayField::Illuminance));
    TEST_ASSERT_FALSE(Display::fieldNeedsDegreeRing(DisplayField::VocIndex));
    TEST_ASSERT_FALSE(Display::fieldNeedsDegreeRing(DisplayField::PM25));
    TEST_ASSERT_FALSE(Display::fieldNeedsDegreeRing(DisplayField::None));
}

// --- variant extraction ---

void test_variant_extraction_float() {
    const Measurement m = floatMeasurement(MeasurementType::Pressure, 1013.25f);
    float value = 0.0f;
    TEST_ASSERT_TRUE(Display::measurementValueAsFloat(m, value));
    TEST_ASSERT_EQUAL_FLOAT(1013.25f, value);
}

void test_variant_extraction_int32() {
    const Measurement m = intMeasurement(MeasurementType::CO2, 812);
    float value = 0.0f;
    TEST_ASSERT_TRUE(Display::measurementValueAsFloat(m, value));
    TEST_ASSERT_EQUAL_FLOAT(812.0f, value);
}

// --- formatting ---

void test_format_humidity_line() {
    char buf[32];
    const FieldReading reading{47.4f, true};
    TEST_ASSERT_EQUAL(6, Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::Humidity, reading));
    TEST_ASSERT_EQUAL_STRING("47 %rH", buf);
}

void test_format_dew_point_line_one_decimal() {
    char buf[32];
    const FieldReading reading{12.44f, true};
    TEST_ASSERT_EQUAL(7, Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::DewPoint, reading));
    TEST_ASSERT_EQUAL_STRING("DP 12.4", buf);
}

void test_format_pressure_lines() {
    char buf[32];
    const FieldReading pressure{1013.4f, true};
    Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::Pressure, pressure);
    TEST_ASSERT_EQUAL_STRING("P 1013 hPa", buf);

    const FieldReading seaLevel{1016.2f, true};
    Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::SeaLevelPressure, seaLevel);
    TEST_ASSERT_EQUAL_STRING("1016 hPa", buf);
}

void test_format_co2_int32_line() {
    char buf[32];
    const FieldReading reading{812.0f, true};
    Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::CO2, reading);
    TEST_ASSERT_EQUAL_STRING("CO2 812 ppm", buf);
}

void test_format_illuminance_line() {
    char buf[32];
    const FieldReading reading{320.2f, true};
    Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::Illuminance, reading);
    TEST_ASSERT_EQUAL_STRING("320 lux", buf);
}

void test_format_voc_line() {
    char buf[32];
    const FieldReading reading{120.0f, true};
    Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::VocIndex, reading);
    TEST_ASSERT_EQUAL_STRING("VOC 120", buf);
}

void test_format_pm25_ascii_unit() {
    char buf[32];
    const FieldReading reading{8.0f, true};
    Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::PM25, reading);
    TEST_ASSERT_EQUAL_STRING("PM2.5 8 ug/m3", buf);
}

void test_placeholders_keep_label_and_unit() {
    char buf[32];
    const FieldReading missing{NAN, false};

    Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::Humidity, missing);
    TEST_ASSERT_EQUAL_STRING("-- %rH", buf);

    Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::DewPoint, missing);
    TEST_ASSERT_EQUAL_STRING("DP --", buf);

    Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::Pressure, missing);
    TEST_ASSERT_EQUAL_STRING("P -- hPa", buf);

    Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::SeaLevelPressure, missing);
    TEST_ASSERT_EQUAL_STRING("-- hPa", buf);

    Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::CO2, missing);
    TEST_ASSERT_EQUAL_STRING("CO2 -- ppm", buf);

    Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::Illuminance, missing);
    TEST_ASSERT_EQUAL_STRING("-- lux", buf);

    Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::VocIndex, missing);
    TEST_ASSERT_EQUAL_STRING("VOC --", buf);

    Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::PM25, missing);
    TEST_ASSERT_EQUAL_STRING("PM2.5 -- ug/m3", buf);
}

void test_format_none_writes_empty_line() {
    char buf[32] = "untouched";
    TEST_ASSERT_EQUAL(0, Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::None, FieldReading{}));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_format_handles_null_and_zero_length() {
    char buf[32] = "untouched";
    const FieldReading reading{47.0f, true};
    TEST_ASSERT_EQUAL(0, Display::formatDisplayFieldLine(nullptr, 32, DisplayField::Humidity, reading));
    TEST_ASSERT_EQUAL(0, Display::formatDisplayFieldLine(buf, 0, DisplayField::Humidity, reading));
    TEST_ASSERT_EQUAL_STRING("untouched", buf);
}

// The widest rendered line fits the fixed vocabulary's width budget; see the
// measurement note in src/display/DisplayField.h (~155 px at FreeSans12pt7b
// on the 200 px panel). This guard catches a future field that outgrows the
// display buffer before it reaches the panel.
void test_widest_line_fits_the_shared_buffer() {
    char buf[32];
    const FieldReading reading{8.0f, true};
    Display::formatDisplayFieldLine(buf, sizeof(buf), DisplayField::PM25, reading);
    TEST_ASSERT_EQUAL_STRING("PM2.5 8 ug/m3", buf);
    TEST_ASSERT_TRUE(strlen(buf) + 1 <= 32);
}

// --- availability ---

void test_invalid_snapshot_makes_every_field_unavailable() {
    const auto measurements = measurementVector({floatMeasurement(MeasurementType::RelativeHumidity, 47.0f),
                                                 floatMeasurement(MeasurementType::Pressure, 1013.0f)});

    TEST_ASSERT_FALSE(readOrDefault(DisplayField::Humidity, measurements, false).available);
    TEST_ASSERT_FALSE(readOrDefault(DisplayField::Pressure, measurements, false).available);
}

void test_available_fields_read_float_and_int_measurements() {
    const auto measurements = measurementVector({floatMeasurement(MeasurementType::RelativeHumidity, 47.0f),
                                                 floatMeasurement(MeasurementType::Pressure, 1013.0f),
                                                 intMeasurement(MeasurementType::CO2, 812)});

    const FieldReading humidity = readOrDefault(DisplayField::Humidity, measurements, true);
    TEST_ASSERT_TRUE(humidity.available);
    TEST_ASSERT_EQUAL_FLOAT(47.0f, humidity.value);

    const FieldReading co2 = readOrDefault(DisplayField::CO2, measurements, true);
    TEST_ASSERT_TRUE(co2.available);
    TEST_ASSERT_EQUAL_FLOAT(812.0f, co2.value);
}

void test_pressure_only_snapshot_still_has_pressure() {
    const auto measurements = measurementVector({floatMeasurement(MeasurementType::Pressure, 1013.0f)});
    const FieldReading pressure = readOrDefault(DisplayField::Pressure, measurements, true);
    TEST_ASSERT_TRUE(pressure.available);
    TEST_ASSERT_EQUAL_FLOAT(1013.0f, pressure.value);

    // No temperature in the snapshot: the primary slot is unavailable, but
    // that must not make the pressure slot unavailable too.
    TEST_ASSERT_FALSE(readOrDefault(DisplayField::Humidity, measurements, true).available);
}

void test_sea_level_requires_elevation() {
    const auto measurements = measurementVector({floatMeasurement(MeasurementType::SeaLevelPressure, 1016.0f)});

    TEST_ASSERT_FALSE(readOrDefault(DisplayField::SeaLevelPressure, measurements, true, 0.0f).available);
    TEST_ASSERT_FALSE(readOrDefault(DisplayField::SeaLevelPressure, measurements, true, -5.0f).available);

    const FieldReading available = readOrDefault(DisplayField::SeaLevelPressure, measurements, true, 250.0f);
    TEST_ASSERT_TRUE(available.available);
    TEST_ASSERT_EQUAL_FLOAT(1016.0f, available.value);
}

void test_voc_requires_temperature_and_humidity() {
    const auto vocOnly = measurementVector({intMeasurement(MeasurementType::VocIndex, 120)});
    TEST_ASSERT_FALSE(readOrDefault(DisplayField::VocIndex, vocOnly, true).available);

    const auto withoutHumidity = measurementVector(
        {intMeasurement(MeasurementType::VocIndex, 120), floatMeasurement(MeasurementType::Temperature, 21.4f)});
    TEST_ASSERT_FALSE(readOrDefault(DisplayField::VocIndex, withoutHumidity, true).available);

    const auto compensated = measurementVector({intMeasurement(MeasurementType::VocIndex, 120),
                                                floatMeasurement(MeasurementType::Temperature, 21.4f),
                                                floatMeasurement(MeasurementType::RelativeHumidity, 47.0f)});
    const FieldReading reading = readOrDefault(DisplayField::VocIndex, compensated, true);
    TEST_ASSERT_TRUE(reading.available);
    TEST_ASSERT_EQUAL_FLOAT(120.0f, reading.value);
}

void test_nan_or_missing_measurement_is_unavailable() {
    const auto nanHumidity = measurementVector({floatMeasurement(MeasurementType::RelativeHumidity, NAN)});
    TEST_ASSERT_FALSE(readOrDefault(DisplayField::Humidity, nanHumidity, true).available);

    const auto empty = std::vector<Measurement>{};
    TEST_ASSERT_FALSE(readOrDefault(DisplayField::Pressure, empty, true).available);
    TEST_ASSERT_FALSE(readOrDefault(DisplayField::None, empty, true).available);
}

int runUnityTests() {
    UNITY_BEGIN();
    // Metadata
    RUN_TEST(test_field_spec_measurement_types);
    RUN_TEST(test_field_spec_none_and_invalid_have_no_spec);
    RUN_TEST(test_field_hysteresis_values);
    RUN_TEST(test_only_dew_point_needs_degree_ring);
    // Variant extraction
    RUN_TEST(test_variant_extraction_float);
    RUN_TEST(test_variant_extraction_int32);
    // Formatting
    RUN_TEST(test_format_humidity_line);
    RUN_TEST(test_format_dew_point_line_one_decimal);
    RUN_TEST(test_format_pressure_lines);
    RUN_TEST(test_format_co2_int32_line);
    RUN_TEST(test_format_illuminance_line);
    RUN_TEST(test_format_voc_line);
    RUN_TEST(test_format_pm25_ascii_unit);
    RUN_TEST(test_placeholders_keep_label_and_unit);
    RUN_TEST(test_format_none_writes_empty_line);
    RUN_TEST(test_format_handles_null_and_zero_length);
    RUN_TEST(test_widest_line_fits_the_shared_buffer);
    // Availability
    RUN_TEST(test_invalid_snapshot_makes_every_field_unavailable);
    RUN_TEST(test_available_fields_read_float_and_int_measurements);
    RUN_TEST(test_pressure_only_snapshot_still_has_pressure);
    RUN_TEST(test_sea_level_requires_elevation);
    RUN_TEST(test_voc_requires_temperature_and_humidity);
    RUN_TEST(test_nan_or_missing_measurement_is_unavailable);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
