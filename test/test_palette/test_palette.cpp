#include "unity.h"
#include "support/Palette.h"
#include "color.h"

void setUp() {}
void tearDown() {}

void test_palette_empty() {
    Support::Palette palette;
    TEST_ASSERT_EQUAL_UINT32(0, palette.get_color(0.5f));
}

void test_palette_single_point() {
    Support::Palette palette;
    palette.addPoint(0.5f, 0xFF0000); // Red
    TEST_ASSERT_EQUAL_UINT32(0xFF0000, palette.get_color(0.0f));
    TEST_ASSERT_EQUAL_UINT32(0xFF0000, palette.get_color(0.5f));
    TEST_ASSERT_EQUAL_UINT32(0xFF0000, palette.get_color(1.0f));
}

void test_palette_linear_interpolation() {
    Support::Palette palette;
    palette.addPoint(0.0f, 0x000000); // Black
    palette.addPoint(1.0f, 0xFFFFFF); // White

    TEST_ASSERT_EQUAL_UINT32(0x000000, palette.get_color(0.0f));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFF, palette.get_color(1.0f));
    TEST_ASSERT_EQUAL_UINT32(0x7F7F7F, palette.get_color(0.5f));
}

void test_palette_step_interpolation() {
    Support::Palette palette;
    palette.addPoint(0.0f, 0xFF0000, Support::InterpolationType::Step); // Red
    palette.addPoint(0.5f, 0x00FF00, Support::InterpolationType::Step); // Green
    palette.addPoint(1.0f, 0x0000FF, Support::InterpolationType::Step); // Blue

    TEST_ASSERT_EQUAL_UINT32(0xFF0000, palette.get_color(0.25f));
    TEST_ASSERT_EQUAL_UINT32(0x00FF00, palette.get_color(0.5f));
    TEST_ASSERT_EQUAL_UINT32(0x00FF00, palette.get_color(0.75f));
    TEST_ASSERT_EQUAL_UINT32(0x0000FF, palette.get_color(1.0f));
}

void test_palette_multiple_points() {
    Support::Palette palette;
    palette.addPoint(0.0f, 0xFF0000); // Red
    palette.addPoint(0.5f, 0x00FF00); // Green
    palette.addPoint(1.0f, 0x0000FF); // Blue

    TEST_ASSERT_EQUAL_UINT32(0xFF0000, palette.get_color(0.0f));
    TEST_ASSERT_EQUAL_UINT32(0x7F7F00, palette.get_color(0.25f));
    TEST_ASSERT_EQUAL_UINT32(0x00FF00, palette.get_color(0.5f));
    TEST_ASSERT_EQUAL_UINT32(0x007F7F, palette.get_color(0.75f));
    TEST_ASSERT_EQUAL_UINT32(0x0000FF, palette.get_color(1.0f));
}

void test_palette_unsorted_points() {
    Support::Palette palette;
    palette.addPoint(1.0f, 0x0000FF); // Blue
    palette.addPoint(0.0f, 0xFF0000); // Red
    palette.addPoint(0.5f, 0x00FF00); // Green

    TEST_ASSERT_EQUAL_UINT32(0xFF0000, palette.get_color(0.0f));
    TEST_ASSERT_EQUAL_UINT32(0x00FF00, palette.get_color(0.5f));
    TEST_ASSERT_EQUAL_UINT32(0x0000FF, palette.get_color(1.0f));
}

void test_palette_power_interpolation() {
    Support::Palette palette;
    palette.addPoint(0.0f, 0x000000, Support::InterpolationType::Power, 2.0f); // Power of 2
    palette.addPoint(1.0f, 0xFFFFFF);

    // at 0.5, progress should be 0.5^2 = 0.25
    // 0x000000 * 0.75 + 0xFFFFFF * 0.25 = 0x3F3F3F
    TEST_ASSERT_EQUAL_UINT32(0x3F3F3F, palette.get_color(0.5f));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_palette_empty);
    RUN_TEST(test_palette_single_point);
    RUN_TEST(test_palette_linear_interpolation);
    RUN_TEST(test_palette_step_interpolation);
    RUN_TEST(test_palette_multiple_points);
    RUN_TEST(test_palette_unsorted_points);
    RUN_TEST(test_palette_power_interpolation);
    return UNITY_END();
}
