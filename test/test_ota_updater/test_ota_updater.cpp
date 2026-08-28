// Tests for the OTA version-ordering logic.
//
// These exercise the *production* implementation in src/support/VersionCompare.h.
// The previous version of this file carried private copies of compareVersions()
// and hasEnoughMemory(), so it tested nothing that shipped — and it was not
// listed in platformio.ini's build_src_filter, so it never even ran. Both are
// fixed: the comparison lives in a header with no Arduino dependency precisely
// so the native test can link against the real code.

#include "unity.h"
#include "support/VersionCompare.h"

using Support::compareVersions;
using Support::isNewerVersion;

void setUp() {}
void tearDown() {}

// --- ordering ---

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

// A minor bump must outrank a larger patch number on the older minor.
void test_version_compare_minor_outranks_patch() {
    TEST_ASSERT_EQUAL(1, compareVersions("v1.0.9", "v1.1.0"));
    TEST_ASSERT_EQUAL(-1, compareVersions("v1.1.0", "v1.0.9"));
}

void test_version_compare_prerelease_current() {
    TEST_ASSERT_EQUAL(0, compareVersions("v1.0.0-dev", "v1.0.0"));
}

// --- unparseable input is never treated as an upgrade ---

void test_version_compare_invalid_format() {
    TEST_ASSERT_EQUAL(0, compareVersions("invalid", "v1.0.0"));
}

void test_version_compare_invalid_available() {
    TEST_ASSERT_EQUAL(0, compareVersions("v1.0.0", "not-a-version"));
}

void test_version_compare_null_safe() {
    TEST_ASSERT_EQUAL(0, compareVersions(nullptr, "v1.0.0"));
    TEST_ASSERT_EQUAL(0, compareVersions("v1.0.0", nullptr));
}

// --- isNewerVersion: the predicate that actually gates flashing ---

void test_is_newer_true_only_when_greater() {
    TEST_ASSERT_TRUE(isNewerVersion("v1.0.0", "v1.0.1"));
    TEST_ASSERT_FALSE(isNewerVersion("v1.0.1", "v1.0.1"));
    TEST_ASSERT_FALSE(isNewerVersion("v1.0.2", "v1.0.1"));
}

// The regression this ordering exists for: an untagged developer build reports
// `git describe` output, which differs textually from the release tag it is
// based on. Comparing with != offered that release as an "update", i.e. a
// downgrade dressed up as an upgrade. A genuinely newer release must still win.
void test_untagged_dev_build_is_not_offered_a_downgrade() {
    TEST_ASSERT_FALSE(isNewerVersion("v1.2.3-4-gabc1234", "v1.2.3"));
    TEST_ASSERT_TRUE(isNewerVersion("v1.2.3-4-gabc1234", "v1.3.0"));
}

// The compiled-in fallback when there is no git tag at all must still accept
// any real release.
void test_default_dev_version_accepts_any_release() {
    TEST_ASSERT_TRUE(isNewerVersion("v0.0.0-dev", "v0.0.1"));
    TEST_ASSERT_TRUE(isNewerVersion("v0.0.0-dev", "v1.0.0"));
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_version_compare_newer_available);
    RUN_TEST(test_version_compare_current_newer);
    RUN_TEST(test_version_compare_equal);
    RUN_TEST(test_version_compare_major_increment);
    RUN_TEST(test_version_compare_minor_increment);
    RUN_TEST(test_version_compare_patch_increment);
    RUN_TEST(test_version_compare_minor_outranks_patch);
    RUN_TEST(test_version_compare_prerelease_current);
    RUN_TEST(test_version_compare_invalid_format);
    RUN_TEST(test_version_compare_invalid_available);
    RUN_TEST(test_version_compare_null_safe);
    RUN_TEST(test_is_newer_true_only_when_greater);
    RUN_TEST(test_untagged_dev_build_is_not_offered_a_downgrade);
    RUN_TEST(test_default_dev_version_accepts_any_release);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
