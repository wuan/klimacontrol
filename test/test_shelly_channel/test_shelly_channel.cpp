#include "unity.h"

#include <cstring>

#include "actuator/ShellyChannel.h"

using Actuator::ChannelConfig;
using Actuator::Conformance;

void setUp() {}
void tearDown() {}

namespace {
    // Captured verbatim from the real manifolds on 2026-09-02. Kept exactly as
    // the devices emit them — including the irregular spacing after commas,
    // which is the sort of thing a hand-written parser gets wrong.
    const char *GAESTEBAD =
        R"({"id":3, "name":"Gästebad","in_mode":"detached","in_locked":false,)"
        R"("initial_state":"restore_last", "auto_on":false, "auto_on_delay":60.00, )"
        R"("auto_off":false, "auto_off_delay":60.00,"power_limit":4480,"voltage_limit":280,)"
        R"("undervoltage_limit":0,"autorecover_voltage_errors":false,"current_limit":16.000,)"
        R"("reverse":false,"counts":{"enable":true,"power_thr":100}})";

    // The bathroom fan, which is the one device on the network that already has
    // a lease configured. Different firmware (1.7.5), slightly different shape.
    const char *BATH_FAN =
        R"({"id":0, "name":null,"in_mode":"follow","in_locked":false,)"
        R"("initial_state":"match_input", "auto_on":false, "auto_on_delay":60.00, )"
        R"("auto_off":true, "auto_off_delay":300.00,"power_limit":4480,)"
        R"("voltage_limit":280,"autorecover_voltage_errors":false,"current_limit":16.000})";

    // What a correctly prepared heating channel will look like after the
    // cutover runbook has been applied to it.
    const char *CONFORMING =
        R"({"id":3, "name":"Gästebad","in_mode":"detached","in_locked":false,)"
        R"("initial_state":"off", "auto_on":false, "auto_on_delay":60.00, )"
        R"("auto_off":true, "auto_off_delay":180.00,"power_limit":4480})";

    constexpr float MIN_DELAY = 120.0f;
}

// --- field extraction ---

void test_extract_bool_distinguishes_similar_keys() {
    bool v = true;
    // "auto_off" must not match inside "auto_off_delay" — the one collision
    // that actually occurs in these payloads.
    TEST_ASSERT_TRUE(Actuator::extractBool(GAESTEBAD, "auto_off", v));
    TEST_ASSERT_FALSE(v);
    TEST_ASSERT_TRUE(Actuator::extractBool(BATH_FAN, "auto_off", v));
    TEST_ASSERT_TRUE(v);
}

void test_extract_number_handles_spacing() {
    float v = 0.0f;
    TEST_ASSERT_TRUE(Actuator::extractNumber(GAESTEBAD, "auto_off_delay", v));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, v);
    TEST_ASSERT_TRUE(Actuator::extractNumber(BATH_FAN, "auto_off_delay", v));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 300.0f, v);
}

void test_extract_string() {
    char buf[20];
    TEST_ASSERT_TRUE(Actuator::extractString(GAESTEBAD, "in_mode", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("detached", buf);
    TEST_ASSERT_TRUE(Actuator::extractString(GAESTEBAD, "initial_state", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("restore_last", buf);
}

void test_extract_missing_key_fails() {
    bool b = true;
    float f = 1.0f;
    char buf[8];
    TEST_ASSERT_FALSE(Actuator::extractBool(GAESTEBAD, "nope", b));
    TEST_ASSERT_FALSE(Actuator::extractNumber(GAESTEBAD, "nope", f));
    TEST_ASSERT_FALSE(Actuator::extractString(GAESTEBAD, "nope", buf, sizeof(buf)));
}

void test_extract_null_value_is_not_a_string() {
    // The bath fan reports "name":null. A parser that returned an empty string
    // here would be indistinguishable from a genuinely empty name.
    char buf[20];
    TEST_ASSERT_FALSE(Actuator::extractString(BATH_FAN, "name", buf, sizeof(buf)));
}

void test_extract_truncated_string_reports_failure() {
    // A value longer than the buffer must fail rather than silently return a
    // prefix that might compare equal to something expected.
    char buf[5];
    TEST_ASSERT_FALSE(Actuator::extractString(GAESTEBAD, "initial_state", buf, sizeof(buf)));
}

void test_extract_handles_null_json() {
    bool b = false;
    TEST_ASSERT_FALSE(Actuator::extractBool(nullptr, "auto_off", b));
}

// --- parsing ---

void test_parse_real_gaestebad_payload() {
    ChannelConfig c;
    TEST_ASSERT_TRUE(c.parse(GAESTEBAD));
    TEST_ASSERT_TRUE(c.read);
    TEST_ASSERT_FALSE(c.autoOff);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, c.autoOffDelayS);
    TEST_ASSERT_EQUAL_STRING("restore_last", c.initialState);
    TEST_ASSERT_EQUAL_STRING("detached", c.inMode);
}

void test_parse_garbage_is_not_read() {
    ChannelConfig c;
    TEST_ASSERT_FALSE(c.parse("not json at all"));
    TEST_ASSERT_FALSE(c.read);
}

// --- conformance ---
//
// This predicate is the gate that decides whether the firmware will put heat
// into a room. Each failure mode gets its own case.

void test_gaestebad_as_found_is_refused() {
    // Exactly the state every heating channel is in today: no lease, and a
    // power-on state that restores the previous output.
    ChannelConfig c;
    c.parse(GAESTEBAD);
    TEST_ASSERT_EQUAL(static_cast<int>(Conformance::AutoOffDisabled),
                      static_cast<int>(Actuator::checkConformance(c, MIN_DELAY)));
}

void test_conforming_channel_is_accepted() {
    ChannelConfig c;
    TEST_ASSERT_TRUE(c.parse(CONFORMING));
    TEST_ASSERT_EQUAL(static_cast<int>(Conformance::Ok),
                      static_cast<int>(Actuator::checkConformance(c, MIN_DELAY)));
}

void test_unread_config_is_refused() {
    ChannelConfig c; // never parsed
    TEST_ASSERT_EQUAL(static_cast<int>(Conformance::NotRead),
                      static_cast<int>(Actuator::checkConformance(c, MIN_DELAY)));
}

void test_short_lease_is_refused() {
    ChannelConfig c;
    c.parse(CONFORMING);
    c.autoOffDelayS = 30.0f; // shorter than one renewal cycle would tolerate
    TEST_ASSERT_EQUAL(static_cast<int>(Conformance::AutoOffTooShort),
                      static_cast<int>(Actuator::checkConformance(c, MIN_DELAY)));
}

void test_lease_exactly_at_the_minimum_is_accepted() {
    ChannelConfig c;
    c.parse(CONFORMING);
    c.autoOffDelayS = MIN_DELAY;
    TEST_ASSERT_EQUAL(static_cast<int>(Conformance::Ok),
                      static_cast<int>(Actuator::checkConformance(c, MIN_DELAY)));
}

void test_restore_last_is_refused() {
    ChannelConfig c;
    c.parse(CONFORMING);
    std::strcpy(c.initialState, "restore_last");
    TEST_ASSERT_EQUAL(static_cast<int>(Conformance::InitialStateUnsafe),
                      static_cast<int>(Actuator::checkConformance(c, MIN_DELAY)));
}

void test_match_input_is_refused() {
    // The bath fan's setting. Safe for a fan, not for a valve.
    ChannelConfig c;
    c.parse(CONFORMING);
    std::strcpy(c.initialState, "match_input");
    TEST_ASSERT_EQUAL(static_cast<int>(Conformance::InitialStateUnsafe),
                      static_cast<int>(Actuator::checkConformance(c, MIN_DELAY)));
}

void test_attached_input_is_refused() {
    ChannelConfig c;
    c.parse(CONFORMING);
    std::strcpy(c.inMode, "follow");
    TEST_ASSERT_EQUAL(static_cast<int>(Conformance::InputNotDetached),
                      static_cast<int>(Actuator::checkConformance(c, MIN_DELAY)));
}

void test_bath_fan_would_be_refused_despite_having_a_lease() {
    // It has auto_off, which is why it was the proof the mechanism works — but
    // match_input and follow make it wrong for a valve. Guards against the
    // temptation to treat "has a lease" as sufficient.
    ChannelConfig c;
    TEST_ASSERT_TRUE(c.parse(BATH_FAN));
    TEST_ASSERT_EQUAL(static_cast<int>(Conformance::InitialStateUnsafe),
                      static_cast<int>(Actuator::checkConformance(c, MIN_DELAY)));
}

void test_every_conformance_value_has_a_name_and_detail() {
    const Conformance all[] = {Conformance::Ok,
                               Conformance::NotRead,
                               Conformance::AutoOffDisabled,
                               Conformance::AutoOffTooShort,
                               Conformance::InitialStateUnsafe,
                               Conformance::InputNotDetached};
    for (Conformance c : all) {
        TEST_ASSERT_NOT_NULL(Actuator::conformanceName(c));
        TEST_ASSERT_TRUE(std::strcmp(Actuator::conformanceName(c), "unknown") != 0);
        TEST_ASSERT_NOT_NULL(Actuator::conformanceDetail(c));
        TEST_ASSERT_TRUE(std::strlen(Actuator::conformanceDetail(c)) > 10);
    }
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_extract_bool_distinguishes_similar_keys);
    RUN_TEST(test_extract_number_handles_spacing);
    RUN_TEST(test_extract_string);
    RUN_TEST(test_extract_missing_key_fails);
    RUN_TEST(test_extract_null_value_is_not_a_string);
    RUN_TEST(test_extract_truncated_string_reports_failure);
    RUN_TEST(test_extract_handles_null_json);
    RUN_TEST(test_parse_real_gaestebad_payload);
    RUN_TEST(test_parse_garbage_is_not_read);
    RUN_TEST(test_gaestebad_as_found_is_refused);
    RUN_TEST(test_conforming_channel_is_accepted);
    RUN_TEST(test_unread_config_is_refused);
    RUN_TEST(test_short_lease_is_refused);
    RUN_TEST(test_lease_exactly_at_the_minimum_is_accepted);
    RUN_TEST(test_restore_last_is_refused);
    RUN_TEST(test_match_input_is_refused);
    RUN_TEST(test_attached_input_is_refused);
    RUN_TEST(test_bath_fan_would_be_refused_despite_having_a_lease);
    RUN_TEST(test_every_conformance_value_has_a_name_and_detail);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
