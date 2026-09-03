#include "unity.h"
#include "support/ApPassword.h"

#include <cstring>

void setUp() {}
void tearDown() {}

// The exact outputs below are pinned regression catches: a future change
// to the FNV salt, mixing, or salt string will fail these tests. This
// is intentional — the password is derived from a six-hex device id that
// is constant for a given board, so two firmwares built with the same
// implementation MUST agree on the password for a given device id.

static const char *PIN_FOR_000000 = "eceec3eb";
static const char *PIN_FOR_AABBCC = "edc5507b";
static const char *PIN_FOR_EMPTY  = "215f7803";

// --- Output shape ---

void test_output_is_eight_lowercase_hex_chars() {
    char buf[9] = {0};
    Support::computeApPassword("AABBCC", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(8, static_cast<int>(std::strlen(buf)));
    for (int i = 0; i < 8; ++i) {
        const char c = buf[i];
        const bool isHexDigit =
            (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        TEST_ASSERT_TRUE_MESSAGE(isHexDigit,
            "output character must be lowercase hex digit");
    }
}

void test_output_is_nul_terminated_in_ninth_byte() {
    char buf[10] = {0};
    Support::computeApPassword("AABBCC", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_CHAR('\0', buf[8]);
}

void test_output_fits_in_eight_chars_plus_nul() {
    char buf[9] = {0};
    Support::computeApPassword("AABBCC", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(PIN_FOR_AABBCC, buf);
}

// --- Determinism: same input always produces same output ---

void test_same_input_same_output_first_call() {
    char buf[9] = {0};
    Support::computeApPassword("AABBCC", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(PIN_FOR_AABBCC, buf);
}

void test_same_input_same_output_second_call() {
    char buf1[9] = {0};
    char buf2[9] = {0};
    Support::computeApPassword("AABBCC", buf1, sizeof(buf1));
    Support::computeApPassword("AABBCC", buf2, sizeof(buf2));
    TEST_ASSERT_EQUAL_STRING(buf1, buf2);
}

void test_same_input_overwrites_previous_output() {
    char buf[9] = {0};
    Support::computeApPassword("AABBCC", buf, sizeof(buf));
    Support::computeApPassword("000000", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(PIN_FOR_000000, buf);
}

// --- Distinct ids produce distinct passwords ---

void test_two_distinct_ids_distinct_passwords() {
    char a[9] = {0};
    char b[9] = {0};
    Support::computeApPassword("000000", a, sizeof(a));
    Support::computeApPassword("FFFFFF", b, sizeof(b));
    TEST_ASSERT_NOT_EQUAL(0, std::strcmp(a, b));
}

void test_nearby_ids_distinct_passwords() {
    char a[9] = {0};
    char b[9] = {0};
    Support::computeApPassword("000000", a, sizeof(a));
    Support::computeApPassword("000001", b, sizeof(b));
    TEST_ASSERT_NOT_EQUAL(0, std::strcmp(a, b));
}

void test_many_distinct_ids_all_distinct() {
    // 16 hex ids spanning the byte range, all expected to be distinct.
    const char *ids[] = {
        "000000", "010203", "040506", "070809",
        "0A0B0C", "0D0E0F", "101112", "131415",
        "AABBCC", "DDEEFF", "DEADBE", "FACADE",
        "123456", "789ABC", "BEEF42", "CAFEFE"
    };
    char passwords[16][9] = {{0}};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        Support::computeApPassword(ids[i], passwords[i], 9);
    }
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        for (size_t j = i + 1; j < sizeof(ids) / sizeof(ids[0]); ++j) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(0, std::strcmp(passwords[i], passwords[j]),
                "two distinct ids produced the same password");
        }
    }
}

// --- Pinned regression values (catches salt / mixing changes) ---

void test_pinned_value_for_000000() {
    char buf[9] = {0};
    Support::computeApPassword("000000", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(PIN_FOR_000000, buf);
}

void test_pinned_value_for_AABBCC() {
    char buf[9] = {0};
    Support::computeApPassword("AABBCC", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(PIN_FOR_AABBCC, buf);
}

void test_pinned_value_for_empty() {
    char buf[9] = {0};
    Support::computeApPassword("", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(PIN_FOR_EMPTY, buf);
}

// --- Buffer-too-small is rejected, output is unmodified ---

void test_buffer_size_8_rejected() {
    char buf[8] = {'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X'};
    Support::computeApPassword("AABBCC", buf, sizeof(buf));
    // Output must be unmodified — no partial password, no terminator.
    for (size_t i = 0; i < sizeof(buf); ++i) {
        TEST_ASSERT_EQUAL_CHAR('X', buf[i]);
    }
}

void test_buffer_size_0_rejected() {
    char buf[1] = {'X'};
    Support::computeApPassword("AABBCC", buf, 0);
    TEST_ASSERT_EQUAL_CHAR('X', buf[0]);
}

void test_null_output_buffer_rejected() {
    // Must not crash. Pass a small buffer to a null pointer: the
    // function returns without writing.
    Support::computeApPassword("AABBCC", nullptr, 9);
    TEST_ASSERT_TRUE(true); // No crash is the assertion.
}

void test_null_device_id_treated_as_empty() {
    char buf[9] = {0};
    Support::computeApPassword(nullptr, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(PIN_FOR_EMPTY, buf);
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_output_is_eight_lowercase_hex_chars);
    RUN_TEST(test_output_is_nul_terminated_in_ninth_byte);
    RUN_TEST(test_output_fits_in_eight_chars_plus_nul);
    RUN_TEST(test_same_input_same_output_first_call);
    RUN_TEST(test_same_input_same_output_second_call);
    RUN_TEST(test_same_input_overwrites_previous_output);
    RUN_TEST(test_two_distinct_ids_distinct_passwords);
    RUN_TEST(test_nearby_ids_distinct_passwords);
    RUN_TEST(test_many_distinct_ids_all_distinct);
    RUN_TEST(test_pinned_value_for_000000);
    RUN_TEST(test_pinned_value_for_AABBCC);
    RUN_TEST(test_pinned_value_for_empty);
    RUN_TEST(test_buffer_size_8_rejected);
    RUN_TEST(test_buffer_size_0_rejected);
    RUN_TEST(test_null_output_buffer_rejected);
    RUN_TEST(test_null_device_id_treated_as_empty);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
