#include <unity.h>

#include <cstring>

#include "support/LocalTime.h"

using Support::applyTimezone;
using Support::formatLocalDate;
using Support::formatLocalHhMm;
using Support::isPlausibleTimezone;

// POSIX TZ strings under test.
static const char *TZ_UTC = "UTC0";
static const char *TZ_BERLIN = "CET-1CEST,M3.5.0,M10.5.0/3";        // EU rules
static const char *TZ_NEW_YORK = "EST5EDT,M3.2.0,M11.1.0";          // US rules
static const char *TZ_SYDNEY = "AEST-10AEDT,M10.1.0,M4.1.0/3";      // southern hemisphere
static const char *TZ_KOLKATA = "IST-5:30";                         // fractional offset
static const char *TZ_DUBAI = "<+04>-4";                            // quoted designation
static const char *TZ_PHOENIX = "MST7";                             // no DST at all

// Epochs (verified against Python's datetime, all UTC).
static constexpr uint32_t E_2026_01_15_1200Z = 1768478400; // winter
static constexpr uint32_t E_2026_07_15_1200Z = 1784116800; // summer
static constexpr uint32_t E_EU_DST_BEFORE = 1774745940;    // 2026-03-29T00:59Z
static constexpr uint32_t E_EU_DST_AT = 1774746000;        // 2026-03-29T01:00Z
static constexpr uint32_t E_EU_DST_END_BEFORE = 1792889940; // 2026-10-25T00:59Z
static constexpr uint32_t E_EU_DST_END_AT = 1792890000;     // 2026-10-25T01:00Z
static constexpr uint32_t E_US_DST_BEFORE = 1772953140;     // 2026-03-08T06:59Z
static constexpr uint32_t E_US_DST_AT = 1772953200;         // 2026-03-08T07:00Z

void setUp() {}
void tearDown() {}

// Convenience: apply a zone, format, and compare.
static void assertHhMm(const char *tz, uint32_t epoch, const char *expected) {
    applyTimezone(tz);
    char buf[8];
    formatLocalHhMm(buf, sizeof(buf), epoch);
    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

// --- UTC baseline ---

void test_utc_passthrough() {
    assertHhMm(TZ_UTC, E_2026_01_15_1200Z, "12:00");
    assertHhMm(TZ_UTC, E_2026_07_15_1200Z, "12:00");
}

void test_default_timezone_is_utc() {
    // A null or empty zone must degrade to UTC, not to undefined behaviour.
    assertHhMm(nullptr, E_2026_01_15_1200Z, "12:00");
    assertHhMm("", E_2026_01_15_1200Z, "12:00");
}

// --- EU daylight saving ---

void test_berlin_winter_is_utc_plus_one() {
    assertHhMm(TZ_BERLIN, E_2026_01_15_1200Z, "13:00");
}

void test_berlin_summer_is_utc_plus_two() {
    assertHhMm(TZ_BERLIN, E_2026_07_15_1200Z, "14:00");
}

void test_berlin_dst_start_skips_an_hour() {
    // 01:59 CET is followed immediately by 03:00 CEST; 02:00 never occurs.
    assertHhMm(TZ_BERLIN, E_EU_DST_BEFORE, "01:59");
    assertHhMm(TZ_BERLIN, E_EU_DST_AT, "03:00");
}

void test_berlin_dst_end_repeats_an_hour() {
    // 02:59 CEST falls back to 02:00 CET.
    assertHhMm(TZ_BERLIN, E_EU_DST_END_BEFORE, "02:59");
    assertHhMm(TZ_BERLIN, E_EU_DST_END_AT, "02:00");
}

// --- US rules differ from EU rules ---

void test_new_york_dst_starts_on_a_different_date() {
    // US switches on the second Sunday of March, weeks before the EU. This is
    // the case a single hardcoded "EU DST" rule would get wrong.
    assertHhMm(TZ_NEW_YORK, E_US_DST_BEFORE, "01:59");
    assertHhMm(TZ_NEW_YORK, E_US_DST_AT, "03:00");
}

void test_eu_still_on_standard_time_when_us_has_switched() {
    // Same instant, two zones, different DST states.
    assertHhMm(TZ_NEW_YORK, E_US_DST_AT, "03:00"); // EDT, UTC-4
    assertHhMm(TZ_BERLIN, E_US_DST_AT, "08:00");   // CET, UTC+1 (not yet CEST)
}

// --- southern hemisphere inverts the seasons ---

void test_sydney_is_on_dst_in_january() {
    // January is summer in Sydney: AEDT, UTC+11.
    assertHhMm(TZ_SYDNEY, E_2026_01_15_1200Z, "23:00");
}

void test_sydney_is_on_standard_time_in_july() {
    // July is winter in Sydney: AEST, UTC+10.
    assertHhMm(TZ_SYDNEY, E_2026_07_15_1200Z, "22:00");
}

// --- unusual but valid POSIX forms ---

void test_fractional_offset() {
    // India is UTC+5:30, no DST.
    assertHhMm(TZ_KOLKATA, E_2026_01_15_1200Z, "17:30");
    assertHhMm(TZ_KOLKATA, E_2026_07_15_1200Z, "17:30");
}

void test_quoted_designation() {
    // Dubai is UTC+4, expressed with a quoted numeric abbreviation.
    assertHhMm(TZ_DUBAI, E_2026_01_15_1200Z, "16:00");
}

void test_zone_without_dst_does_not_shift() {
    // Arizona stays on MST year-round.
    assertHhMm(TZ_PHOENIX, E_2026_01_15_1200Z, "05:00");
    assertHhMm(TZ_PHOENIX, E_2026_07_15_1200Z, "05:00");
}

// --- unsynced sentinel ---

void test_epoch_zero_renders_empty() {
    applyTimezone(TZ_BERLIN);
    char buf[8];
    memset(buf, 'x', sizeof(buf));
    TEST_ASSERT_EQUAL(0, formatLocalHhMm(buf, sizeof(buf), 0));
    // Must be an empty string, NOT "01:00" (the Unix epoch in CET).
    TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_epoch_zero_renders_empty_date() {
    applyTimezone(TZ_BERLIN);
    char buf[16];
    TEST_ASSERT_EQUAL(0, formatLocalDate(buf, sizeof(buf), 0));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

// --- date formatting ---

void test_format_date() {
    applyTimezone(TZ_UTC);
    char buf[16];
    formatLocalDate(buf, sizeof(buf), E_2026_01_15_1200Z);
    TEST_ASSERT_EQUAL_STRING("2026-01-15", buf);
}

void test_format_date_respects_zone_rollover() {
    // 2026-01-15T12:00Z is already the 15th at 23:00 in Sydney; a zone far
    // enough east rolls the date over.
    applyTimezone(TZ_SYDNEY);
    char buf[16];
    formatLocalDate(buf, sizeof(buf), E_2026_01_15_1200Z);
    TEST_ASSERT_EQUAL_STRING("2026-01-15", buf);

    // ...and two hours later it is the 16th locally while still the 15th UTC.
    formatLocalDate(buf, sizeof(buf), E_2026_01_15_1200Z + 7200);
    TEST_ASSERT_EQUAL_STRING("2026-01-16", buf);
}

// --- buffer safety ---

void test_null_and_zero_length_buffers() {
    applyTimezone(TZ_BERLIN);
    char buf[8] = "keepme";
    TEST_ASSERT_EQUAL(0, formatLocalHhMm(nullptr, 8, E_2026_01_15_1200Z));
    TEST_ASSERT_EQUAL(0, formatLocalHhMm(buf, 0, E_2026_01_15_1200Z));
    TEST_ASSERT_EQUAL_STRING("keepme", buf);
    TEST_ASSERT_EQUAL(0, formatLocalDate(nullptr, 16, E_2026_01_15_1200Z));
    TEST_ASSERT_EQUAL(0, formatLocalDate(buf, 0, E_2026_01_15_1200Z));
    TEST_ASSERT_EQUAL_STRING("keepme", buf);
}

// --- validation ---

void test_plausible_timezones_accepted() {
    TEST_ASSERT_TRUE(isPlausibleTimezone(TZ_UTC));
    TEST_ASSERT_TRUE(isPlausibleTimezone(TZ_BERLIN));
    TEST_ASSERT_TRUE(isPlausibleTimezone(TZ_NEW_YORK));
    TEST_ASSERT_TRUE(isPlausibleTimezone(TZ_SYDNEY));
    TEST_ASSERT_TRUE(isPlausibleTimezone(TZ_KOLKATA)); // fractional offset
    TEST_ASSERT_TRUE(isPlausibleTimezone(TZ_DUBAI));   // quoted designation
}

void test_implausible_timezones_rejected() {
    TEST_ASSERT_FALSE(isPlausibleTimezone(nullptr));
    TEST_ASSERT_FALSE(isPlausibleTimezone(""));
    TEST_ASSERT_FALSE(isPlausibleTimezone("CET\t-1")); // control character
    TEST_ASSERT_FALSE(isPlausibleTimezone("CET\n"));

    char tooLong[Support::MAX_TIMEZONE_LEN + 8];
    memset(tooLong, 'A', sizeof(tooLong) - 1);
    tooLong[sizeof(tooLong) - 1] = '\0';
    TEST_ASSERT_FALSE(isPlausibleTimezone(tooLong));
}

void test_max_length_boundary() {
    char atMax[Support::MAX_TIMEZONE_LEN + 1];
    memset(atMax, 'A', Support::MAX_TIMEZONE_LEN);
    atMax[Support::MAX_TIMEZONE_LEN] = '\0';
    TEST_ASSERT_TRUE(isPlausibleTimezone(atMax));
}

int runUnityTests() {
    UNITY_BEGIN();
    // UTC baseline
    RUN_TEST(test_utc_passthrough);
    RUN_TEST(test_default_timezone_is_utc);
    // EU DST
    RUN_TEST(test_berlin_winter_is_utc_plus_one);
    RUN_TEST(test_berlin_summer_is_utc_plus_two);
    RUN_TEST(test_berlin_dst_start_skips_an_hour);
    RUN_TEST(test_berlin_dst_end_repeats_an_hour);
    // Region-specific rules
    RUN_TEST(test_new_york_dst_starts_on_a_different_date);
    RUN_TEST(test_eu_still_on_standard_time_when_us_has_switched);
    RUN_TEST(test_sydney_is_on_dst_in_january);
    RUN_TEST(test_sydney_is_on_standard_time_in_july);
    // Unusual valid forms
    RUN_TEST(test_fractional_offset);
    RUN_TEST(test_quoted_designation);
    RUN_TEST(test_zone_without_dst_does_not_shift);
    // Sentinel
    RUN_TEST(test_epoch_zero_renders_empty);
    RUN_TEST(test_epoch_zero_renders_empty_date);
    // Dates
    RUN_TEST(test_format_date);
    RUN_TEST(test_format_date_respects_zone_rollover);
    // Buffer safety
    RUN_TEST(test_null_and_zero_length_buffers);
    // Validation
    RUN_TEST(test_plausible_timezones_accepted);
    RUN_TEST(test_implausible_timezones_rejected);
    RUN_TEST(test_max_length_boundary);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
