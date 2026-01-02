#include "unity.h"
#include "color.h"
#include "show/ColorRanges.h"
#include <vector>

// Test the color parsing logic used by ShowFactory
// Note: ShowFactory itself has Arduino dependencies that don't compile in native environment
// This test verifies the core parsing logic that ensures exactly n colors are available

std::vector<Strip::Color> parseColors(const std::vector<Strip::Color>& inputColors,
                                       size_t count,
                                       Strip::Color defaultColor) {
    std::vector<Strip::Color> colors = inputColors;

    // If no colors provided, use default
    if (colors.empty()) {
        colors.push_back(defaultColor);
    }

    // Ensure exactly 'count' colors by repeating/truncating
    std::vector<Strip::Color> result;
    result.reserve(count);

    for (size_t i = 0; i < count; i++) {
        result.push_back(colors[i % colors.size()]);
    }

    return result;
}

// Test parseColors with empty input (should use default)
void test_parse_colors_empty() {
    std::vector<Strip::Color> input;
    auto result = parseColors(input, 1, color(255, 250, 230));
    TEST_ASSERT_EQUAL(1, result.size());
    TEST_ASSERT_EQUAL_HEX32(color(255, 250, 230), result[0]);
}

// Test parseColors with single color, need 1
void test_parse_colors_one_need_one() {
    std::vector<Strip::Color> input = {color(255, 0, 0)};
    auto result = parseColors(input, 1, color(255, 250, 230));
    TEST_ASSERT_EQUAL(1, result.size());
    TEST_ASSERT_EQUAL_HEX32(color(255, 0, 0), result[0]);
}

// Test parseColors with 1 color, need 2 (should cycle)
void test_parse_colors_one_need_two() {
    std::vector<Strip::Color> input = {color(255, 0, 0)};
    auto result = parseColors(input, 2, color(255, 250, 230));
    TEST_ASSERT_EQUAL(2, result.size());
    TEST_ASSERT_EQUAL_HEX32(color(255, 0, 0), result[0]);
    TEST_ASSERT_EQUAL_HEX32(color(255, 0, 0), result[1]);  // Cycled
}

// Test parseColors with 1 color, need 3 (should cycle)
void test_parse_colors_one_need_three() {
    std::vector<Strip::Color> input = {color(255, 0, 0)};
    auto result = parseColors(input, 3, color(255, 250, 230));
    TEST_ASSERT_EQUAL(3, result.size());
    TEST_ASSERT_EQUAL_HEX32(color(255, 0, 0), result[0]);
    TEST_ASSERT_EQUAL_HEX32(color(255, 0, 0), result[1]);  // Cycled
    TEST_ASSERT_EQUAL_HEX32(color(255, 0, 0), result[2]);  // Cycled
}

// Test parseColors with 2 colors, need 2
void test_parse_colors_two_need_two() {
    std::vector<Strip::Color> input = {color(255, 0, 0), color(0, 0, 255)};
    auto result = parseColors(input, 2, color(255, 250, 230));
    TEST_ASSERT_EQUAL(2, result.size());
    TEST_ASSERT_EQUAL_HEX32(color(255, 0, 0), result[0]);
    TEST_ASSERT_EQUAL_HEX32(color(0, 0, 255), result[1]);
}

// Test parseColors with 2 colors, need 3 (should cycle)
void test_parse_colors_two_need_three() {
    std::vector<Strip::Color> input = {color(255, 0, 0), color(0, 0, 255)};
    auto result = parseColors(input, 3, color(255, 250, 230));
    TEST_ASSERT_EQUAL(3, result.size());
    TEST_ASSERT_EQUAL_HEX32(color(255, 0, 0), result[0]);
    TEST_ASSERT_EQUAL_HEX32(color(0, 0, 255), result[1]);
    TEST_ASSERT_EQUAL_HEX32(color(255, 0, 0), result[2]);  // Cycled back to first
}

// Test parseColors with 2 colors, need 5 (should cycle multiple times)
void test_parse_colors_two_need_five() {
    std::vector<Strip::Color> input = {color(255, 0, 0), color(0, 0, 255)};
    auto result = parseColors(input, 5, color(255, 250, 230));
    TEST_ASSERT_EQUAL(5, result.size());
    TEST_ASSERT_EQUAL_HEX32(color(255, 0, 0), result[0]);
    TEST_ASSERT_EQUAL_HEX32(color(0, 0, 255), result[1]);
    TEST_ASSERT_EQUAL_HEX32(color(255, 0, 0), result[2]);  // Cycled
    TEST_ASSERT_EQUAL_HEX32(color(0, 0, 255), result[3]);  // Cycled
    TEST_ASSERT_EQUAL_HEX32(color(255, 0, 0), result[4]);  // Cycled
}

// Test parseColors with 3 colors, need 3
void test_parse_colors_three_need_three() {
    std::vector<Strip::Color> input = {
        color(255, 0, 0),
        color(0, 255, 0),
        color(0, 0, 255)
    };
    auto result = parseColors(input, 3, color(255, 250, 230));
    TEST_ASSERT_EQUAL(3, result.size());
    TEST_ASSERT_EQUAL_HEX32(color(255, 0, 0), result[0]);
    TEST_ASSERT_EQUAL_HEX32(color(0, 255, 0), result[1]);
    TEST_ASSERT_EQUAL_HEX32(color(0, 0, 255), result[2]);
}

// Test parseColors with 4 colors, need 2 (should truncate)
void test_parse_colors_four_need_two() {
    std::vector<Strip::Color> input = {
        color(255, 0, 0),
        color(0, 255, 0),
        color(0, 0, 255),
        color(255, 255, 0)
    };
    auto result = parseColors(input, 2, color(255, 250, 230));
    TEST_ASSERT_EQUAL(2, result.size());
    TEST_ASSERT_EQUAL_HEX32(color(255, 0, 0), result[0]);
    TEST_ASSERT_EQUAL_HEX32(color(0, 255, 0), result[1]);
}

// Test parseColors with 4 colors, need 4
void test_parse_colors_four_need_four() {
    std::vector<Strip::Color> input = {
        color(255, 0, 0),
        color(0, 255, 0),
        color(0, 0, 255),
        color(255, 255, 0)
    };
    auto result = parseColors(input, 4, color(255, 250, 230));
    TEST_ASSERT_EQUAL(4, result.size());
    TEST_ASSERT_EQUAL_HEX32(color(255, 0, 0), result[0]);
    TEST_ASSERT_EQUAL_HEX32(color(0, 255, 0), result[1]);
    TEST_ASSERT_EQUAL_HEX32(color(0, 0, 255), result[2]);
    TEST_ASSERT_EQUAL_HEX32(color(255, 255, 0), result[3]);
}

int runUnityTests() {
    UNITY_BEGIN();

    // Test parseColors logic (used by ShowFactory)
    RUN_TEST(test_parse_colors_empty);
    RUN_TEST(test_parse_colors_one_need_one);
    RUN_TEST(test_parse_colors_one_need_two);
    RUN_TEST(test_parse_colors_one_need_three);
    RUN_TEST(test_parse_colors_two_need_two);
    RUN_TEST(test_parse_colors_two_need_three);
    RUN_TEST(test_parse_colors_two_need_five);
    RUN_TEST(test_parse_colors_three_need_three);
    RUN_TEST(test_parse_colors_four_need_two);
    RUN_TEST(test_parse_colors_four_need_four);

    return UNITY_END();
}

int main() {
    return runUnityTests();
}
