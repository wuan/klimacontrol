#include <unity.h>

#include <cmath>
#include <cstring>
#include <limits>

#include "display/RefreshPolicy.h"

using Display::RefreshKind;
using Display::ControlState;
using Display::RefreshPolicy;

// Default interval used by most tests, matching Config::DEFAULT_DISPLAY_INTERVAL.
static constexpr uint16_t INTERVAL_SEC = 60;
static constexpr uint32_t INTERVAL_MS = INTERVAL_SEC * 1000u;

void setUp() {}
void tearDown() {}

// Drives the policy past its first-paint case so a test can start from a
// settled state. Returns the timestamp of that initial full refresh.
static uint32_t primeAt(RefreshPolicy &policy, float temp, float hum, uint32_t nowMs) {
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Full),
                      static_cast<int>(policy.evaluate(temp, hum, true, nowMs)));
    return nowMs;
}

// --- first paint ---

void test_first_evaluation_is_full() {
    RefreshPolicy policy(INTERVAL_SEC);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Full),
                      static_cast<int>(policy.evaluate(21.4f, 47.0f, true, 1000)));
}

void test_first_evaluation_is_full_even_when_invalid() {
    // A booting device with no sensor still needs its placeholder painted, and
    // the panel may be holding an arbitrary retained image.
    RefreshPolicy policy(INTERVAL_SEC);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Full),
                      static_cast<int>(policy.evaluate(NAN, NAN, false, 0)));
}

void test_reset_restores_first_paint_behaviour() {
    RefreshPolicy policy(INTERVAL_SEC);
    primeAt(policy, 21.4f, 47.0f, 1000);
    policy.reset();
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Full),
                      static_cast<int>(policy.evaluate(21.4f, 47.0f, true, 2000)));
}

// --- hysteresis ---

void test_unchanged_values_return_none() {
    RefreshPolicy policy(INTERVAL_SEC);
    uint32_t t = primeAt(policy, 21.4f, 47.0f, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::None),
                      static_cast<int>(policy.evaluate(21.4f, 47.0f, true, t + 10 * INTERVAL_MS)));
}

void test_sub_hysteresis_temperature_noise_returns_none() {
    RefreshPolicy policy(INTERVAL_SEC);
    uint32_t t = primeAt(policy, 21.44f, 47.0f, 1000);
    // 0.02 C of dither, far past the interval floor so only hysteresis can block it.
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::None),
                      static_cast<int>(policy.evaluate(21.46f, 47.0f, true, t + 10 * INTERVAL_MS)));
}

void test_sub_hysteresis_humidity_noise_returns_none() {
    RefreshPolicy policy(INTERVAL_SEC);
    uint32_t t = primeAt(policy, 21.4f, 47.0f, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::None),
                      static_cast<int>(policy.evaluate(21.4f, 47.5f, true, t + 10 * INTERVAL_MS)));
}

void test_temperature_change_at_threshold_refreshes() {
    RefreshPolicy policy(INTERVAL_SEC);
    uint32_t t = primeAt(policy, 21.0f, 47.0f, 1000);
    // Exactly at the 0.1 C threshold.
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(21.1f, 47.0f, true, t + INTERVAL_MS)));
}

void test_temperature_change_below_threshold_suppressed() {
    RefreshPolicy policy(INTERVAL_SEC);
    uint32_t t = primeAt(policy, 21.00f, 47.0f, 1000);
    // 0.05 C is under the 0.1 C threshold and invisible at one decimal place.
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::None),
                      static_cast<int>(policy.evaluate(21.05f, 47.0f, true, t + 10 * INTERVAL_MS)));
}

void test_humidity_change_at_threshold_refreshes() {
    RefreshPolicy policy(INTERVAL_SEC);
    uint32_t t = primeAt(policy, 21.0f, 47.0f, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(21.0f, 48.0f, true, t + INTERVAL_MS)));
}

void test_temperature_change_is_direction_agnostic() {
    RefreshPolicy policy(INTERVAL_SEC);
    uint32_t t = primeAt(policy, 21.0f, 47.0f, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(20.5f, 47.0f, true, t + INTERVAL_MS)));
}

// --- minimum interval floor ---

void test_change_inside_interval_is_suppressed_then_fires() {
    RefreshPolicy policy(INTERVAL_SEC);
    uint32_t t = primeAt(policy, 21.0f, 47.0f, 1000);

    // 20 s in: real change, but the floor blocks it.
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::None),
                      static_cast<int>(policy.evaluate(21.5f, 47.0f, true, t + 20000)));

    // The change must still be outstanding once the floor passes — this is the
    // regression guard for recording values on a suppressed evaluation.
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(21.5f, 47.0f, true, t + INTERVAL_MS)));
}

void test_interval_boundary_is_inclusive() {
    RefreshPolicy policy(INTERVAL_SEC);
    uint32_t t = primeAt(policy, 21.0f, 47.0f, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::None),
                      static_cast<int>(policy.evaluate(21.5f, 47.0f, true, t + INTERVAL_MS - 1)));
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(21.5f, 47.0f, true, t + INTERVAL_MS)));
}

void test_zero_interval_allows_immediate_refresh() {
    // Not reachable through validated config, but the policy must not divide or
    // underflow on a zero interval.
    RefreshPolicy policy(0);
    uint32_t t = primeAt(policy, 21.0f, 47.0f, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(21.5f, 47.0f, true, t)));
}

// --- ghosting / periodic full refresh ---

void test_twelfth_partial_is_promoted_to_full() {
    RefreshPolicy policy(INTERVAL_SEC);
    uint32_t t = primeAt(policy, 20.0f, 47.0f, 1000);

    float temp = 20.0f;
    for (int i = 0; i < Display::FULL_REFRESH_EVERY_N_PARTIALS; i++) {
        temp += 0.5f;
        t += INTERVAL_MS;
        TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                          static_cast<int>(policy.evaluate(temp, 47.0f, true, t)));
    }
    TEST_ASSERT_EQUAL(Display::FULL_REFRESH_EVERY_N_PARTIALS, policy.getPartialsSinceFull());

    // The next due refresh clears the accumulated ghosting.
    temp += 0.5f;
    t += INTERVAL_MS;
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Full),
                      static_cast<int>(policy.evaluate(temp, 47.0f, true, t)));
    TEST_ASSERT_EQUAL(0, policy.getPartialsSinceFull());

    // ...and the cycle starts over.
    temp += 0.5f;
    t += INTERVAL_MS;
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(temp, 47.0f, true, t)));
}

void test_suppressed_evaluations_do_not_advance_the_partial_counter() {
    RefreshPolicy policy(INTERVAL_SEC);
    uint32_t t = primeAt(policy, 20.0f, 47.0f, 1000);
    for (int i = 0; i < 50; i++) {
        policy.evaluate(25.0f, 47.0f, true, t + 100 * i); // all inside the floor
    }
    TEST_ASSERT_EQUAL(0, policy.getPartialsSinceFull());
}

// --- validity transitions ---

void test_valid_to_invalid_forces_refresh() {
    RefreshPolicy policy(INTERVAL_SEC);
    uint32_t t = primeAt(policy, 21.4f, 47.0f, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(21.4f, 47.0f, false, t + INTERVAL_MS)));
}

void test_invalid_to_valid_forces_refresh() {
    RefreshPolicy policy(INTERVAL_SEC);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Full),
                      static_cast<int>(policy.evaluate(NAN, NAN, false, 1000)));
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(21.4f, 47.0f, true, 1000 + INTERVAL_MS)));
}

void test_staying_invalid_returns_none() {
    RefreshPolicy policy(INTERVAL_SEC);
    policy.evaluate(NAN, NAN, false, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::None),
                      static_cast<int>(policy.evaluate(NAN, NAN, false, 1000 + 10 * INTERVAL_MS)));
}

void test_nan_temperature_is_treated_as_invalid() {
    RefreshPolicy policy(INTERVAL_SEC);
    uint32_t t = primeAt(policy, 21.4f, 47.0f, 1000);
    // valid==true but the value is NAN: must be treated as unavailable, which
    // is a validity transition and therefore forces a refresh.
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(NAN, 47.0f, true, t + INTERVAL_MS)));
}

void test_nan_humidity_is_treated_as_invalid() {
    RefreshPolicy policy(INTERVAL_SEC);
    uint32_t t = primeAt(policy, 21.4f, 47.0f, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(21.4f, NAN, true, t + INTERVAL_MS)));
}

// --- millis() rollover ---

void test_rollover_does_not_suppress_refreshes() {
    RefreshPolicy policy(INTERVAL_SEC);
    // Last refresh 30 s before the wrap point.
    const uint32_t nearMax = std::numeric_limits<uint32_t>::max() - 30000u;
    primeAt(policy, 21.0f, 47.0f, nearMax);

    // 30 s after the wrap: only 60 s of real time has elapsed, so the floor is
    // exactly met. Unsigned subtraction makes this work; a signed comparison
    // would compute a huge negative elapsed time.
    const uint32_t afterWrap = 30000u - 1u; // nearMax + 60000 wraps to this
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(21.5f, 47.0f, true, afterWrap)));
}

void test_rollover_still_enforces_the_floor() {
    RefreshPolicy policy(INTERVAL_SEC);
    const uint32_t nearMax = std::numeric_limits<uint32_t>::max() - 30000u;
    primeAt(policy, 21.0f, 47.0f, nearMax);

    // 10 s past the wrap = 40 s elapsed, still inside the 60 s floor.
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::None),
                      static_cast<int>(policy.evaluate(21.5f, 47.0f, true, 10000u)));
}

// --- formatting ---

void test_format_temperature_one_decimal() {
    char buf[16];
    Display::formatTemperature(buf, sizeof(buf), 21.44f, true);
    TEST_ASSERT_EQUAL_STRING("21.4", buf);
}

void test_format_temperature_negative() {
    char buf[16];
    Display::formatTemperature(buf, sizeof(buf), -3.25f, true);
    TEST_ASSERT_EQUAL_STRING("-3.2", buf);
}

void test_format_temperature_placeholder_when_invalid() {
    char buf[16];
    Display::formatTemperature(buf, sizeof(buf), 21.4f, false);
    TEST_ASSERT_EQUAL_STRING("--.-", buf);
}

void test_format_temperature_placeholder_when_nan() {
    char buf[16];
    Display::formatTemperature(buf, sizeof(buf), NAN, true);
    TEST_ASSERT_EQUAL_STRING("--.-", buf);
}

void test_format_humidity_whole_number() {
    char buf[16];
    Display::formatHumidity(buf, sizeof(buf), 47.4f, true);
    TEST_ASSERT_EQUAL_STRING("47", buf);
}

void test_format_humidity_placeholder_when_invalid() {
    char buf[16];
    Display::formatHumidity(buf, sizeof(buf), 47.0f, false);
    TEST_ASSERT_EQUAL_STRING("--", buf);
}

void test_format_humidity_placeholder_when_nan() {
    char buf[16];
    Display::formatHumidity(buf, sizeof(buf), NAN, true);
    TEST_ASSERT_EQUAL_STRING("--", buf);
}

void test_format_handles_null_and_zero_length() {
    char buf[16] = "untouched";
    TEST_ASSERT_EQUAL(0, Display::formatTemperature(nullptr, 16, 21.0f, true));
    TEST_ASSERT_EQUAL(0, Display::formatTemperature(buf, 0, 21.0f, true));
    TEST_ASSERT_EQUAL_STRING("untouched", buf);
    TEST_ASSERT_EQUAL(0, Display::formatHumidity(nullptr, 16, 47.0f, true));
    TEST_ASSERT_EQUAL(0, Display::formatHumidity(buf, 0, 47.0f, true));
    TEST_ASSERT_EQUAL_STRING("untouched", buf);
}

// --- wall-clock trigger ---

void test_minute_rollover_triggers_refresh() {
    RefreshPolicy policy(INTERVAL_SEC);
    // Prime with a known clock minute.
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Full),
                      static_cast<int>(policy.evaluate(21.0f, 47.0f, true, 1000, 1000)));
    // Values unchanged, but the minute has rolled over: the footer must repaint.
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(21.0f, 47.0f, true, 1000 + INTERVAL_MS, 1001)));
}

void test_same_minute_does_not_trigger_refresh() {
    RefreshPolicy policy(INTERVAL_SEC);
    policy.evaluate(21.0f, 47.0f, true, 1000, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::None),
                      static_cast<int>(policy.evaluate(21.0f, 47.0f, true, 1000 + 10 * INTERVAL_MS, 1000)));
}

void test_clock_refresh_still_respects_the_interval_floor() {
    // The minute trigger must not be able to outrun the panel-protection
    // budget: with a 300 s interval the clock updates every 5 minutes, not
    // every one.
    RefreshPolicy policy(300);
    policy.evaluate(21.0f, 47.0f, true, 1000, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::None),
                      static_cast<int>(policy.evaluate(21.0f, 47.0f, true, 1000 + 60000, 1001)));
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(21.0f, 47.0f, true, 1000 + 300000, 1005)));
}

void test_ntp_sync_populates_the_clock() {
    // 0 -> non-zero is the unsynced-to-synced transition; the footer gains a
    // time and must repaint to show it.
    RefreshPolicy policy(INTERVAL_SEC);
    policy.evaluate(21.0f, 47.0f, true, 1000, 0);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(21.0f, 47.0f, true, 1000 + INTERVAL_MS, 29000000)));
}

void test_clock_trigger_advances_the_ghosting_counter() {
    // Clock-driven refreshes are real refreshes, so they must count toward the
    // periodic full refresh that clears ghosting.
    RefreshPolicy policy(INTERVAL_SEC);
    policy.evaluate(21.0f, 47.0f, true, 1000, 1000);
    uint32_t t = 1000;
    for (int i = 1; i <= Display::FULL_REFRESH_EVERY_N_PARTIALS; i++) {
        t += INTERVAL_MS;
        TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                          static_cast<int>(policy.evaluate(21.0f, 47.0f, true, t, 1000 + i)));
    }
    t += INTERVAL_MS;
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Full),
                      static_cast<int>(policy.evaluate(21.0f, 47.0f, true, t, 1013)));
}

// --- setpoint / control symbol ---

void test_setpoint_change_triggers_refresh() {
    // The setpoint is footer content the user changes from the web UI. Without
    // it as an input, a stable sensor with an unsynced clock would keep showing
    // the old target indefinitely.
    RefreshPolicy policy(INTERVAL_SEC);
    policy.evaluate(21.0f, 47.0f, true, 1000, 0, 21.5f, ControlState::ACTIVE_OFF);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(21.0f, 47.0f, true, 1000 + INTERVAL_MS, 0,
                                                       22.5f, ControlState::ACTIVE_OFF)));
}

void test_unchanged_setpoint_does_not_trigger_refresh() {
    RefreshPolicy policy(INTERVAL_SEC);
    policy.evaluate(21.0f, 47.0f, true, 1000, 0, 21.5f, ControlState::ACTIVE_OFF);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::None),
                      static_cast<int>(policy.evaluate(21.0f, 47.0f, true, 1000 + INTERVAL_MS, 0,
                                                       21.5f, ControlState::ACTIVE_OFF)));
}

void test_setpoint_change_below_rendered_precision_is_suppressed() {
    // Rendered with one decimal, so a 0.01 K move is the same picture.
    RefreshPolicy policy(INTERVAL_SEC);
    policy.evaluate(21.0f, 47.0f, true, 1000, 0, 21.5f, ControlState::ACTIVE_OFF);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::None),
                      static_cast<int>(policy.evaluate(21.0f, 47.0f, true, 1000 + INTERVAL_MS, 0,
                                                       21.51f, ControlState::ACTIVE_OFF)));
}

void test_control_state_change_triggers_refresh() {
    RefreshPolicy policy(INTERVAL_SEC);
    policy.evaluate(21.0f, 47.0f, true, 1000, 0, 21.5f, ControlState::ACTIVE_OFF);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(21.0f, 47.0f, true, 1000 + INTERVAL_MS, 0,
                                                       21.5f, ControlState::ACTIVE_ON)));
}

void test_setpoint_nan_transition_triggers_refresh() {
    // NAN renders as a placeholder; gaining or losing a real value is visible.
    RefreshPolicy policy(INTERVAL_SEC);
    policy.evaluate(21.0f, 47.0f, true, 1000, 0, NAN, ControlState::INACTIVE);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(21.0f, 47.0f, true, 1000 + INTERVAL_MS, 0,
                                                       21.5f, ControlState::INACTIVE)));
}

void test_setpoint_nan_to_nan_does_not_trigger_refresh() {
    RefreshPolicy policy(INTERVAL_SEC);
    policy.evaluate(21.0f, 47.0f, true, 1000, 0, NAN, ControlState::INACTIVE);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::None),
                      static_cast<int>(policy.evaluate(21.0f, 47.0f, true, 1000 + INTERVAL_MS, 0,
                                                       NAN, ControlState::INACTIVE)));
}

void test_setpoint_change_still_respects_the_interval_floor() {
    RefreshPolicy policy(INTERVAL_SEC);
    policy.evaluate(21.0f, 47.0f, true, 1000, 0, 21.5f, ControlState::ACTIVE_OFF);
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::None),
                      static_cast<int>(policy.evaluate(21.0f, 47.0f, true, 1000 + INTERVAL_MS - 1,
                                                       0, 22.5f, ControlState::ACTIVE_OFF)));
    // Still outstanding: it fires once the floor passes.
    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::Partial),
                      static_cast<int>(policy.evaluate(21.0f, 47.0f, true, 1000 + INTERVAL_MS, 0,
                                                       22.5f, ControlState::ACTIVE_OFF)));
}

// --- demand bar bucketing ---
//
// The bar exists instead of a percentage because the panel repaints on any
// visible change: a live number would hold refreshes at the interval floor
// forever. These tests pin down the two properties that makes that true —
// coarse buckets, and hysteresis at the boundaries.

void test_demand_bucket_zero_and_full() {
    TEST_ASSERT_EQUAL_UINT8(0, Display::nextDemandBucket(0.0f, 0));
    TEST_ASSERT_EQUAL_UINT8(0, Display::nextDemandBucket(-0.5f, 3));
    TEST_ASSERT_EQUAL_UINT8(Display::DEMAND_BUCKETS, Display::nextDemandBucket(1.0f, 0));
    TEST_ASSERT_EQUAL_UINT8(Display::DEMAND_BUCKETS, Display::nextDemandBucket(2.0f, 0));
}

void test_demand_bucket_nan_reads_as_zero() {
    TEST_ASSERT_EQUAL_UINT8(0, Display::nextDemandBucket(NAN, 4));
}

void test_demand_bucket_rising_from_zero() {
    // Five buckets, so boundaries at 0.2/0.4/0.6/0.8.
    TEST_ASSERT_EQUAL_UINT8(1, Display::nextDemandBucket(0.10f, 0));
    TEST_ASSERT_EQUAL_UINT8(2, Display::nextDemandBucket(0.30f, 0));
    TEST_ASSERT_EQUAL_UINT8(3, Display::nextDemandBucket(0.50f, 0));
    TEST_ASSERT_EQUAL_UINT8(4, Display::nextDemandBucket(0.70f, 0));
    TEST_ASSERT_EQUAL_UINT8(5, Display::nextDemandBucket(0.90f, 0));
}

void test_demand_bucket_holds_on_a_boundary() {
    // Exactly on the 0.4 edge: whichever side it was showing, it stays there.
    // This is the case that would otherwise repaint the panel every tick.
    TEST_ASSERT_EQUAL_UINT8(2, Display::nextDemandBucket(0.40f, 2));
    TEST_ASSERT_EQUAL_UINT8(3, Display::nextDemandBucket(0.40f, 3));
}

void test_demand_bucket_dithering_does_not_flip() {
    // A demand jittering by a percent either side of a boundary must not move
    // the bar at all.
    uint8_t bucket = 2;
    for (int i = 0; i < 20; ++i) {
        const float f = (i % 2 == 0) ? 0.395f : 0.405f;
        bucket = Display::nextDemandBucket(f, bucket);
        TEST_ASSERT_EQUAL_UINT8(2, bucket);
    }
}

void test_demand_bucket_moves_once_hysteresis_is_cleared() {
    uint8_t bucket = Display::nextDemandBucket(0.40f, 2);
    TEST_ASSERT_EQUAL_UINT8(2, bucket);
    bucket = Display::nextDemandBucket(0.45f, bucket); // clears 0.4 + 0.03
    TEST_ASSERT_EQUAL_UINT8(3, bucket);
    bucket = Display::nextDemandBucket(0.30f, bucket); // clears 0.4 - 0.03 downward
    TEST_ASSERT_EQUAL_UINT8(2, bucket);
}

// --- demand bar drives refreshes ---

void test_demand_bucket_change_triggers_refresh() {
    RefreshPolicy policy(INTERVAL_SEC);
    policy.evaluate(21.0f, 50.0f, true, 1000, 0, 22.0f, ControlState::ACTIVE_ON, 1);

    // Same readings, same setpoint, same state — only the bar moved.
    const RefreshKind kind = policy.evaluate(21.0f, 50.0f, true, 1000 + INTERVAL_MS, 0, 22.0f,
                                             ControlState::ACTIVE_ON, 3);

    TEST_ASSERT_NOT_EQUAL(static_cast<int>(RefreshKind::None), static_cast<int>(kind));
}

void test_unchanged_demand_bucket_does_not_trigger_refresh() {
    RefreshPolicy policy(INTERVAL_SEC);
    policy.evaluate(21.0f, 50.0f, true, 1000, 0, 22.0f, ControlState::ACTIVE_ON, 3);

    // Nothing visible changed, so the panel must stay put even though the
    // interval floor has passed. This is the property that keeps the bar from
    // costing refreshes.
    const RefreshKind kind = policy.evaluate(21.0f, 50.0f, true, 1000 + INTERVAL_MS, 0, 22.0f,
                                             ControlState::ACTIVE_ON, 3);

    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::None), static_cast<int>(kind));
}

void test_demand_bucket_change_respects_the_interval_floor() {
    RefreshPolicy policy(INTERVAL_SEC);
    policy.evaluate(21.0f, 50.0f, true, 1000, 0, 22.0f, ControlState::ACTIVE_ON, 1);

    // Too soon: the change is real but the floor still applies, exactly as it
    // does for a setpoint change.
    const RefreshKind kind =
        policy.evaluate(21.0f, 50.0f, true, 1500, 0, 22.0f, ControlState::ACTIVE_ON, 4);

    TEST_ASSERT_EQUAL(static_cast<int>(RefreshKind::None), static_cast<int>(kind));
}

int runUnityTests() {
    UNITY_BEGIN();
    // First paint
    RUN_TEST(test_first_evaluation_is_full);
    RUN_TEST(test_first_evaluation_is_full_even_when_invalid);
    RUN_TEST(test_reset_restores_first_paint_behaviour);
    // Hysteresis
    RUN_TEST(test_unchanged_values_return_none);
    RUN_TEST(test_sub_hysteresis_temperature_noise_returns_none);
    RUN_TEST(test_sub_hysteresis_humidity_noise_returns_none);
    RUN_TEST(test_temperature_change_at_threshold_refreshes);
    RUN_TEST(test_temperature_change_below_threshold_suppressed);
    RUN_TEST(test_humidity_change_at_threshold_refreshes);
    RUN_TEST(test_temperature_change_is_direction_agnostic);
    // Minimum interval
    RUN_TEST(test_change_inside_interval_is_suppressed_then_fires);
    RUN_TEST(test_interval_boundary_is_inclusive);
    RUN_TEST(test_zero_interval_allows_immediate_refresh);
    // Ghosting
    RUN_TEST(test_twelfth_partial_is_promoted_to_full);
    RUN_TEST(test_suppressed_evaluations_do_not_advance_the_partial_counter);
    // Validity transitions
    RUN_TEST(test_valid_to_invalid_forces_refresh);
    RUN_TEST(test_invalid_to_valid_forces_refresh);
    RUN_TEST(test_staying_invalid_returns_none);
    RUN_TEST(test_nan_temperature_is_treated_as_invalid);
    RUN_TEST(test_nan_humidity_is_treated_as_invalid);
    // Rollover
    RUN_TEST(test_rollover_does_not_suppress_refreshes);
    RUN_TEST(test_rollover_still_enforces_the_floor);
    // Wall-clock trigger
    RUN_TEST(test_minute_rollover_triggers_refresh);
    RUN_TEST(test_same_minute_does_not_trigger_refresh);
    RUN_TEST(test_clock_refresh_still_respects_the_interval_floor);
    RUN_TEST(test_ntp_sync_populates_the_clock);
    RUN_TEST(test_clock_trigger_advances_the_ghosting_counter);
    // Setpoint and control symbol
    RUN_TEST(test_setpoint_change_triggers_refresh);
    RUN_TEST(test_unchanged_setpoint_does_not_trigger_refresh);
    RUN_TEST(test_setpoint_change_below_rendered_precision_is_suppressed);
    RUN_TEST(test_control_state_change_triggers_refresh);
    RUN_TEST(test_setpoint_nan_transition_triggers_refresh);
    RUN_TEST(test_setpoint_nan_to_nan_does_not_trigger_refresh);
    RUN_TEST(test_setpoint_change_still_respects_the_interval_floor);
    // Formatting
    RUN_TEST(test_format_temperature_one_decimal);
    RUN_TEST(test_format_temperature_negative);
    RUN_TEST(test_format_temperature_placeholder_when_invalid);
    RUN_TEST(test_format_temperature_placeholder_when_nan);
    RUN_TEST(test_format_humidity_whole_number);
    RUN_TEST(test_format_humidity_placeholder_when_invalid);
    RUN_TEST(test_format_humidity_placeholder_when_nan);
    RUN_TEST(test_format_handles_null_and_zero_length);
    RUN_TEST(test_demand_bucket_zero_and_full);
    RUN_TEST(test_demand_bucket_nan_reads_as_zero);
    RUN_TEST(test_demand_bucket_rising_from_zero);
    RUN_TEST(test_demand_bucket_holds_on_a_boundary);
    RUN_TEST(test_demand_bucket_dithering_does_not_flip);
    RUN_TEST(test_demand_bucket_moves_once_hysteresis_is_cleared);
    RUN_TEST(test_demand_bucket_change_triggers_refresh);
    RUN_TEST(test_unchanged_demand_bucket_does_not_trigger_refresh);
    RUN_TEST(test_demand_bucket_change_respects_the_interval_floor);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
