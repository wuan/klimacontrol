#include "unity.h"
#include "TimerScheduler.h"

// Provide millis() for native builds (needed by TimerScheduler internal logic)
static uint32_t mockMillis = 0;
unsigned long millis() { return mockMillis; }

static Config::ConfigManager cfg;
static SensorController *sc = nullptr;
static TimerScheduler *scheduler = nullptr;

void setUp() {
    mockMillis = 0;
    sc = new SensorController(cfg);
    scheduler = new TimerScheduler(cfg, *sc);
}

void tearDown() {
    delete scheduler;
    scheduler = nullptr;
    delete sc;
    sc = nullptr;
}

// ---- getSecondsSinceMidnight ----

void test_seconds_since_midnight_zero_epoch() {
    // Special-case: epoch 0 returns 0
    TEST_ASSERT_EQUAL(0u, scheduler->getSecondsSinceMidnight(0));
}

void test_seconds_since_midnight_at_midnight_utc() {
    // 2024-01-01 00:00:00 UTC = 1704067200
    // seconds since midnight with tz=0 should be 0
    TEST_ASSERT_EQUAL(0u, scheduler->getSecondsSinceMidnight(1704067200u));
}

void test_seconds_since_midnight_at_noon_utc() {
    // 2024-01-01 12:00:00 UTC = 1704067200 + 43200
    TEST_ASSERT_EQUAL(43200u, scheduler->getSecondsSinceMidnight(1704067200u + 43200u));
}

void test_seconds_since_midnight_near_end_of_day() {
    // 2024-01-01 23:59:59 UTC = 1704067200 + 86399
    TEST_ASSERT_EQUAL(86399u, scheduler->getSecondsSinceMidnight(1704067200u + 86399u));
}

void test_seconds_since_midnight_timezone_plus_one() {
    // With tz offset +1h, midnight UTC becomes 01:00 local
    scheduler->setTimezoneOffset(1);
    uint32_t result = scheduler->getSecondsSinceMidnight(1704067200u); // UTC midnight
    TEST_ASSERT_EQUAL(3600u, result); // 01:00 local = 3600 s
}

void test_seconds_since_midnight_timezone_minus_one() {
    // With tz offset -1h, midnight UTC becomes 23:00 local (previous day)
    scheduler->setTimezoneOffset(-1);
    uint32_t result = scheduler->getSecondsSinceMidnight(1704067200u); // UTC midnight
    TEST_ASSERT_EQUAL(82800u, result); // 23:00 = 82800 s
}

// ---- setTimezoneOffset ----

void test_set_timezone_offset_valid_positive() {
    scheduler->setTimezoneOffset(5);
    TEST_ASSERT_EQUAL(5, scheduler->getTimezoneOffset());
}

void test_set_timezone_offset_valid_negative() {
    scheduler->setTimezoneOffset(-8);
    TEST_ASSERT_EQUAL(-8, scheduler->getTimezoneOffset());
}

void test_set_timezone_offset_zero() {
    scheduler->setTimezoneOffset(3);
    scheduler->setTimezoneOffset(0);
    TEST_ASSERT_EQUAL(0, scheduler->getTimezoneOffset());
}

void test_set_timezone_offset_invalid_too_low() {
    scheduler->setTimezoneOffset(0);
    scheduler->setTimezoneOffset(-13); // invalid
    TEST_ASSERT_EQUAL(0, scheduler->getTimezoneOffset()); // unchanged
}

void test_set_timezone_offset_invalid_too_high() {
    scheduler->setTimezoneOffset(0);
    scheduler->setTimezoneOffset(15); // invalid (max is 14)
    TEST_ASSERT_EQUAL(0, scheduler->getTimezoneOffset()); // unchanged
}

// ---- setCountdown ----

void test_set_countdown_sets_timer() {
    bool ok = scheduler->setCountdown(0, 3600,
        Config::TimerAction::ENABLE_CONTROL, 0, 1000u);
    TEST_ASSERT_TRUE(ok);

    const Config::TimersConfig &tc = scheduler->getTimersConfig();
    TEST_ASSERT_TRUE(tc.timers[0].enabled);
    TEST_ASSERT_EQUAL(static_cast<int>(Config::TimerType::COUNTDOWN),
                      static_cast<int>(tc.timers[0].type));
    TEST_ASSERT_EQUAL(static_cast<int>(Config::TimerAction::ENABLE_CONTROL),
                      static_cast<int>(tc.timers[0].action));
    TEST_ASSERT_EQUAL(1000u + 3600u, tc.timers[0].target_time);
    TEST_ASSERT_EQUAL(3600u, tc.timers[0].duration_seconds);
}

void test_set_countdown_invalid_index() {
    bool ok = scheduler->setCountdown(Config::TimersConfig::MAX_TIMERS, 3600,
        Config::TimerAction::ENABLE_CONTROL, 0, 1000u);
    TEST_ASSERT_FALSE(ok);
}

void test_set_countdown_all_slots() {
    for (uint8_t i = 0; i < Config::TimersConfig::MAX_TIMERS; i++) {
        bool ok = scheduler->setCountdown(i, (i + 1) * 60,
            Config::TimerAction::DISABLE_CONTROL, 0, 5000u);
        TEST_ASSERT_TRUE(ok);
    }
    const Config::TimersConfig &tc = scheduler->getTimersConfig();
    for (uint8_t i = 0; i < Config::TimersConfig::MAX_TIMERS; i++) {
        TEST_ASSERT_TRUE(tc.timers[i].enabled);
    }
}

// ---- getRemainingSeconds (COUNTDOWN) ----

void test_get_remaining_seconds_countdown() {
    scheduler->setCountdown(0, 3600,
        Config::TimerAction::ENABLE_CONTROL, 0, 1000u);
    // currentEpoch = 1500, target = 1000 + 3600 = 4600
    uint32_t remaining = scheduler->getRemainingSeconds(0, 1500u);
    TEST_ASSERT_EQUAL(4600u - 1500u, remaining);
}

void test_get_remaining_seconds_already_expired() {
    scheduler->setCountdown(0, 3600,
        Config::TimerAction::ENABLE_CONTROL, 0, 1000u);
    // currentEpoch > target_time
    uint32_t remaining = scheduler->getRemainingSeconds(0, 5000u);
    TEST_ASSERT_EQUAL(0u, remaining);
}

void test_get_remaining_seconds_invalid_index() {
    uint32_t remaining = scheduler->getRemainingSeconds(
        Config::TimersConfig::MAX_TIMERS, 1000u);
    TEST_ASSERT_EQUAL(0u, remaining);
}

void test_get_remaining_seconds_inactive_timer() {
    // Timer not set → not enabled → 0
    uint32_t remaining = scheduler->getRemainingSeconds(0, 1000u);
    TEST_ASSERT_EQUAL(0u, remaining);
}

// ---- setDailyAlarm ----

void test_set_daily_alarm_valid() {
    bool ok = scheduler->setDailyAlarm(0, 7 * 3600,  // 07:00
        Config::TimerAction::ENABLE_CONTROL, 0);
    TEST_ASSERT_TRUE(ok);

    const Config::TimersConfig &tc = scheduler->getTimersConfig();
    TEST_ASSERT_TRUE(tc.timers[0].enabled);
    TEST_ASSERT_EQUAL(static_cast<int>(Config::TimerType::ALARM_DAILY),
                      static_cast<int>(tc.timers[0].type));
    TEST_ASSERT_EQUAL(7u * 3600u, tc.timers[0].target_time);
}

void test_set_daily_alarm_midnight() {
    bool ok = scheduler->setDailyAlarm(1, 0,
        Config::TimerAction::DISABLE_CONTROL, 0);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(0u, scheduler->getTimersConfig().timers[1].target_time);
}

void test_set_daily_alarm_invalid_time() {
    bool ok = scheduler->setDailyAlarm(0, 86400,   // exactly 86400 is invalid
        Config::TimerAction::ENABLE_CONTROL, 0);
    TEST_ASSERT_FALSE(ok);
}

void test_set_daily_alarm_invalid_index() {
    bool ok = scheduler->setDailyAlarm(Config::TimersConfig::MAX_TIMERS, 3600,
        Config::TimerAction::ENABLE_CONTROL, 0);
    TEST_ASSERT_FALSE(ok);
}

// ---- getRemainingSeconds (ALARM_DAILY) ----

void test_get_remaining_seconds_daily_alarm_before_trigger() {
    // Alarm at 08:00 (28800 s), current time 06:00 (21600 s) → 7200 s remaining
    scheduler->setDailyAlarm(0, 28800u,
        Config::TimerAction::ENABLE_CONTROL, 0);
    // Use a UTC midnight + 6h epoch so getSecondsSinceMidnight returns 21600
    uint32_t epoch = 1704067200u + 21600u; // midnight + 6 h
    uint32_t remaining = scheduler->getRemainingSeconds(0, epoch);
    TEST_ASSERT_EQUAL(28800u - 21600u, remaining);
}

void test_get_remaining_seconds_daily_alarm_after_trigger() {
    // Alarm at 06:00 (21600 s), current time 08:00 (28800 s) → wraps to tomorrow
    scheduler->setDailyAlarm(0, 21600u,
        Config::TimerAction::ENABLE_CONTROL, 0);
    uint32_t epoch = 1704067200u + 28800u; // midnight + 8 h
    uint32_t remaining = scheduler->getRemainingSeconds(0, epoch);
    // (86400 - 28800) + 21600 = 57600 + 21600 = 79200
    TEST_ASSERT_EQUAL((86400u - 28800u) + 21600u, remaining);
}

// ---- cancelTimer ----

void test_cancel_timer_disables() {
    scheduler->setCountdown(0, 3600,
        Config::TimerAction::ENABLE_CONTROL, 0, 1000u);
    bool ok = scheduler->cancelTimer(0);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(scheduler->getTimersConfig().timers[0].enabled);
}

void test_cancel_timer_invalid_index() {
    bool ok = scheduler->cancelTimer(Config::TimersConfig::MAX_TIMERS);
    TEST_ASSERT_FALSE(ok);
}

// ---- checkTimers (NTP gate) ----

void test_check_timers_skipped_when_ntp_unavailable() {
    scheduler->setNtpAvailable(false);
    scheduler->setCountdown(0, 100,
        Config::TimerAction::ENABLE_CONTROL, 0, 0u);
    // currentEpoch far past target – but NTP is off, so nothing fires
    scheduler->checkTimers(5000u);
    TEST_ASSERT_TRUE(scheduler->getTimersConfig().timers[0].enabled); // still enabled
}

void test_check_timers_countdown_triggers() {
    scheduler->setNtpAvailable(true);
    scheduler->setCountdown(0, 100,
        Config::TimerAction::ENABLE_CONTROL, 0, 0u);
    // target_time = 0 + 100; currentEpoch = 200 > target → fires
    scheduler->checkTimers(200u);
    // After firing, countdown timer is disabled
    TEST_ASSERT_FALSE(scheduler->getTimersConfig().timers[0].enabled);
}

void test_check_timers_countdown_does_not_trigger_before_target() {
    scheduler->setNtpAvailable(true);
    scheduler->setCountdown(0, 1000,
        Config::TimerAction::ENABLE_CONTROL, 0, 0u);
    scheduler->checkTimers(500u); // before target_time = 1000
    TEST_ASSERT_TRUE(scheduler->getTimersConfig().timers[0].enabled);
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_seconds_since_midnight_zero_epoch);
    RUN_TEST(test_seconds_since_midnight_at_midnight_utc);
    RUN_TEST(test_seconds_since_midnight_at_noon_utc);
    RUN_TEST(test_seconds_since_midnight_near_end_of_day);
    RUN_TEST(test_seconds_since_midnight_timezone_plus_one);
    RUN_TEST(test_seconds_since_midnight_timezone_minus_one);
    RUN_TEST(test_set_timezone_offset_valid_positive);
    RUN_TEST(test_set_timezone_offset_valid_negative);
    RUN_TEST(test_set_timezone_offset_zero);
    RUN_TEST(test_set_timezone_offset_invalid_too_low);
    RUN_TEST(test_set_timezone_offset_invalid_too_high);
    RUN_TEST(test_set_countdown_sets_timer);
    RUN_TEST(test_set_countdown_invalid_index);
    RUN_TEST(test_set_countdown_all_slots);
    RUN_TEST(test_get_remaining_seconds_countdown);
    RUN_TEST(test_get_remaining_seconds_already_expired);
    RUN_TEST(test_get_remaining_seconds_invalid_index);
    RUN_TEST(test_get_remaining_seconds_inactive_timer);
    RUN_TEST(test_set_daily_alarm_valid);
    RUN_TEST(test_set_daily_alarm_midnight);
    RUN_TEST(test_set_daily_alarm_invalid_time);
    RUN_TEST(test_set_daily_alarm_invalid_index);
    RUN_TEST(test_get_remaining_seconds_daily_alarm_before_trigger);
    RUN_TEST(test_get_remaining_seconds_daily_alarm_after_trigger);
    RUN_TEST(test_cancel_timer_disables);
    RUN_TEST(test_cancel_timer_invalid_index);
    RUN_TEST(test_check_timers_skipped_when_ntp_unavailable);
    RUN_TEST(test_check_timers_countdown_triggers);
    RUN_TEST(test_check_timers_countdown_does_not_trigger_before_target);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
