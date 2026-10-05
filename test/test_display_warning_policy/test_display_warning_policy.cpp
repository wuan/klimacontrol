#include <unity.h>

#include <cmath>
#include <cstdint>
#include <limits>

#include "display/WarningPolicy.h"

using Display::WarningConditions;
using Display::WarningPolicy;
using Display::WarningToken;

static constexpr uint32_t DWELL_MS = Display::WARNING_CLEAR_DWELL_S * 1000u;

void setUp() {}
void tearDown() {}

// Fresh policy with both thresholds enabled: frost at 5 C, humidity at 70 %.
static WarningPolicy makePolicy() {
    return WarningPolicy(5.0f, 70);
}

static WarningConditions idle() {
    return WarningConditions{}; // all false, readings NAN
}

static WarningConditions withHumidity(float pct) {
    WarningConditions c;
    c.humidity = pct;
    return c;
}

static WarningConditions withTemperature(float tempC) {
    WarningConditions c;
    c.temperature = tempC;
    return c;
}

static WarningConditions withOverheat() {
    WarningConditions c;
    c.overheat = true;
    return c;
}

static WarningConditions withSensorInvalid() {
    WarningConditions c;
    c.sensorInvalid = true;
    return c;
}

static WarningConditions withActuatorUncertain() {
    WarningConditions c;
    c.actuatorUncertain = true;
    return c;
}

// --- onset: immediate, no dwell ---

void test_quiescent_conditions_show_nothing() {
    WarningPolicy policy = makePolicy();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::NONE), static_cast<int>(policy.evaluate(idle(), 1000)));
}

void test_humidity_onset_is_immediate() {
    WarningPolicy policy = makePolicy();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(70.0f), 1000)));
}

void test_frost_onset_is_immediate() {
    WarningPolicy policy = makePolicy();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::FROST),
                      static_cast<int>(policy.evaluate(withTemperature(5.0f), 1000)));
}

void test_overheat_onset_is_immediate() {
    WarningPolicy policy = makePolicy();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::OVERHEAT),
                      static_cast<int>(policy.evaluate(withOverheat(), 1000)));
}

void test_sensor_onset_is_immediate() {
    WarningPolicy policy = makePolicy();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::SENSOR),
                      static_cast<int>(policy.evaluate(withSensorInvalid(), 1000)));
}

void test_actuator_onset_is_immediate() {
    WarningPolicy policy = makePolicy();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::ACTUATOR),
                      static_cast<int>(policy.evaluate(withActuatorUncertain(), 1000)));
}

// --- humidity threshold hysteresis ---

void test_humidity_below_threshold_does_not_fire() {
    WarningPolicy policy = makePolicy();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::NONE),
                      static_cast<int>(policy.evaluate(withHumidity(69.9f), 1000)));
}

void test_humidity_at_threshold_fires() {
    WarningPolicy policy = makePolicy();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(70.0f), 1000)));
}

void test_humidity_within_release_margin_holds() {
    // Fires at 70, hovers down to 67.0 (= threshold - margin, inclusive of
    // hold): the warning must not release.
    WarningPolicy policy = makePolicy();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(70.0f), 1000)));
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(67.0f), 2000)));
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(70.0f), 3000)));
}

void test_humidity_decisive_crossing_starts_dwell_then_clears() {
    WarningPolicy policy = makePolicy();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(70.0f), 1000)));

    // Below threshold - margin: the condition is inactive, but the warning
    // stays displayed while the dwell elapses.
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(60.0f), 2000)));
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(60.0f), 2000 + DWELL_MS - 1)));
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::NONE),
                      static_cast<int>(policy.evaluate(withHumidity(60.0f), 2000 + DWELL_MS)));
}

void test_humidity_boundary_hover_does_not_toggle() {
    // Oscillation either side of the fire threshold, all within the release
    // margin: the warning, once on, must never drop (no refresh churn).
    WarningPolicy policy = makePolicy();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(70.0f), 1000)));
    uint32_t t = 2000;
    for (int i = 0; i < 20; ++i) {
        const float pct = (i % 2 == 0) ? 68.5f : 71.0f;
        TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(WarningToken::HUMID),
                                  static_cast<int>(policy.evaluate(withHumidity(pct), t)),
                                  "hover inside the margin must hold the warning");
        t += 1000;
    }
}

// --- frost threshold hysteresis ---

void test_frost_above_threshold_does_not_fire() {
    WarningPolicy policy = makePolicy();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::NONE),
                      static_cast<int>(policy.evaluate(withTemperature(5.1f), 1000)));
}

void test_frost_at_threshold_fires() {
    WarningPolicy policy = makePolicy();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::FROST),
                      static_cast<int>(policy.evaluate(withTemperature(5.0f), 1000)));
}

void test_frost_within_release_margin_holds_then_clears() {
    WarningPolicy policy = makePolicy();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::FROST),
                      static_cast<int>(policy.evaluate(withTemperature(5.0f), 1000)));

    // Release needs temp > threshold + margin = 7.0; 6.9 must hold.
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::FROST),
                      static_cast<int>(policy.evaluate(withTemperature(6.9f), 2000)));
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::FROST),
                      static_cast<int>(policy.evaluate(withTemperature(6.9f), 2000 + DWELL_MS - 1)));

    // Decisive crossing above the margin: dwell completes, warning clears.
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::FROST),
                      static_cast<int>(policy.evaluate(withTemperature(7.1f), 2000 + DWELL_MS)));
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::NONE),
                      static_cast<int>(policy.evaluate(withTemperature(7.1f), 2000 + 2 * DWELL_MS)));
}

// --- NAN / unavailable inputs ---

void test_nan_readings_never_fire_threshold_warnings() {
    WarningPolicy policy = makePolicy();
    WarningConditions c = idle(); // temperature/humidity NAN
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::NONE), static_cast<int>(policy.evaluate(c, 1000)));
}

void test_nan_reading_holds_a_latched_threshold_warning() {
    // A missing measurement is not evidence the condition cleared: the latch
    // holds rather than starting the clear dwell on phantom data.
    WarningPolicy policy = makePolicy();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::FROST),
                      static_cast<int>(policy.evaluate(withTemperature(4.0f), 1000)));

    WarningConditions nanTemp;
    nanTemp.temperature = NAN;
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::FROST),
                      static_cast<int>(policy.evaluate(nanTemp, 1000 + DWELL_MS + 1)));
}

// --- priority selection ---

void test_overheat_outranks_humid() {
    WarningPolicy policy = makePolicy();
    WarningConditions c = withHumidity(80.0f);
    c.overheat = true;
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::OVERHEAT), static_cast<int>(policy.evaluate(c, 1000)));
}

void test_overheat_outranks_frost() {
    WarningPolicy policy = makePolicy();
    WarningConditions c = withTemperature(0.0f);
    c.overheat = true;
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::OVERHEAT), static_cast<int>(policy.evaluate(c, 1000)));
}

void test_frost_outranks_sensor() {
    WarningPolicy policy = makePolicy();
    // Latch frost first, then lose the sensor.
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::FROST),
                      static_cast<int>(policy.evaluate(withTemperature(4.0f), 1000)));
    WarningConditions c = withSensorInvalid();
    c.temperature = NAN;
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::FROST), static_cast<int>(policy.evaluate(c, 2000)));
}

void test_sensor_outranks_actuator() {
    WarningPolicy policy = makePolicy();
    WarningConditions c = withActuatorUncertain();
    c.sensorInvalid = true;
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::SENSOR), static_cast<int>(policy.evaluate(c, 1000)));
}

void test_actuator_outranks_humid() {
    WarningPolicy policy = makePolicy();
    WarningConditions c = withHumidity(80.0f);
    c.actuatorUncertain = true;
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::ACTUATOR), static_cast<int>(policy.evaluate(c, 1000)));
}

void test_priority_downgrade_shows_next_warning_immediately() {
    WarningPolicy policy = makePolicy();
    WarningConditions c = withHumidity(80.0f);
    c.overheat = true;
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::OVERHEAT), static_cast<int>(policy.evaluate(c, 1000)));

    // Overheat clears: the still-active humidity warning takes over at once,
    // without waiting out the clear dwell.
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(80.0f), 2000)));
}

// --- clear dwell: brief recovery does not clear ---

void test_brief_recovery_does_not_clear_warning() {
    WarningPolicy policy = makePolicy();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(70.0f), 1000)));

    // Inactive for less than the dwell...
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(60.0f), 1000 + DWELL_MS / 2)));
    // ...then active again: the warning continues without interruption and
    // the partial dwell is discarded.
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(70.0f), 1000 + DWELL_MS)));
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(70.0f), 1000 + DWELL_MS + DWELL_MS / 2)));
    // The second inactivity period must run a full dwell of its own.
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(60.0f), 1000 + 2 * DWELL_MS + 1)));
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::NONE),
                      static_cast<int>(policy.evaluate(withHumidity(60.0f), 1000 + 3 * DWELL_MS + 1)));
}

// --- wrap-safe elapsed-time comparison ---

void test_clear_dwell_survives_millis_rollover() {
    WarningPolicy policy = makePolicy();
    const uint32_t nearMax = std::numeric_limits<uint32_t>::max();

    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(70.0f), nearMax - 1000)));

    // Condition goes inactive just before the rollover; the dwell must be
    // measured across it (unsigned subtraction), not reset or truncated.
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(60.0f), nearMax)));
    // 20 s before wrap: dwell (30 s) not yet complete.
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::HUMID),
                      static_cast<int>(policy.evaluate(withHumidity(60.0f), nearMax + DWELL_MS - 20000)));
    // Past the wrap: elapsed = 30 s exactly, warning clears.
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::NONE),
                      static_cast<int>(policy.evaluate(withHumidity(60.0f), nearMax + DWELL_MS)));
}

void test_clear_dwell_survives_millis_rollover_on_latch_set() {
    // Same scenario but with inactiveSinceMs itself set right before the wrap.
    WarningPolicy policy = makePolicy();
    const uint32_t nearMax = std::numeric_limits<uint32_t>::max() - 5;

    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::FROST),
                      static_cast<int>(policy.evaluate(withTemperature(4.0f), nearMax - 1000)));
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::FROST),
                      static_cast<int>(policy.evaluate(withTemperature(10.0f), nearMax)));
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::NONE),
                      static_cast<int>(policy.evaluate(withTemperature(10.0f), nearMax + DWELL_MS)));
}

// --- disabled thresholds ---

void test_disabled_frost_threshold_never_fires() {
    WarningPolicy policy(NAN, 70);
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::NONE),
                      static_cast<int>(policy.evaluate(withTemperature(-40.0f), 1000)));
}

void test_disabled_humidity_threshold_never_fires() {
    WarningPolicy policy(5.0f, 0);
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::NONE),
                      static_cast<int>(policy.evaluate(withHumidity(100.0f), 1000)));
}

void test_always_on_conditions_fire_even_with_thresholds_disabled() {
    WarningPolicy policy(NAN, 0);
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::OVERHEAT),
                      static_cast<int>(policy.evaluate(withOverheat(), 1000)));
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::SENSOR),
                      static_cast<int>(policy.evaluate(withSensorInvalid(), 2000)));
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::ACTUATOR),
                      static_cast<int>(policy.evaluate(withActuatorUncertain(), 3000)));
}

// --- reset ---

void test_reset_clears_warning_and_latches() {
    WarningPolicy policy = makePolicy();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::FROST),
                      static_cast<int>(policy.evaluate(withTemperature(4.0f), 1000)));
    policy.reset();
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::NONE), static_cast<int>(policy.evaluate(idle(), 2000)));
    // The latch is gone too: a reading above the threshold must not restore
    // the warning without crossing it again.
    TEST_ASSERT_EQUAL(static_cast<int>(WarningToken::NONE),
                      static_cast<int>(policy.evaluate(withTemperature(6.0f), 3000)));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_quiescent_conditions_show_nothing);
    RUN_TEST(test_humidity_onset_is_immediate);
    RUN_TEST(test_frost_onset_is_immediate);
    RUN_TEST(test_overheat_onset_is_immediate);
    RUN_TEST(test_sensor_onset_is_immediate);
    RUN_TEST(test_actuator_onset_is_immediate);

    RUN_TEST(test_humidity_below_threshold_does_not_fire);
    RUN_TEST(test_humidity_at_threshold_fires);
    RUN_TEST(test_humidity_within_release_margin_holds);
    RUN_TEST(test_humidity_decisive_crossing_starts_dwell_then_clears);
    RUN_TEST(test_humidity_boundary_hover_does_not_toggle);

    RUN_TEST(test_frost_above_threshold_does_not_fire);
    RUN_TEST(test_frost_at_threshold_fires);
    RUN_TEST(test_frost_within_release_margin_holds_then_clears);

    RUN_TEST(test_nan_readings_never_fire_threshold_warnings);
    RUN_TEST(test_nan_reading_holds_a_latched_threshold_warning);

    RUN_TEST(test_overheat_outranks_humid);
    RUN_TEST(test_overheat_outranks_frost);
    RUN_TEST(test_frost_outranks_sensor);
    RUN_TEST(test_sensor_outranks_actuator);
    RUN_TEST(test_actuator_outranks_humid);
    RUN_TEST(test_priority_downgrade_shows_next_warning_immediately);

    RUN_TEST(test_brief_recovery_does_not_clear_warning);

    RUN_TEST(test_clear_dwell_survives_millis_rollover);
    RUN_TEST(test_clear_dwell_survives_millis_rollover_on_latch_set);

    RUN_TEST(test_disabled_frost_threshold_never_fires);
    RUN_TEST(test_disabled_humidity_threshold_never_fires);
    RUN_TEST(test_always_on_conditions_fire_even_with_thresholds_disabled);

    RUN_TEST(test_reset_clears_warning_and_latches);

    return UNITY_END();
}
