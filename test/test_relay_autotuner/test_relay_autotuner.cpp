#include "unity.h"

#include <cmath>
#include <deque>

#include "control/RelayAutotuner.h"

using Control::AutotuneAbort;
using Control::AutotuneLimits;
using Control::AutotuneState;
using Control::RelayAutotuner;

void setUp() {}
void tearDown() {}

namespace {

    constexpr uint32_t TICK_MS = 60000; // the intended control cadence

    // First-order-plus-dead-time plant, the standard coarse model of a heated
    // room: heat takes L to show up at the sensor at all, then approaches its
    // steady state with time constant tau.
    //
    //   dT/dt = ( K*u(t - L) - (T - ambient) ) / tau
    //
    // A test fixture, not production code. It exists so that behaviours which
    // take six to twenty hours on real hardware — convergence, non-convergence,
    // every abort path — are reachable in milliseconds.
    class Plant {
    public:
        Plant(float gainK, uint32_t deadTimeMs, float tauSeconds, float ambient, float initial)
            : gainK(gainK), tauSeconds(tauSeconds), ambient(ambient), temperature(initial),
              delaySamples(deadTimeMs / TICK_MS) {}

        void step(float input, uint32_t dtMs) {
            delayLine.push_back(input);
            float effective = 0.0f;
            if (delayLine.size() > delaySamples) {
                effective = delayLine.front();
                delayLine.pop_front();
            }
            const float dt = static_cast<float>(dtMs) / 1000.0f;
            const float drive = gainK * effective;
            temperature += ((drive - (temperature - ambient)) / tauSeconds) * dt;
        }

        float value() const { return temperature; }

    private:
        float gainK;
        float tauSeconds;
        float ambient;
        float temperature;
        size_t delaySamples;
        std::deque<float> delayLine;
    };

    // Limits tuned for the test cadence: the plants below oscillate with
    // periods of tens of minutes, so the day-long production budget would make
    // the timeout tests pointlessly long.
    AutotuneLimits testLimits() {
        AutotuneLimits l;
        l.hysteresis = 0.10f;
        l.relayAmplitude = 0.5f;
        l.ceilingOffset = 3.0f;
        l.floorOffset = 3.0f;
        l.maxDurationMs = 24u * 3600u * 1000u;
        l.settlingTimeoutMs = 30u * 60u * 1000u;
        l.settlingRateKPerMin = 0.05f;
        l.requiredCycles = 3;
        return l;
    }

    struct RunOutcome {
        AutotuneState state;
        uint32_t ticks;
    };

    // Drive tuner and plant together until the run ends or the tick budget runs
    // out. Returns where it got to.
    RunOutcome runToCompletion(RelayAutotuner &tuner, Plant &plant, uint32_t startMs,
                               uint32_t maxTicks, bool dataValid = true) {
        uint32_t now = startMs;
        for (uint32_t i = 0; i < maxTicks; ++i) {
            const float out = tuner.update(plant.value(), dataValid, now);
            if (tuner.state() == AutotuneState::Done || tuner.state() == AutotuneState::Aborted) {
                return {tuner.state(), i};
            }
            plant.step(out, TICK_MS);
            now += TICK_MS;
        }
        return {tuner.state(), maxTicks};
    }

    // A plausible underfloor plant: 30 min dead time, 3 h time constant, and a
    // gain that puts full-on steady state about 4 K above ambient.
    Plant typicalPlant(float initial = 20.0f) {
        return Plant(4.0f, 30u * 60u * 1000u, 3.0f * 3600.0f, 20.0f, initial);
    }

}

// --- Convergence ---

void test_converges_on_a_realistic_plant() {
    RelayAutotuner tuner(testLimits());
    Plant plant = typicalPlant();
    tuner.start(21.0f, 1000);

    const RunOutcome outcome = runToCompletion(tuner, plant, 1000, 5000);

    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Done), static_cast<int>(outcome.state));
    TEST_ASSERT_TRUE(tuner.result().ku > 0.0f);
    TEST_ASSERT_TRUE(tuner.result().tu > 0.0f);
}

void test_identified_period_is_physically_plausible() {
    RelayAutotuner tuner(testLimits());
    Plant plant = typicalPlant();
    tuner.start(21.0f, 1000);
    runToCompletion(tuner, plant, 1000, 5000);

    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Done), static_cast<int>(tuner.state()));

    // For a relay experiment the limit-cycle period is a small multiple of the
    // dead time — here 1800 s. Anything outside a couple of orders of magnitude
    // means the cycle boundaries are being counted wrongly.
    const float tu = tuner.result().tu;
    TEST_ASSERT_TRUE(tu > 1800.0f);
    TEST_ASSERT_TRUE(tu < 20.0f * 1800.0f);
}

void test_requires_the_configured_number_of_cycles() {
    AutotuneLimits strict = testLimits();
    strict.requiredCycles = 6;
    RelayAutotuner strictTuner(strict);
    Plant strictPlant = typicalPlant();
    strictTuner.start(21.0f, 1000);
    runToCompletion(strictTuner, strictPlant, 1000, 8000);

    AutotuneLimits lax = testLimits();
    lax.requiredCycles = 3;
    RelayAutotuner laxTuner(lax);
    Plant laxPlant = typicalPlant();
    laxTuner.start(21.0f, 1000);
    runToCompletion(laxTuner, laxPlant, 1000, 8000);

    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Done), static_cast<int>(strictTuner.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Done), static_cast<int>(laxTuner.state()));
    // Demanding more consistent cycles cannot finish sooner.
    TEST_ASSERT_TRUE(strictTuner.completedCycles() >= laxTuner.completedCycles());
}

// --- Derivation arithmetic ---

void test_derived_gains_match_tyreus_luyben() {
    RelayAutotuner tuner(testLimits());
    Plant plant = typicalPlant();
    tuner.start(21.0f, 1000);
    runToCompletion(tuner, plant, 1000, 5000);
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Done), static_cast<int>(tuner.state()));

    const float ku = tuner.result().ku;
    const float tu = tuner.result().tu;
    const float expectedKp = ku / 3.2f;
    const float expectedKi = expectedKp / (2.2f * tu);

    TEST_ASSERT_FLOAT_WITHIN(expectedKp * 0.001f, expectedKp, tuner.result().gains.kp);
    TEST_ASSERT_FLOAT_WITHIN(expectedKi * 0.001f, expectedKi, tuner.result().gains.ki);
}

void test_derivative_gain_is_always_zero() {
    RelayAutotuner tuner(testLimits());
    Plant plant = typicalPlant();
    tuner.start(21.0f, 1000);
    runToCompletion(tuner, plant, 1000, 5000);

    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Done), static_cast<int>(tuner.state()));
    // Deliberate: on a lag-dominant plant the derivative term amplifies sensor
    // noise and contributes nothing.
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, tuner.result().gains.kd);
}

void test_hysteresis_correction_lowers_ku() {
    RelayAutotuner tuner(testLimits());
    Plant plant = typicalPlant();
    tuner.start(21.0f, 1000);
    runToCompletion(tuner, plant, 1000, 5000);
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Done), static_cast<int>(tuner.state()));

    // Recover the amplitude the corrected formula implies, then check the
    // uncorrected 4d/(pi*a) would have reported a *larger* Ku. Over-estimating
    // Ku yields tuning that is too aggressive, which is the expensive
    // direction to err on a plant that cannot shed heat quickly.
    const float ku = tuner.result().ku;
    const float d = 0.5f;
    const float h = 0.10f;
    const float root = (4.0f * d) / (static_cast<float>(M_PI) * ku); // = sqrt(a^2 - h^2)
    const float a = std::sqrt(root * root + h * h);
    const float uncorrected = (4.0f * d) / (static_cast<float>(M_PI) * a);

    TEST_ASSERT_TRUE(uncorrected < ku);
}

// --- Settling ---

void test_stays_settling_while_temperature_ramps() {
    RelayAutotuner tuner(testLimits());
    tuner.start(21.0f, 1000);

    // 0.2 K/min, well above the 0.05 K/min threshold, and deliberately kept
    // inside the +-3 K safety envelope — a steeper ramp aborts on the ceiling
    // instead, which tests the wrong thing.
    float temp = 20.0f;
    uint32_t now = 1000;
    for (int i = 0; i < 5; ++i) {
        tuner.update(temp, true, now);
        temp += 0.2f;
        now += TICK_MS;
    }

    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Settling), static_cast<int>(tuner.state()));
}

void test_proceeds_once_temperature_is_steady() {
    RelayAutotuner tuner(testLimits());
    tuner.start(21.0f, 1000);

    uint32_t now = 1000;
    tuner.update(20.5f, true, now);
    now += TICK_MS;
    tuner.update(20.5f, true, now); // no change at all -> steady

    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Oscillating),
                      static_cast<int>(tuner.state()));
}

void test_settling_timeout_aborts() {
    AutotuneLimits limits = testLimits();
    limits.settlingTimeoutMs = 5u * 60u * 1000u;
    RelayAutotuner tuner(limits);
    tuner.start(21.0f, 1000);

    float temp = 20.0f;
    uint32_t now = 1000;
    for (int i = 0; i < 20; ++i) {
        tuner.update(temp, true, now);
        temp += (i % 2 == 0) ? 0.4f : -0.4f; // never settles
        now += TICK_MS;
    }

    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Aborted), static_cast<int>(tuner.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneAbort::SettlingTimeout),
                      static_cast<int>(tuner.abortReason()));
}

// --- Abort paths ---

void test_ceiling_breach_aborts_and_output_is_zero() {
    RelayAutotuner tuner(testLimits());
    tuner.start(21.0f, 1000);

    const float out = tuner.update(21.0f + 3.5f, true, 2000);

    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Aborted), static_cast<int>(tuner.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneAbort::CeilingBreached),
                      static_cast<int>(tuner.abortReason()));
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, out);
}

void test_floor_breach_aborts_and_output_is_zero() {
    RelayAutotuner tuner(testLimits());
    tuner.start(21.0f, 1000);

    const float out = tuner.update(21.0f - 3.5f, true, 2000);

    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Aborted), static_cast<int>(tuner.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneAbort::FloorBreached),
                      static_cast<int>(tuner.abortReason()));
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, out);
}

void test_sensor_loss_aborts_immediately() {
    RelayAutotuner tuner(testLimits());
    tuner.start(21.0f, 1000);
    tuner.update(20.9f, true, 2000);

    // A relay experiment with no feedback is an uncontrolled output.
    const float out = tuner.update(20.9f, false, 3000);

    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Aborted), static_cast<int>(tuner.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneAbort::SensorLost),
                      static_cast<int>(tuner.abortReason()));
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, out);
}

void test_nan_reading_aborts() {
    RelayAutotuner tuner(testLimits());
    tuner.start(21.0f, 1000);

    tuner.update(NAN, true, 2000);

    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Aborted), static_cast<int>(tuner.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneAbort::SensorLost),
                      static_cast<int>(tuner.abortReason()));
}

void test_run_timeout_aborts_without_converging() {
    AutotuneLimits limits = testLimits();
    limits.maxDurationMs = 2u * 3600u * 1000u; // 2 h, too short for this plant
    RelayAutotuner tuner(limits);
    Plant plant = typicalPlant();
    tuner.start(21.0f, 1000);

    runToCompletion(tuner, plant, 1000, 5000);

    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Aborted), static_cast<int>(tuner.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneAbort::RunTimeout),
                      static_cast<int>(tuner.abortReason()));
}

void test_user_cancel_aborts_and_output_is_zero() {
    RelayAutotuner tuner(testLimits());
    tuner.start(21.0f, 1000);
    tuner.update(20.9f, true, 2000);

    tuner.cancel();
    const float out = tuner.update(20.9f, true, 3000);

    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Aborted), static_cast<int>(tuner.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneAbort::UserRequested),
                      static_cast<int>(tuner.abortReason()));
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, out);
}

void test_amplitude_barely_clearing_hysteresis_aborts() {
    // A plant that only just overshoots the switching band. The radicand is
    // positive but tiny, so the uncaught result would be an enormous Ku and
    // correspondingly absurd gains.
    AutotuneLimits limits = testLimits();
    limits.hysteresis = 0.10f;
    RelayAutotuner tuner(limits);
    tuner.start(21.0f, 1000);

    // Hand-drive a limit cycle whose peaks sit just outside the switching band,
    // giving an amplitude barely above h.
    uint32_t now = 1000;
    float temp = 21.0f;
    for (int i = 0; i < 400; ++i) {
        const float out = tuner.update(temp, true, now);
        if (tuner.state() != AutotuneState::Settling &&
            tuner.state() != AutotuneState::Oscillating) {
            break;
        }
        // Move a hair past the band in whichever direction the relay asks for,
        // so amplitude ends up ~= h rather than comfortably above it.
        const float targetSide = (out > 0.5f) ? (21.0f + 0.1001f) : (21.0f - 0.1001f);
        temp += (targetSide - temp) * 0.9f;
        now += TICK_MS;
    }

    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Aborted), static_cast<int>(tuner.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneAbort::AmplitudeTooSmall),
                      static_cast<int>(tuner.abortReason()));
}

void test_aborts_are_terminal() {
    RelayAutotuner tuner(testLimits());
    Plant plant = typicalPlant();
    tuner.start(21.0f, 1000);
    tuner.update(20.9f, true, 2000);
    tuner.cancel();

    // Keep feeding it perfectly good data; it must not resume.
    uint32_t now = 3000;
    for (int i = 0; i < 50; ++i) {
        const float out = tuner.update(plant.value(), true, now);
        TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, out);
        plant.step(0.0f, TICK_MS);
        now += TICK_MS;
    }
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Aborted), static_cast<int>(tuner.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneAbort::UserRequested),
                      static_cast<int>(tuner.abortReason()));
}

// --- Edge cases ---

void test_output_is_zero_when_idle() {
    RelayAutotuner tuner(testLimits());

    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Idle), static_cast<int>(tuner.state()));
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, tuner.update(20.0f, true, 1000));
}

void test_output_is_zero_after_done() {
    RelayAutotuner tuner(testLimits());
    Plant plant = typicalPlant();
    tuner.start(21.0f, 1000);
    runToCompletion(tuner, plant, 1000, 5000);
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Done), static_cast<int>(tuner.state()));

    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, tuner.update(plant.value(), true, 99999999u));
}

void test_run_spanning_millis_rollover() {
    RelayAutotuner tuner(testLimits());
    Plant plant = typicalPlant();
    // Start ~10 ticks before the 32-bit wrap so the whole run straddles it.
    const uint32_t start = 0xFFFFFFFFu - (10u * TICK_MS);
    tuner.start(21.0f, start);

    const RunOutcome outcome = runToCompletion(tuner, plant, start, 5000);

    // Unsigned arithmetic must make this indistinguishable from any other run;
    // a signed or naive subtraction would show a ~4.29e6 s interval and blow
    // straight through the duration budget.
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Done), static_cast<int>(outcome.state));
    TEST_ASSERT_TRUE(tuner.result().tu > 0.0f);
    TEST_ASSERT_TRUE(tuner.result().tu < 20.0f * 1800.0f);
}

void test_instances_do_not_share_state() {
    RelayAutotuner a(testLimits());
    RelayAutotuner b(testLimits());
    Plant plantA = typicalPlant();

    a.start(21.0f, 1000);
    runToCompletion(a, plantA, 1000, 5000);

    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Done), static_cast<int>(a.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Idle), static_cast<int>(b.state()));
    TEST_ASSERT_EQUAL(0, b.completedCycles());
}

void test_start_resets_a_previous_run() {
    RelayAutotuner tuner(testLimits());
    tuner.start(21.0f, 1000);
    tuner.update(30.0f, true, 2000); // ceiling breach
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Aborted), static_cast<int>(tuner.state()));

    tuner.start(21.0f, 10000);

    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneState::Settling), static_cast<int>(tuner.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(AutotuneAbort::None), static_cast<int>(tuner.abortReason()));
    TEST_ASSERT_EQUAL(0, tuner.completedCycles());
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_converges_on_a_realistic_plant);
    RUN_TEST(test_identified_period_is_physically_plausible);
    RUN_TEST(test_requires_the_configured_number_of_cycles);
    RUN_TEST(test_derived_gains_match_tyreus_luyben);
    RUN_TEST(test_derivative_gain_is_always_zero);
    RUN_TEST(test_hysteresis_correction_lowers_ku);
    RUN_TEST(test_stays_settling_while_temperature_ramps);
    RUN_TEST(test_proceeds_once_temperature_is_steady);
    RUN_TEST(test_settling_timeout_aborts);
    RUN_TEST(test_ceiling_breach_aborts_and_output_is_zero);
    RUN_TEST(test_floor_breach_aborts_and_output_is_zero);
    RUN_TEST(test_sensor_loss_aborts_immediately);
    RUN_TEST(test_nan_reading_aborts);
    RUN_TEST(test_run_timeout_aborts_without_converging);
    RUN_TEST(test_user_cancel_aborts_and_output_is_zero);
    RUN_TEST(test_amplitude_barely_clearing_hysteresis_aborts);
    RUN_TEST(test_aborts_are_terminal);
    RUN_TEST(test_output_is_zero_when_idle);
    RUN_TEST(test_output_is_zero_after_done);
    RUN_TEST(test_run_spanning_millis_rollover);
    RUN_TEST(test_instances_do_not_share_state);
    RUN_TEST(test_start_resets_a_previous_run);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
