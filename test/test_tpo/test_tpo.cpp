#include "unity.h"

#include <limits>

#include "control/TimeProportionalOutput.h"

using Control::TimeProportionalOutput;

void setUp() {}
void tearDown() {}

// Unsigned subtraction so the loops below stay correct across the rollover.
static uint32_t since_helper(uint32_t now, uint32_t then) {
    return now - then;
}

namespace {
    // The shipped defaults: 20 minute cycle, 3 minute wax actuator stroke.
    // 4 x 3 = 12 <= 20, so the pair satisfies timingValid(), and the usable
    // duty range is 15%..85% rather than the 25%..75% a 5 minute stroke would
    // leave. See the dead-zone note in design.md.
    constexpr uint32_t CYCLE = 20u * 60u * 1000u;
    constexpr uint32_t TRAVEL = 3u * 60u * 1000u;

    // A cycle long enough that the dwell rails do not dominate, for tests about
    // proportioning rather than about snapping.
    constexpr uint32_t LONG_CYCLE = 60u * 60u * 1000u;
    constexpr uint32_t SHORT_TRAVEL = 60u * 1000u;

    // Drive from t0 for one cycle and report how long the valve was open.
    uint32_t measureOpenMs(TimeProportionalOutput &tpo, float demand, uint32_t t0,
                           uint32_t cycleMs, uint32_t stepMs) {
        uint32_t openFor = 0;
        for (uint32_t t = t0; since_helper(t, t0) < cycleMs; t += stepMs) {
            if (tpo.update(demand, t)) {
                openFor += stepMs;
            }
        }
        return openFor;
    }
}

// --- timing validation ---

void test_timing_valid_requires_four_strokes() {
    TEST_ASSERT_TRUE(TimeProportionalOutput::timingValid(CYCLE, TRAVEL));
    TEST_ASSERT_TRUE(TimeProportionalOutput::timingValid(TRAVEL * 4, TRAVEL));
    TEST_ASSERT_FALSE(TimeProportionalOutput::timingValid(TRAVEL * 3, TRAVEL));
    TEST_ASSERT_FALSE(TimeProportionalOutput::timingValid(0, TRAVEL));
}

// --- proportioning ---

void test_half_duty_is_half_the_cycle() {
    TimeProportionalOutput tpo(LONG_CYCLE, SHORT_TRAVEL);
    const uint32_t step = 10u * 1000u;
    const uint32_t openFor = measureOpenMs(tpo, 0.5f, 1000, LONG_CYCLE, step);
    TEST_ASSERT_UINT32_WITHIN(step * 2, LONG_CYCLE / 2, openFor);
}

void test_thirty_percent_duty() {
    // 0.30 of a 20 minute cycle is 6 minutes open and 14 closed; both clear the
    // 3 minute stroke, so this is delivered as asked.
    TimeProportionalOutput tpo(CYCLE, TRAVEL);
    const uint32_t step = 10u * 1000u;
    const uint32_t openFor = measureOpenMs(tpo, 0.30f, 1000, CYCLE, step);
    TEST_ASSERT_UINT32_WITHIN(step * 2, static_cast<uint32_t>(CYCLE * 0.30f), openFor);
}

void test_small_duty_delivers_nothing_in_its_first_cycle() {
    // 0.10 of a 20 minute cycle is 2 minutes, short of the 3 minute stroke, so
    // this cycle delivers nothing — but the demand is banked as credit rather
    // than discarded, and arrives as a full pulse a cycle or two later. See
    // test_small_duty_averages_correctly_over_many_cycles.
    TimeProportionalOutput tpo(CYCLE, TRAVEL);
    const uint32_t openFor = measureOpenMs(tpo, 0.10f, 1000, CYCLE, 10u * 1000u);
    TEST_ASSERT_EQUAL_UINT32(0, openFor);
    TEST_ASSERT_TRUE(tpo.creditMs() > 0.0f); // owed, not lost
}

// --- cycle skipping ---
//
// The property that matters: no individual cycle can represent a duty below one
// stroke, but the average across cycles must still track demand. Without this,
// every duty under 15% collapses to zero — which on an underfloor plant is most
// of the heating season.

namespace {
    struct SkipRun {
        uint32_t totalOpenMs = 0;
        uint32_t totalMs = 0;
        bool everPartialStroke = false;
        uint32_t pulses = 0;
    };

    // Step cycle-by-cycle, sampling the delivered open time of each.
    SkipRun runCycles(TimeProportionalOutput &tpo, float demand, uint32_t cycleMs,
                      uint32_t travelMs, int n) {
        SkipRun r;
        uint32_t t = 1000;
        for (int i = 0; i < n; ++i) {
            tpo.update(demand, t);
            const uint32_t openMs = static_cast<uint32_t>(tpo.latchedDuty() * cycleMs + 0.5f);
            if (openMs > 0) {
                ++r.pulses;
                // A pulse must always be a complete stroke or a whole cycle.
                if (openMs < travelMs) {
                    r.everPartialStroke = true;
                }
            }
            r.totalOpenMs += openMs;
            r.totalMs += cycleMs;
            t += cycleMs;
        }
        return r;
    }
}

void test_small_duty_averages_correctly_over_many_cycles() {
    TimeProportionalOutput tpo(CYCLE, TRAVEL);
    const SkipRun r = runCycles(tpo, 0.05f, CYCLE, TRAVEL, 40);

    const float delivered = static_cast<float>(r.totalOpenMs) / static_cast<float>(r.totalMs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.05f, delivered);
    TEST_ASSERT_FALSE(r.everPartialStroke);
    TEST_ASSERT_TRUE(r.pulses > 0); // it really did open sometimes
    TEST_ASSERT_TRUE(r.pulses < 40); // and really did skip cycles
}

void test_ten_percent_duty_averages_correctly() {
    TimeProportionalOutput tpo(CYCLE, TRAVEL);
    const SkipRun r = runCycles(tpo, 0.10f, CYCLE, TRAVEL, 40);
    const float delivered = static_cast<float>(r.totalOpenMs) / static_cast<float>(r.totalMs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.10f, delivered);
    TEST_ASSERT_FALSE(r.everPartialStroke);
}

void test_high_duty_averages_correctly_despite_rounding_up() {
    // The mirror case: 0.95 rounds up to a fully open cycle because the closed
    // interval would be too short. That overdelivers, pushing credit negative,
    // and the debt must be worked off rather than accumulating.
    TimeProportionalOutput tpo(CYCLE, TRAVEL);
    const SkipRun r = runCycles(tpo, 0.95f, CYCLE, TRAVEL, 40);
    const float delivered = static_cast<float>(r.totalOpenMs) / static_cast<float>(r.totalMs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.95f, delivered);
    TEST_ASSERT_FALSE(r.everPartialStroke);
}

void test_mid_range_duty_is_unaffected_by_skipping() {
    // Duties that fit comfortably must behave exactly as before: every cycle
    // delivers, none are skipped, and no credit accumulates.
    TimeProportionalOutput tpo(CYCLE, TRAVEL);
    const SkipRun r = runCycles(tpo, 0.50f, CYCLE, TRAVEL, 10);
    TEST_ASSERT_EQUAL_UINT32(10, r.pulses);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, tpo.creditMs());
}

void test_zero_demand_discards_credit() {
    // Credit banked before demand fell to zero must not be delivered later:
    // the reason for that heat has gone.
    TimeProportionalOutput tpo(CYCLE, TRAVEL);
    tpo.update(0.10f, 1000); // banks 2 minutes
    TEST_ASSERT_TRUE(tpo.creditMs() > 0.0f);
    tpo.update(0.0f, 1000 + CYCLE);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tpo.creditMs());
}

void test_reset_discards_credit() {
    TimeProportionalOutput tpo(CYCLE, TRAVEL);
    tpo.update(0.10f, 1000);
    TEST_ASSERT_TRUE(tpo.creditMs() > 0.0f);
    tpo.reset();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tpo.creditMs());
}

void test_thirty_percent_duty_with_a_faster_actuator() {
    // Same demand, an actuator that strokes in one minute: now 4.5 minutes of
    // open time is achievable and must actually be delivered.
    TimeProportionalOutput tpo(CYCLE, SHORT_TRAVEL);
    const uint32_t step = 5u * 1000u;
    const uint32_t openFor = measureOpenMs(tpo, 0.30f, 1000, CYCLE, step);
    TEST_ASSERT_UINT32_WITHIN(step * 2, static_cast<uint32_t>(CYCLE * 0.30f), openFor);
}

// --- dwell rails ---

void test_tiny_duty_snaps_closed() {
    TimeProportionalOutput tpo(CYCLE, TRAVEL);
    tpo.update(0.01f, 1000);
    TEST_ASSERT_FALSE(tpo.isOpen());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tpo.latchedDuty());
}

void test_near_full_duty_snaps_open() {
    TimeProportionalOutput tpo(CYCLE, TRAVEL);
    tpo.update(0.99f, 1000);
    TEST_ASSERT_TRUE(tpo.isOpen());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, tpo.latchedDuty());
}

void test_zero_and_full_never_switch() {
    // A cycle spent entirely on one rail must produce no transitions at all —
    // this is what stops a saturated controller from cycling the valve.
    TimeProportionalOutput off(CYCLE, TRAVEL);
    TimeProportionalOutput on(CYCLE, TRAVEL);
    bool offEver = false;
    bool onEver = false;
    for (uint32_t t = 1000; t < 1000 + CYCLE; t += 10u * 1000u) {
        offEver = offEver || off.update(0.0f, t);
        onEver = onEver || !on.update(1.0f, t);
    }
    TEST_ASSERT_FALSE(offEver); // never opened
    TEST_ASSERT_FALSE(onEver);  // never closed
}

// --- latching ---

void test_duty_is_latched_for_the_cycle() {
    TimeProportionalOutput tpo(LONG_CYCLE, SHORT_TRAVEL);
    tpo.update(0.25f, 1000);
    const float latched = tpo.latchedDuty();

    // Demand jumps mid-cycle; the current cycle must ignore it.
    tpo.update(0.90f, 1000 + LONG_CYCLE / 4);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, latched, tpo.latchedDuty());

    // The next boundary picks it up.
    tpo.update(0.90f, 1000 + LONG_CYCLE + 1);
    TEST_ASSERT_TRUE(tpo.latchedDuty() > latched);
}

void test_cycle_count_advances_at_boundaries() {
    TimeProportionalOutput tpo(LONG_CYCLE, SHORT_TRAVEL);
    tpo.update(0.5f, 1000);
    TEST_ASSERT_EQUAL_UINT32(1, tpo.completedCycles());
    tpo.update(0.5f, 1000 + LONG_CYCLE / 2);
    TEST_ASSERT_EQUAL_UINT32(1, tpo.completedCycles());
    tpo.update(0.5f, 1000 + LONG_CYCLE);
    TEST_ASSERT_EQUAL_UINT32(2, tpo.completedCycles());
}

// --- reset ---

void test_reset_closes_and_restarts() {
    TimeProportionalOutput tpo(LONG_CYCLE, SHORT_TRAVEL);
    tpo.update(1.0f, 1000);
    TEST_ASSERT_TRUE(tpo.isOpen());

    tpo.reset();
    TEST_ASSERT_FALSE(tpo.isOpen());
    TEST_ASSERT_EQUAL_UINT32(0, tpo.completedCycles());

    // A fresh cycle latches immediately rather than resuming a stale one.
    tpo.update(0.5f, 500000);
    TEST_ASSERT_EQUAL_UINT32(1, tpo.completedCycles());
}

void test_set_timing_resets() {
    TimeProportionalOutput tpo(LONG_CYCLE, SHORT_TRAVEL);
    tpo.update(1.0f, 1000);
    TEST_ASSERT_TRUE(tpo.isOpen());
    // The cycle in progress was computed for timings that no longer apply.
    tpo.setTiming(CYCLE, TRAVEL);
    TEST_ASSERT_FALSE(tpo.isOpen());
}

// --- robustness ---

void test_nan_and_out_of_range_demand() {
    TimeProportionalOutput tpo(LONG_CYCLE, SHORT_TRAVEL);
    tpo.update(std::numeric_limits<float>::quiet_NaN(), 1000);
    TEST_ASSERT_FALSE(tpo.isOpen());

    TimeProportionalOutput hi(LONG_CYCLE, SHORT_TRAVEL);
    hi.update(5.0f, 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, hi.latchedDuty());

    TimeProportionalOutput lo(LONG_CYCLE, SHORT_TRAVEL);
    lo.update(-5.0f, 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, lo.latchedDuty());
}

void test_cycle_spanning_millis_rollover() {
    TimeProportionalOutput tpo(LONG_CYCLE, SHORT_TRAVEL);
    // Begin a cycle shortly before the 32-bit wrap so it straddles it.
    const uint32_t start = std::numeric_limits<uint32_t>::max() - (LONG_CYCLE / 2);
    const uint32_t step = 10u * 1000u;
    const uint32_t openFor = measureOpenMs(tpo, 0.5f, start, LONG_CYCLE, step);
    // Signed or naive subtraction here would end the cycle instantly at the
    // wrap and report a wildly wrong open time.
    TEST_ASSERT_UINT32_WITHIN(step * 2, LONG_CYCLE / 2, openFor);
    TEST_ASSERT_EQUAL_UINT32(1, tpo.completedCycles());
}

void test_instances_do_not_share_state() {
    TimeProportionalOutput a(LONG_CYCLE, SHORT_TRAVEL);
    TimeProportionalOutput b(LONG_CYCLE, SHORT_TRAVEL);
    a.update(1.0f, 1000);
    TEST_ASSERT_TRUE(a.isOpen());
    TEST_ASSERT_FALSE(b.isOpen());
    TEST_ASSERT_EQUAL_UINT32(0, b.completedCycles());
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_timing_valid_requires_four_strokes);
    RUN_TEST(test_half_duty_is_half_the_cycle);
    RUN_TEST(test_thirty_percent_duty);
    RUN_TEST(test_small_duty_delivers_nothing_in_its_first_cycle);
    RUN_TEST(test_small_duty_averages_correctly_over_many_cycles);
    RUN_TEST(test_ten_percent_duty_averages_correctly);
    RUN_TEST(test_high_duty_averages_correctly_despite_rounding_up);
    RUN_TEST(test_mid_range_duty_is_unaffected_by_skipping);
    RUN_TEST(test_zero_demand_discards_credit);
    RUN_TEST(test_reset_discards_credit);
    RUN_TEST(test_thirty_percent_duty_with_a_faster_actuator);
    RUN_TEST(test_tiny_duty_snaps_closed);
    RUN_TEST(test_near_full_duty_snaps_open);
    RUN_TEST(test_zero_and_full_never_switch);
    RUN_TEST(test_duty_is_latched_for_the_cycle);
    RUN_TEST(test_cycle_count_advances_at_boundaries);
    RUN_TEST(test_reset_closes_and_restarts);
    RUN_TEST(test_set_timing_resets);
    RUN_TEST(test_nan_and_out_of_range_demand);
    RUN_TEST(test_cycle_spanning_millis_rollover);
    RUN_TEST(test_instances_do_not_share_state);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
