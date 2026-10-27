#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "MqttClient.h"
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

// --- control-state payloads (mqtt-integration → "Control and actuator state is published") ---

void test_setpoint_payload_is_a_number() {
    char out[16];
    const int n = Net::formatSetpointPayload(out, sizeof(out), 21.5f);
    TEST_ASSERT_EQUAL_STRING("21.5", out);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(strlen(out)), n);

    Net::formatSetpointPayload(out, sizeof(out), 22.0f);
    TEST_ASSERT_EQUAL_STRING("22.0", out);
}

void test_control_enabled_payload_is_on_or_off() {
    char out[8];
    Net::formatControlEnabledPayload(out, sizeof(out), true);
    TEST_ASSERT_EQUAL_STRING("ON", out);
    Net::formatControlEnabledPayload(out, sizeof(out), false);
    TEST_ASSERT_EQUAL_STRING("OFF", out);
}

void test_control_output_payload_is_a_percentage() {
    char out[8];
    Net::formatControlOutputPayload(out, sizeof(out), 0.0f);
    TEST_ASSERT_EQUAL_STRING("0", out);
    Net::formatControlOutputPayload(out, sizeof(out), 0.5f);
    TEST_ASSERT_EQUAL_STRING("50", out);
    Net::formatControlOutputPayload(out, sizeof(out), 1.0f);
    TEST_ASSERT_EQUAL_STRING("100", out);
}

void test_control_output_payload_clamps_and_survives_nan() {
    char out[8];
    // Out-of-range demands cannot leak past the [0, 100] contract.
    Net::formatControlOutputPayload(out, sizeof(out), 1.4f);
    TEST_ASSERT_EQUAL_STRING("100", out);
    Net::formatControlOutputPayload(out, sizeof(out), -0.5f);
    TEST_ASSERT_EQUAL_STRING("0", out);
    // NaN would print as "nan" and be rejected by the subscriber.
    Net::formatControlOutputPayload(out, sizeof(out), std::nanf(""));
    TEST_ASSERT_EQUAL_STRING("0", out);
}

void test_heating_payload_is_on_or_off() {
    char out[8];
    Net::formatHeatingPayload(out, sizeof(out), true);
    TEST_ASSERT_EQUAL_STRING("ON", out);
    Net::formatHeatingPayload(out, sizeof(out), false);
    TEST_ASSERT_EQUAL_STRING("OFF", out);
}

// --- parseSetpointPayload (inbound command) ---

void test_parse_setpoint_accepts_in_range_numbers() {
    float value = 0.0f;
    TEST_ASSERT_TRUE(Net::parseSetpointPayload("23.5", value));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 23.5f, value);

    // A trailing newline, as a shell `mosquitto_pub` may add, is tolerated.
    TEST_ASSERT_TRUE(Net::parseSetpointPayload("10.0\n", value));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, value);

    TEST_ASSERT_TRUE(Net::parseSetpointPayload("30", value));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.0f, value);
}

void test_parse_setpoint_rejects_out_of_range() {
    float value = 0.0f;
    TEST_ASSERT_FALSE(Net::parseSetpointPayload("40.0", value));
    TEST_ASSERT_FALSE(Net::parseSetpointPayload("9.9", value));
    TEST_ASSERT_FALSE(Net::parseSetpointPayload("-5", value));
}

void test_parse_setpoint_rejects_non_numeric() {
    float value = 0.0f;
    TEST_ASSERT_FALSE(Net::parseSetpointPayload("", value));
    TEST_ASSERT_FALSE(Net::parseSetpointPayload("warm", value));
    TEST_ASSERT_FALSE(Net::parseSetpointPayload("12abc", value)); // partial parse
    TEST_ASSERT_FALSE(Net::parseSetpointPayload("ON", value));
}

void test_parse_setpoint_rejects_non_finite() {
    float value = 0.0f;
    TEST_ASSERT_FALSE(Net::parseSetpointPayload("nan", value));
    TEST_ASSERT_FALSE(Net::parseSetpointPayload("inf", value));
    TEST_ASSERT_FALSE(Net::parseSetpointPayload("-inf", value));
}

// --- Home Assistant discovery payload ---

namespace {

    /**
     * Depth-aware scan that returns the immediate member names of the JSON
     * object following `"key":{`. Enough of a parser to prove the payload is
     * structurally sound and to enumerate its components, without adding a
     * JSON dependency to the native environment.
     */
    std::vector<std::string> memberNames(const std::string& json, const std::string& key) {
        std::vector<std::string> names;
        const std::string marker = "\"" + key + "\":{";
        const size_t pos = json.find(marker);
        if (pos == std::string::npos) return names;

        int depth = 1;
        bool inString = false;
        bool atKeyStart = true; // immediately after '{' or a depth-1 ','
        std::string current;
        for (size_t i = pos + marker.size(); i < json.size() && depth > 0; ++i) {
            const char c = json[i];
            if (inString) {
                if (c == '\\') {
                    ++i;
                } else if (c == '"') {
                    inString = false;
                    if (depth == 1 && atKeyStart) {
                        names.push_back(current);
                        atKeyStart = false;
                    }
                } else if (depth == 1 && atKeyStart) {
                    current.push_back(c);
                }
                continue;
            }
            if (c == '"') {
                inString = true;
                current.clear();
            } else if (c == '{' || c == '[') {
                ++depth;
            } else if (c == '}' || c == ']') {
                --depth;
            } else if (c == ',' && depth == 1) {
                atKeyStart = true;
            }
        }
        return names;
    }

    bool hasMember(const std::vector<std::string>& names, const char* want) {
        for (const auto& n : names) {
            if (n == want) return true;
        }
        return false;
    }

    size_t countOccurrences(const std::string& haystack, const std::string& needle) {
        size_t count = 0;
        for (size_t pos = haystack.find(needle); pos != std::string::npos;
             pos = haystack.find(needle, pos + needle.size())) {
            ++count;
        }
        return count;
    }

    std::string discoveryPayload(const char* prefix = "sensors", const char* name = "Bedroom") {
        char out[2048];
        const int n = Net::formatHaDiscoveryPayload(out, sizeof(out), "AABBCC", name, prefix, "v1.2.3");
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, n);
        TEST_ASSERT_LESS_THAN_INT(static_cast<int>(sizeof(out)), n); // not truncated
        TEST_ASSERT_EQUAL_INT(static_cast<int>(strlen(out)), n);
        return std::string(out);
    }

} // namespace

void test_discovery_topic() {
    char topic[128];
    const int n = Net::formatHaDiscoveryTopic(topic, sizeof(topic), "AABBCC");
    TEST_ASSERT_EQUAL_STRING("homeassistant/device/AABBCC/config", topic);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(strlen(topic)), n);
}

void test_discovery_payload_device_block_and_components() {
    const std::string json = discoveryPayload();

    // Device identifiers. The `~` base topic is declared per component (Home
    // Assistant only substitutes it there for device discovery), so it appears
    // once for each of the five components.
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"ids\":\"AABBCC\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"name\":\"Bedroom\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"mf\":\"Klima-Control\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"sw\":\"v1.2.3\""));
    TEST_ASSERT_EQUAL_UINT(5, countOccurrences(json, "\"~\":\"sensors\""));

    const std::vector<std::string> dev = memberNames(json, "dev");
    TEST_ASSERT_TRUE(hasMember(dev, "ids"));
    TEST_ASSERT_TRUE(hasMember(dev, "name"));
    TEST_ASSERT_TRUE(hasMember(dev, "mf"));
    TEST_ASSERT_TRUE(hasMember(dev, "mdl"));
    TEST_ASSERT_TRUE(hasMember(dev, "sw"));

    // The origin block is mandatory for device discovery.
    const std::vector<std::string> origin = memberNames(json, "o");
    TEST_ASSERT_TRUE(hasMember(origin, "name"));
    TEST_ASSERT_TRUE(hasMember(origin, "sw"));

    // Exactly the five documented components, each a platform with a unique id.
    const std::vector<std::string> cmps = memberNames(json, "cmps");
    TEST_ASSERT_EQUAL_UINT(5, cmps.size());
    TEST_ASSERT_TRUE(hasMember(cmps, "climate"));
    TEST_ASSERT_TRUE(hasMember(cmps, "humidity"));
    TEST_ASSERT_TRUE(hasMember(cmps, "dew_point"));
    TEST_ASSERT_TRUE(hasMember(cmps, "control_output"));
    TEST_ASSERT_TRUE(hasMember(cmps, "heating"));

    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"unique_id\":\"AABBCC_climate\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"unique_id\":\"AABBCC_humidity\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"unique_id\":\"AABBCC_dew_point\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"unique_id\":\"AABBCC_control_output\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"unique_id\":\"AABBCC_heating\""));
}

void test_discovery_payload_topics() {
    const std::string json = discoveryPayload();

    // Availability and the five components' topics.
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"avty_t\":\"~/status\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"p\":\"climate\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"curr_temp_t\":\"~/temperature\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"temp_stat_t\":\"~/target_temperature\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"temp_cmd_t\":\"~/target_temperature/set\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"mode_stat_t\":\"~/control_enabled\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"mode_cmd_t\":\"~/control_enabled/set\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"stat_t\":\"~/relative humidity\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"stat_t\":\"~/dew point\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"stat_t\":\"~/control_output\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"stat_t\":\"~/heating\""));

    // The climate mode mapping the spec pins: ON -> heat, OFF -> off.
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "{{ 'heat' if value == 'ON' else 'off' }}"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "{{ 'ON' if value == 'heat' else 'OFF' }}"));

    // The setpoint bounds advertised to Home Assistant are the accepted range.
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"min_temp\":10,\"max_temp\":30"));
}

void test_discovery_payload_is_well_formed() {
    const std::string json = discoveryPayload();
    // Balanced braces and brackets: a truncation or a stray quote would leave
    // these mismatched, which Home Assistant would reject silently.
    int braces = 0;
    int brackets = 0;
    bool inString = false;
    for (size_t i = 0; i < json.size(); ++i) {
        const char c = json[i];
        if (inString) {
            if (c == '\\') {
                ++i;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            ++braces;
        } else if (c == '}') {
            --braces;
        } else if (c == '[') {
            ++brackets;
        } else if (c == ']') {
            --brackets;
        }
    }
    TEST_ASSERT_EQUAL_INT(0, braces);
    TEST_ASSERT_EQUAL_INT(0, brackets);
    TEST_ASSERT_FALSE(inString);
}

void test_discovery_payload_fits_worst_case() {
    // Longest permitted prefix (63 chars) and device name (31 chars), which is
    // what the design's ~1.5 kB estimate is measured against.
    const std::string prefix(63, 'p');
    const std::string name(31, 'n');
    const std::string deviceId = "AABBCC";
    const std::string version = "v9.9.9-99-gabcdef123";

    char topic[128];
    Net::formatHaDiscoveryTopic(topic, sizeof(topic), deviceId.c_str());

    char payload[2048];
    const int n = Net::formatHaDiscoveryPayload(payload, sizeof(payload), deviceId.c_str(), name.c_str(),
                                                prefix.c_str(), version.c_str());
    TEST_ASSERT_LESS_THAN_INT(static_cast<int>(sizeof(payload)), n); // fits the builder's own buffer

    const size_t framed = strlen(topic) + strlen(payload) + MqttClient::MQTT_FRAMING_OVERHEAD;
    TEST_ASSERT_LESS_THAN_UINT(MqttClient::MQTT_BUFFER_SIZE + 1u, framed);

    // Pin the headroom, not just "fits": an added field should fail here while
    // it is still a small edit, rather than surfacing as a silent truncation
    // on a device with a degraded buffer.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(128u, MqttClient::MQTT_BUFFER_SIZE - framed);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_float_measurement_has_two_decimals);
    RUN_TEST(test_integer_measurement_prints_as_integer);
    RUN_TEST(test_calculated_flag_and_unsynced_epoch);
    RUN_TEST(test_truncation_reports_full_length);
    RUN_TEST(test_setpoint_payload_is_a_number);
    RUN_TEST(test_control_enabled_payload_is_on_or_off);
    RUN_TEST(test_control_output_payload_is_a_percentage);
    RUN_TEST(test_control_output_payload_clamps_and_survives_nan);
    RUN_TEST(test_heating_payload_is_on_or_off);
    RUN_TEST(test_parse_setpoint_accepts_in_range_numbers);
    RUN_TEST(test_parse_setpoint_rejects_out_of_range);
    RUN_TEST(test_parse_setpoint_rejects_non_numeric);
    RUN_TEST(test_parse_setpoint_rejects_non_finite);
    RUN_TEST(test_discovery_topic);
    RUN_TEST(test_discovery_payload_device_block_and_components);
    RUN_TEST(test_discovery_payload_topics);
    RUN_TEST(test_discovery_payload_is_well_formed);
    RUN_TEST(test_discovery_payload_fits_worst_case);
    return UNITY_END();
}
