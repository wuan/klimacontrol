#include "unity.h"
#include "../MockStrip.h"
#include "support/SmoothBlend.h"

MockStrip* mock_strip;

void setUp() {
    mock_strip = new MockStrip(10);
}

void tearDown() {
    delete mock_strip;
}

void test_smooth_blend_single_color() {
    // Set initial colors
    for (int i = 0; i < 10; i++) {
        mock_strip->setPixelColor(i, 0xFF0000); // Red
    }

    // Create blend to blue with 2 second duration (default)
    Support::SmoothBlend blend(*mock_strip, 0x0000FF);

    // Initially should not be complete
    TEST_ASSERT_FALSE(blend.isComplete());

    // Do a few steps - the blend should still be in progress
    bool still_running = blend.step();
    TEST_ASSERT_TRUE(still_running);

    still_running = blend.step();
    TEST_ASSERT_TRUE(still_running);
}

void test_smooth_blend_multiple_colors() {
    // Set initial colors to red
    for (int i = 0; i < 10; i++) {
        mock_strip->setPixelColor(i, 0xFF0000);
    }

    // Create target colors (alternating blue and green)
    std::vector<::Strip::Color> targets;
    for (int i = 0; i < 10; i++) {
        targets.push_back(i % 2 == 0 ? 0x0000FF : 0x00FF00);
    }

    Support::SmoothBlend blend(*mock_strip, targets);

    // Initially should not be complete
    TEST_ASSERT_FALSE(blend.isComplete());

    // Do a few steps
    bool still_running = blend.step();
    TEST_ASSERT_TRUE(still_running);
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_smooth_blend_single_color);
    RUN_TEST(test_smooth_blend_multiple_colors);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
