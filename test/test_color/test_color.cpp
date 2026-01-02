#include <iostream>
#include <memory>
#include <ostream>

#include "unity.h"
#include "color.h"
#include "support/color.h"

void setUp() {
}

void tearDown() {
}

void test_color_extraction() {
    Strip::Color test_color = 0x123456;

    TEST_ASSERT_EQUAL_UINT8(0x12, red(test_color));
    TEST_ASSERT_EQUAL_UINT8(0x34, green(test_color));
    TEST_ASSERT_EQUAL_UINT8(0x56, blue(test_color));
}

void test_color_construction() {
    Strip::Color result = color(0x12, 0x34, 0x56);
    TEST_ASSERT_EQUAL_UINT32(0x123456, result);
}

void test_color_red_component() {
    Strip::Color pure_red = 0xFF0000;
    TEST_ASSERT_EQUAL_UINT8(0xFF, red(pure_red));
    TEST_ASSERT_EQUAL_UINT8(0x00, green(pure_red));
    TEST_ASSERT_EQUAL_UINT8(0x00, blue(pure_red));
}

void test_color_green_component() {
    Strip::Color pure_green = 0x00FF00;
    TEST_ASSERT_EQUAL_UINT8(0x00, red(pure_green));
    TEST_ASSERT_EQUAL_UINT8(0xFF, green(pure_green));
    TEST_ASSERT_EQUAL_UINT8(0x00, blue(pure_green));
}

void test_color_blue_component() {
    Strip::Color pure_blue = 0x0000FF;
    TEST_ASSERT_EQUAL_UINT8(0x00, red(pure_blue));
    TEST_ASSERT_EQUAL_UINT8(0x00, green(pure_blue));
    TEST_ASSERT_EQUAL_UINT8(0xFF, blue(pure_blue));
}

void test_black_body_color() {
    // temp=0.0: brightness=0, result is black
    TEST_ASSERT_EQUAL_UINT32(0x000000, Support::Color::black_body_color(0.0f));
    // temp=0.2: 1000K, brightness=0.667 → (169, 44, 0)
    TEST_ASSERT_EQUAL_UINT32(0xA92C00, Support::Color::black_body_color(0.20f));
    // temp=0.8: 1600K, green=114, brightness=1.0 → (255, 114, 0)
    TEST_ASSERT_EQUAL_UINT32(0xFF7200, Support::Color::black_body_color(0.80f));
    // temp=1.0: 1800K, green=126, brightness=1.0 → (255, 126, 0)
    TEST_ASSERT_EQUAL_UINT32(0xFF7E00, Support::Color::black_body_color(1.0f));
    // Values > 1.0 are clamped to 1.0
    TEST_ASSERT_EQUAL_UINT32(0xFF7E00, Support::Color::black_body_color(5.0f));
    TEST_ASSERT_EQUAL_UINT32(0xFF7E00, Support::Color::black_body_color(100.0f));
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_color_extraction);
    RUN_TEST(test_color_construction);
    RUN_TEST(test_color_red_component);
    RUN_TEST(test_color_green_component);
    RUN_TEST(test_color_blue_component);
    RUN_TEST(test_black_body_color);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
