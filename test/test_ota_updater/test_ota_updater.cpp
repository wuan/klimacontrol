#include "unity.h"
#include <cstdint>
#include <cstring>
#include <cstdio>

void setUp() {}
void tearDown() {}

static int compareVersions(const char* current, const char* available) {
    int cv[3] = {0, 0, 0};
    int av[3] = {0, 0, 0};

    if (sscanf(current, "v%d.%d.%d", &cv[0], &cv[1], &cv[2]) != 3) {
        return 0;
    }
    if (sscanf(available, "v%d.%d.%d", &av[0], &av[1], &av[2]) != 3) {
        return 0;
    }

    for (int i = 0; i < 3; i++) {
        if (av[i] > cv[i]) return 1;
        if (av[i] < cv[i]) return -1;
    }
    return 0;
}

static bool hasEnoughMemory(size_t freeHeap, size_t minRequired) {
    return freeHeap >= minRequired;
}

void test_version_compare_newer_available() {
    TEST_ASSERT_EQUAL(1, compareVersions("v1.0.0", "v1.2.3"));
}

void test_version_compare_current_newer() {
    TEST_ASSERT_EQUAL(-1, compareVersions("v2.0.0", "v1.9.9"));
}

void test_version_compare_equal() {
    TEST_ASSERT_EQUAL(0, compareVersions("v1.2.3", "v1.2.3"));
}

void test_version_compare_major_increment() {
    TEST_ASSERT_EQUAL(1, compareVersions("v1.0.0", "v2.0.0"));
}

void test_version_compare_minor_increment() {
    TEST_ASSERT_EQUAL(1, compareVersions("v1.0.0", "v1.1.0"));
}

void test_version_compare_patch_increment() {
    TEST_ASSERT_EQUAL(1, compareVersions("v1.0.0", "v1.0.1"));
}

void test_version_compare_prerelease_current() {
    TEST_ASSERT_EQUAL(0, compareVersions("v1.0.0-dev", "v1.0.0"));
}

void test_version_compare_invalid_format() {
    TEST_ASSERT_EQUAL(0, compareVersions("invalid", "v1.0.0"));
}

void test_has_enough_memory_exactly_equal() {
    TEST_ASSERT_TRUE(hasEnoughMemory(65536, 65536));
}

void test_has_enough_memory_above() {
    TEST_ASSERT_TRUE(hasEnoughMemory(100000, 65536));
}

void test_has_enough_memory_below() {
    TEST_ASSERT_FALSE(hasEnoughMemory(60000, 65536));
}

void test_has_enough_memory_zero_free() {
    TEST_ASSERT_FALSE(hasEnoughMemory(0, 65536));
}

void test_has_enough_memory_just_below() {
    TEST_ASSERT_FALSE(hasEnoughMemory(65535, 65536));
}

void test_has_enough_memory_just_above() {
    TEST_ASSERT_TRUE(hasEnoughMemory(65537, 65536));
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_version_compare_newer_available);
    RUN_TEST(test_version_compare_current_newer);
    RUN_TEST(test_version_compare_equal);
    RUN_TEST(test_version_compare_major_increment);
    RUN_TEST(test_version_compare_minor_increment);
    RUN_TEST(test_version_compare_patch_increment);
    RUN_TEST(test_version_compare_prerelease_current);
    RUN_TEST(test_version_compare_invalid_format);
    RUN_TEST(test_has_enough_memory_exactly_equal);
    RUN_TEST(test_has_enough_memory_above);
    RUN_TEST(test_has_enough_memory_below);
    RUN_TEST(test_has_enough_memory_zero_free);
    RUN_TEST(test_has_enough_memory_just_below);
    RUN_TEST(test_has_enough_memory_just_above);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}