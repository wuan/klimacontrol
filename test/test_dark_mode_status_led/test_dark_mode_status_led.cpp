#include "unity.h"
#include "DarkModeStatusLed.h"

// Dark-mode policy lives in the DarkModeStatusLed decorator; the wrapped
// StatusLed stays a plain state-to-colour machine. These tests drive the
// wrapper with an injected clock and observe the wrapped LED's colour and
// effective state.

DarkModeStatusLed* testLed;

void setUp() {
    testLed = new DarkModeStatusLed();
    testLed->begin();
}

void tearDown() {
    delete testLed;
}

void test_forwards_state_and_progress_when_not_dark() {
    testLed->setProgress(0.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, testLed->getProgress());
    testLed->setState(LedState::STARTUP);
    TEST_ASSERT_EQUAL(LedState::STARTUP, testLed->getState());
    TEST_ASSERT_EQUAL(LedState::STARTUP, testLed->inner().getState());
    testLed->on();
    TEST_ASSERT_EQUAL(LedState::ON, testLed->inner().getState());
    testLed->toggle();
    TEST_ASSERT_EQUAL(LedState::OFF, testLed->getState());
    TEST_ASSERT_EQUAL(LedState::OFF, testLed->inner().getState());
}

void test_dark_inner_is_off_while_logical_is_on() {
    testLed->setDarkAfterSeconds(300);
    testLed->update(1000);
    testLed->setState(LedState::ON);
    testLed->update(1000 + 300000);
    TEST_ASSERT_TRUE(testLed->isDark(1000 + 300000));
    TEST_ASSERT_EQUAL(LedState::ON, testLed->getState());
    TEST_ASSERT_EQUAL(LedState::OFF, testLed->inner().getState());
}

// --- Dark mode ---

static constexpr uint32_t DARK_S = 300;
static constexpr uint32_t DARK_MS = DARK_S * 1000u;
static constexpr uint32_t BLACK = 0x000000;
static constexpr uint32_t GREEN = 0x000F00;   // progress 0.0
static constexpr uint32_t RED_ERR = 0x0F0000;
static constexpr uint32_t BLUE_STARTUP = 0x00000F;
static constexpr uint32_t FLASH = 0x020202;

void test_dark_default_disabled() {
    TEST_ASSERT_EQUAL(0, testLed->getDarkAfterSeconds());
    testLed->update(1000);
    testLed->setState(LedState::ON);
    testLed->update(1000u + 100u * 3600u * 1000u); // 100 hours later
    TEST_ASSERT_EQUAL_HEX32(GREEN, testLed->inner().lastColor());
}

void test_dark_set_and_get_threshold() {
    testLed->setDarkAfterSeconds(DARK_S);
    TEST_ASSERT_EQUAL(DARK_S, testLed->getDarkAfterSeconds());
}

void test_dark_gradient_before_threshold() {
    testLed->setDarkAfterSeconds(DARK_S);
    testLed->update(1000);
    testLed->setState(LedState::ON);
    testLed->update(200000);
    TEST_ASSERT_EQUAL_HEX32(GREEN, testLed->inner().lastColor());
    TEST_ASSERT_EQUAL(LedState::ON, testLed->getState());
}

void test_dark_engages_after_threshold_state_stays_on() {
    testLed->setDarkAfterSeconds(DARK_S);
    testLed->update(1000);
    testLed->setState(LedState::ON);
    testLed->update(1000 + DARK_MS - 1);
    TEST_ASSERT_EQUAL_HEX32(GREEN, testLed->inner().lastColor());
    testLed->update(1000 + DARK_MS);
    TEST_ASSERT_EQUAL_HEX32(BLACK, testLed->inner().lastColor());
    TEST_ASSERT_EQUAL(LedState::ON, testLed->getState());
}

void test_dark_gradient_changes_do_not_relight() {
    testLed->setDarkAfterSeconds(DARK_S);
    testLed->update(1000);
    testLed->setState(LedState::ON);
    testLed->update(1000 + DARK_MS);
    testLed->setProgress(0.5f);
    testLed->update(2000 + DARK_MS);
    TEST_ASSERT_EQUAL_HEX32(BLACK, testLed->inner().lastColor());
}

void test_dark_suppresses_publish_flash() {
    testLed->setDarkAfterSeconds(DARK_S);
    testLed->update(1000);
    testLed->setState(LedState::ON);
    testLed->update(1000 + DARK_MS);
    testLed->setState(LedState::TRANSMIT_DATA);
    TEST_ASSERT_EQUAL_HEX32(BLACK, testLed->inner().lastColor());
    testLed->update(2000 + DARK_MS);
    TEST_ASSERT_EQUAL_HEX32(BLACK, testLed->inner().lastColor());
    testLed->setState(LedState::ON);
    testLed->update(3000 + DARK_MS);
    TEST_ASSERT_EQUAL_HEX32(BLACK, testLed->inner().lastColor());
}

void test_dark_publish_flash_does_not_reset_anchor() {
    // Publish every 15 s must not keep the timer from ever expiring.
    testLed->setDarkAfterSeconds(DARK_S);
    testLed->update(0);
    testLed->setState(LedState::ON);
    for (uint32_t t = 1000; t < DARK_MS; t += 1000) {
        if (t % 15000 == 0) testLed->setState(LedState::TRANSMIT_DATA);
        else testLed->setState(LedState::ON);
        testLed->update(t);
        TEST_ASSERT_NOT_EQUAL(BLACK, testLed->inner().lastColor());
    }
    testLed->setState(LedState::ON);
    testLed->update(DARK_MS);
    TEST_ASSERT_EQUAL_HEX32(BLACK, testLed->inner().lastColor());
}

void test_dark_flash_visible_before_threshold() {
    testLed->setDarkAfterSeconds(DARK_S);
    testLed->update(1000);
    testLed->setState(LedState::ON);
    testLed->update(16000);
    testLed->setState(LedState::TRANSMIT_DATA);
    TEST_ASSERT_EQUAL_HEX32(FLASH, testLed->inner().lastColor());
}

void test_dark_reconnect_rearms_timer() {
    testLed->setDarkAfterSeconds(DARK_S);
    testLed->update(1000);
    testLed->setState(LedState::ON);
    testLed->update(1000 + DARK_MS);
    TEST_ASSERT_EQUAL_HEX32(BLACK, testLed->inner().lastColor());

    // WiFi drops: STARTUP, then reconnect at T.
    testLed->setState(LedState::STARTUP);
    TEST_ASSERT_EQUAL_HEX32(BLUE_STARTUP, testLed->inner().lastColor());
    const uint32_t T = 5000 + DARK_MS;
    testLed->update(T);
    testLed->setState(LedState::ON);
    TEST_ASSERT_EQUAL_HEX32(GREEN, testLed->inner().lastColor());
    testLed->update(T + DARK_MS - 1);
    TEST_ASSERT_EQUAL_HEX32(GREEN, testLed->inner().lastColor());
    testLed->update(T + DARK_MS);
    TEST_ASSERT_EQUAL_HEX32(BLACK, testLed->inner().lastColor());
}

void test_dark_error_always_visible() {
    testLed->setDarkAfterSeconds(DARK_S);
    testLed->update(1000);
    testLed->setState(LedState::ON);
    testLed->update(1000 + DARK_MS);
    testLed->setState(LedState::ERROR);
    TEST_ASSERT_EQUAL_HEX32(RED_ERR, testLed->inner().lastColor());
    testLed->update(1000 + 2 * DARK_MS);
    TEST_ASSERT_EQUAL_HEX32(RED_ERR, testLed->inner().lastColor());
}

void test_dark_startup_always_visible() {
    testLed->setDarkAfterSeconds(DARK_S);
    testLed->update(1000);
    testLed->setState(LedState::ON);
    testLed->update(1000 + DARK_MS);
    testLed->setState(LedState::STARTUP);
    TEST_ASSERT_EQUAL_HEX32(BLUE_STARTUP, testLed->inner().lastColor());
}

void test_dark_off_state_clears_anchor() {
    testLed->setDarkAfterSeconds(DARK_S);
    testLed->update(1000);
    testLed->setState(LedState::ON);
    testLed->update(1000 + DARK_MS);
    testLed->setState(LedState::OFF);
    testLed->update(2000 + DARK_MS);
    testLed->setState(LedState::ON);
    TEST_ASSERT_EQUAL_HEX32(GREEN, testLed->inner().lastColor());
}

void test_dark_threshold_zero_disables() {
    testLed->setDarkAfterSeconds(0);
    testLed->update(1000);
    testLed->setState(LedState::ON);
    testLed->update(1000 + 10 * DARK_MS);
    TEST_ASSERT_EQUAL_HEX32(GREEN, testLed->inner().lastColor());
}

void test_dark_threshold_change_applies_live() {
    testLed->setDarkAfterSeconds(DARK_S);
    testLed->update(1000);
    testLed->setState(LedState::ON);
    testLed->update(1000 + DARK_MS);
    TEST_ASSERT_EQUAL_HEX32(BLACK, testLed->inner().lastColor());

    // Disabled from another task: next update re-lights without a transition.
    testLed->setDarkAfterSeconds(0);
    testLed->update(2000 + DARK_MS);
    TEST_ASSERT_EQUAL_HEX32(GREEN, testLed->inner().lastColor());

    // Shortened threshold engages immediately on the existing anchor.
    testLed->setDarkAfterSeconds(60);
    testLed->update(3000 + DARK_MS);
    TEST_ASSERT_EQUAL_HEX32(BLACK, testLed->inner().lastColor());
}

void test_dark_survives_uint32_wraparound() {
    testLed->setDarkAfterSeconds(DARK_S);
    const uint32_t start = 0xFFFFFFFFu - 10000u; // 10 s before millis() wraps
    testLed->update(start);
    testLed->setState(LedState::ON);
    testLed->update(start + 5000u);            // still before wrap
    TEST_ASSERT_EQUAL_HEX32(GREEN, testLed->inner().lastColor());
    testLed->update(start + DARK_MS - 1);      // wrapped, just under threshold
    TEST_ASSERT_EQUAL_HEX32(GREEN, testLed->inner().lastColor());
    testLed->update(start + DARK_MS);          // wrapped, at threshold
    TEST_ASSERT_EQUAL_HEX32(BLACK, testLed->inner().lastColor());
}

void test_dark_set_state_before_first_update_anchors_at_zero() {
    testLed->setDarkAfterSeconds(DARK_S);
    testLed->setState(LedState::ON);           // no update() yet: anchor = 0
    testLed->update(DARK_MS - 1);
    TEST_ASSERT_EQUAL_HEX32(GREEN, testLed->inner().lastColor());
    testLed->update(DARK_MS);
    TEST_ASSERT_EQUAL_HEX32(BLACK, testLed->inner().lastColor());
}

// --- Power rail follows dark mode ---

static void engageDark() {
    testLed->setDarkAfterSeconds(DARK_S);
    testLed->update(1000);
    testLed->setState(LedState::ON);
    testLed->update(1000 + DARK_MS);
    TEST_ASSERT_TRUE(testLed->isDark(1000 + DARK_MS));
}

void test_rail_on_before_dark() {
    testLed->setDarkAfterSeconds(DARK_S);
    testLed->update(1000);
    testLed->setState(LedState::ON);
    testLed->update(1000 + DARK_MS - 1);
    TEST_ASSERT_TRUE(testLed->inner().isPowerRailOn());
}

void test_rail_cut_while_dark() {
    engageDark();
    TEST_ASSERT_FALSE(testLed->inner().isPowerRailOn());
    TEST_ASSERT_EQUAL_HEX32(BLACK, testLed->inner().lastColor());
    // Repeated updates and suppressed flashes leave it cut.
    testLed->setState(LedState::TRANSMIT_DATA);
    testLed->update(1000 + DARK_MS + 5000);
    TEST_ASSERT_FALSE(testLed->inner().isPowerRailOn());
}

void test_rail_restored_on_threshold_change() {
    engageDark();
    testLed->setDarkAfterSeconds(0);
    testLed->update(1000 + DARK_MS + 1000);
    TEST_ASSERT_TRUE(testLed->inner().isPowerRailOn());
    TEST_ASSERT_EQUAL_HEX32(GREEN, testLed->inner().lastColor());
}

void test_rail_restored_on_startup() {
    engageDark();
    testLed->setState(LedState::STARTUP);
    TEST_ASSERT_TRUE(testLed->inner().isPowerRailOn());
    TEST_ASSERT_EQUAL_HEX32(BLUE_STARTUP, testLed->inner().lastColor());
}

void test_rail_restored_on_error() {
    engageDark();
    testLed->setState(LedState::ERROR);
    TEST_ASSERT_TRUE(testLed->inner().isPowerRailOn());
    TEST_ASSERT_EQUAL_HEX32(RED_ERR, testLed->inner().lastColor());
}

void test_rail_restored_on_logical_off_then_rerendered_black() {
    engageDark();
    testLed->setState(LedState::OFF);
    TEST_ASSERT_TRUE(testLed->inner().isPowerRailOn());
    testLed->update(1000 + DARK_MS + 1000);
    TEST_ASSERT_EQUAL_HEX32(BLACK, testLed->inner().lastColor());
}

void test_logical_off_outside_dark_leaves_rail_on() {
    testLed->setDarkAfterSeconds(DARK_S);
    testLed->update(1000);
    testLed->setState(LedState::ON);
    testLed->setState(LedState::OFF);
    testLed->update(1000 + DARK_MS + 1000);
    TEST_ASSERT_TRUE(testLed->inner().isPowerRailOn());
}

void test_rail_cycles_on_reconnect() {
    engageDark();
    testLed->setState(LedState::STARTUP);
    TEST_ASSERT_TRUE(testLed->inner().isPowerRailOn());
    const uint32_t t = 1000 + DARK_MS + 5000;
    testLed->update(t);
    testLed->setState(LedState::ON);
    testLed->update(t + DARK_MS - 1);
    TEST_ASSERT_TRUE(testLed->inner().isPowerRailOn());
    testLed->update(t + DARK_MS);
    TEST_ASSERT_FALSE(testLed->inner().isPowerRailOn());
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_forwards_state_and_progress_when_not_dark);
    RUN_TEST(test_dark_inner_is_off_while_logical_is_on);
    RUN_TEST(test_dark_default_disabled);
    RUN_TEST(test_dark_set_and_get_threshold);
    RUN_TEST(test_dark_gradient_before_threshold);
    RUN_TEST(test_dark_engages_after_threshold_state_stays_on);
    RUN_TEST(test_dark_gradient_changes_do_not_relight);
    RUN_TEST(test_dark_suppresses_publish_flash);
    RUN_TEST(test_dark_publish_flash_does_not_reset_anchor);
    RUN_TEST(test_dark_flash_visible_before_threshold);
    RUN_TEST(test_dark_reconnect_rearms_timer);
    RUN_TEST(test_dark_error_always_visible);
    RUN_TEST(test_dark_startup_always_visible);
    RUN_TEST(test_dark_off_state_clears_anchor);
    RUN_TEST(test_dark_threshold_zero_disables);
    RUN_TEST(test_dark_threshold_change_applies_live);
    RUN_TEST(test_dark_survives_uint32_wraparound);
    RUN_TEST(test_dark_set_state_before_first_update_anchors_at_zero);
    RUN_TEST(test_rail_on_before_dark);
    RUN_TEST(test_rail_cut_while_dark);
    RUN_TEST(test_rail_restored_on_threshold_change);
    RUN_TEST(test_rail_restored_on_startup);
    RUN_TEST(test_rail_restored_on_error);
    RUN_TEST(test_rail_restored_on_logical_off_then_rerendered_black);
    RUN_TEST(test_logical_off_outside_dark_leaves_rail_on);
    RUN_TEST(test_rail_cycles_on_reconnect);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
