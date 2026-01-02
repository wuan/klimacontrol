#include "unity.h"
#include "../MockStrip.h"
#include "show/Fire.h"

Show::FireState *state;

void setUp() {
    state = new Show::FireState([] { return 1.0f; }, 10);
}

void tearDown() {
    delete state;
}

void test_default_value_zero() {
    TEST_ASSERT_EQUAL(0.0f, state->get_temperature(0));
}

void test_cooldown_limited_at_zero() {
    state->cooldown(1.0);

    TEST_ASSERT_EQUAL(0.0f, state->get_temperature(0));
}

void test_cooldown() {
    state->set_temperature(0, 1.5f);

    state->cooldown(1.0);

    TEST_ASSERT_EQUAL(0.5f, state->get_temperature(0));
}

void test_spread() {
    state->set_temperature(0, 1.0f);
    state->spread(1.0, 0.0, 0, 0.5f);

    // With double-buffering, heat only spreads one step per frame
    // Heat from index 0 spreads to index 1, not teleporting to index 9
    TEST_ASSERT_EQUAL_FLOAT(0.75f, state->get_temperature(0));
    TEST_ASSERT_EQUAL_FLOAT(0.25f, state->get_temperature(1));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, state->get_temperature(9));
}

void test_spread_limited() {
    state->set_temperature(0, 0.1f);
    state->spread(1.0, 0.0, 0, 0.5f);

    // With double-buffering, heat spreads to adjacent pixel only
    TEST_ASSERT_EQUAL_FLOAT(0.0f, state->get_temperature(0));
    TEST_ASSERT_EQUAL_FLOAT(0.1f, state->get_temperature(1));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, state->get_temperature(9));
}

void test_create_fire() {
    auto cooling = 1.0;
    auto spread = 1.0;
    auto ignition = 1.0;

    auto show = new Show::Fire(cooling, spread, ignition);

    TEST_ASSERT_NOT_NULL(show);
}

void test_spread_multiple_weights() {
    delete state;
    state = new Show::FireState([] { return 1.0f; }, 10);
    state->set_temperature(0, 1.0f);
    state->set_temperature(1, 1.0f);

    // With double-buffering, all reads come from the snapshot.
    // weights = {2.0f, 1.0f} -> at i=2, weights for prev_idx 1 and 0
    // i=1: takes 0.25 from index 0 (temp[0]: 1.0->0.75, temp[1]: 1.0->1.25)
    // i=2: reads prev_temp[1]=1.0, prev_temp[0]=1.0, spreads 0.25
    //      temp[1] -= 0.25*2/3, temp[0] -= 0.25*1/3
    // i=3: reads prev_temp[2]=0, prev_temp[1]=1.0, spreads 0.25
    //      temp[2] -= 0.25*2/3, temp[1] -= 0.25*1/3

    state->spread(1.0, 0.0, 0, 0.5f, {2.0f, 1.0f});

    // Verify energy conservation
    float total = 0;
    for (int i = 0; i < state->length(); i++) {
        total += state->get_temperature(i);
    }
    TEST_ASSERT_EQUAL_FLOAT(2.0f, total);

    TEST_ASSERT_EQUAL_FLOAT(0.6666667f, state->get_temperature(0));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, state->get_temperature(1));
    TEST_ASSERT_EQUAL_FLOAT(0.0833333f, state->get_temperature(2));
}

void test_spark_amount() {
    state->set_temperature(0, 0.0f);
    state->spread(0.0, 1.0, 1, 0.7f);
    TEST_ASSERT_EQUAL_FLOAT(0.7f, state->get_temperature(0));
}

int runUnityTests() {
    UNITY_BEGIN();

    // Test parseColors logic (used by ShowFactory)
    RUN_TEST(test_default_value_zero);
    RUN_TEST(test_cooldown_limited_at_zero);
    RUN_TEST(test_cooldown);
    RUN_TEST(test_spread);
    RUN_TEST(test_spread_limited);
    RUN_TEST(test_spread_multiple_weights);
    RUN_TEST(test_spark_amount);
    RUN_TEST(test_create_fire);

    return UNITY_END();
}

int main() {
    return runUnityTests();
}
