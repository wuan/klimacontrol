// Tests for the deterministic predicates that gate the OTA transport's
// observable behavior. Each predicate here is named by a requirement in
// openspec/specs/ota-updates/spec.md; the change that added this file
// (openspec/changes/ota-transport-hardening/) is what makes them reachable
// from a native test in the first place. The HTTP state machine itself is
// still guarded by the existing on-device integration tests.

#include "unity.h"
#include "ota/OTAConfig.h"
#include "ota/http/RedirectScheme.h"
#include "ota/VersionCompare.h"
#include <cstring>
#include <string>

using OTA::Http::isSecureRedirectPrefix;
using Support::compareVersions;
using Support::isExpectedFirmwareAsset;
using Support::isNewerVersion;

void setUp() {}
void tearDown() {}

// --- Version comparison (see src/support/VersionCompare.h) ------------------

void test_version_newer_patch_is_offered() {
    TEST_ASSERT_TRUE(isNewerVersion("v0.0.73", "v0.0.74"));
}

void test_version_older_is_refused() {
    TEST_ASSERT_FALSE(isNewerVersion("v0.0.74", "v0.0.73"));
}

void test_version_equal_is_not_offered() {
    // The OTA path must never re-flash the running version. isNewerVersion
    // returns false on equality, so a release tagged identically to the
    // running firmware is silently ignored.
    TEST_ASSERT_FALSE(isNewerVersion("v0.0.74", "v0.0.74"));
}

void test_version_untagged_dev_build_is_not_offered_its_base_tag() {
    // scripts/get_version.py falls back to `git describe --tags --always`,
    // so a build four commits past v1.2.3 reports "v1.2.3-4-gabc1234".
    // Compared numerically that is *equal* to v1.2.3 (the suffix is
    // ignored), so the device must not offer itself the v1.2.3 release as
    // an "update" — that would in fact be a downgrade.
    TEST_ASSERT_FALSE(isNewerVersion("v1.2.3-4-gabc1234", "v1.2.3"));
}

void test_version_null_current_refuses_update() {
    // Caller-side bug guard: if FIRMWARE_VERSION is somehow not injected,
    // compareVersions must not pretend the available release is newer.
    TEST_ASSERT_EQUAL(0, compareVersions(nullptr, "v0.0.74"));
    TEST_ASSERT_FALSE(isNewerVersion(nullptr, "v0.0.74"));
}

void test_version_unparseable_tag_returns_zero() {
    // A non-`v%d.%d.%d` tag must not be treated as an upgrade: the caller
    // would refuse to flash something it cannot reason about.
    TEST_ASSERT_EQUAL(0, compareVersions("v1.0.0", "not-a-version"));
    TEST_ASSERT_EQUAL(0, compareVersions("garbage", "v1.0.0"));
    TEST_ASSERT_FALSE(isNewerVersion("v1.0.0", "not-a-version"));
}

// --- Redirect-scheme classification (see src/support/RedirectScheme.h) -------

void test_redirect_absolute_https_is_accepted() {
    TEST_ASSERT_TRUE(isSecureRedirectPrefix("https://release-assets.githubusercontent.com/whatever"));
}

void test_redirect_absolute_http_is_refused() {
    // A downgrade to cleartext on a redirect would silently lose both
    // confidentiality and the CA-bundle check on the hop that actually
    // carries the firmware image.
    TEST_ASSERT_FALSE(isSecureRedirectPrefix("http://example.com/firmware.bin"));
}

void test_redirect_relative_path_is_accepted() {
    // A relative Location inherits the current request's scheme, which is
    // already HTTPS, so it is safe to follow.
    TEST_ASSERT_TRUE(isSecureRedirectPrefix("/path/to/firmware.bin"));
}

void test_redirect_null_is_refused() {
    // No header captured yet — the safe default is to refuse.
    TEST_ASSERT_FALSE(isSecureRedirectPrefix(nullptr));
}

void test_redirect_empty_string_is_refused() {
    // The caller resets redirectLocation[0] to '\0' at the top of each
    // iteration; the classifier must not treat the empty buffer as a
    // relative URL.
    TEST_ASSERT_FALSE(isSecureRedirectPrefix(""));
}

void test_redirect_truncated_absolute_https_still_accepted() {
    // The capture buffer is 32 bytes; strlcpy null-terminates at the cap,
    // so an absolute URL longer than 31 bytes is truncated. The classifier
    // must still recognise the https:// prefix in the truncated prefix.
    char truncated[32] = {};
    const char* longHttps = "https://github.com/wuan/klimacontrol/releases/download/v0.0.74/firmware.bin";
    strncpy(truncated, longHttps, sizeof(truncated) - 1);
    truncated[sizeof(truncated) - 1] = '\0';
    TEST_ASSERT_EQUAL(31, strlen(truncated));
    TEST_ASSERT_TRUE(isSecureRedirectPrefix(truncated));
}

void test_redirect_truncated_absolute_http_still_refused() {
    // Same truncation behaviour must still classify a cleartext target
    // as refused — the prefix check is the whole point.
    char truncated[32] = {};
    const char* longHttp = "http://malicious.example.com/very/long/path/to/firmware.bin";
    strncpy(truncated, longHttp, sizeof(truncated) - 1);
    truncated[sizeof(truncated) - 1] = '\0';
    TEST_ASSERT_EQUAL(31, strlen(truncated));
    TEST_ASSERT_FALSE(isSecureRedirectPrefix(truncated));
}

// --- GitHub API URL composition --------------------------------------------

void test_github_api_url_is_composed_from_constants() {
    // Mirrors the production composition in checkForUpdate(): the constant
    // OTA_GITHUB_API_HOST plus the literal path segments. If the constant
    // ever drifts from api.github.com, this test breaks before the device
    // starts failing to find releases.
    const std::string owner = "wuan";
    const std::string repo = "klimacontrol";
    const std::string url = std::string(OTA_GITHUB_API_HOST) + "repos/" + owner + "/" + repo + "/releases/latest";
    TEST_ASSERT_EQUAL_STRING("https://api.github.com/repos/wuan/klimacontrol/releases/latest", url.c_str());
}

// --- GitHub release-host allowlist (first-hop prefix check) -----------------

void test_host_allowlist_accepts_github_release_url() {
    TEST_ASSERT_EQUAL_STRING("https://github.com/", OTA_GITHUB_RELEASE_HOST);
    // info.downloadUrl.startsWith(OTA_GITHUB_RELEASE_HOST) in production.
    const std::string downloadUrl = "https://github.com/wuan/klimacontrol/releases/download/v0.0.74/firmware.bin";
    TEST_ASSERT_EQUAL_INT(0, downloadUrl.compare(0, strlen(OTA_GITHUB_RELEASE_HOST), OTA_GITHUB_RELEASE_HOST));
}

void test_host_allowlist_rejects_non_github_host() {
    // Mirrors production: !info.downloadUrl.startsWith(OTA_GITHUB_RELEASE_HOST)
    // refuses the update. The prefix check on this URL must return false.
    const std::string downloadUrl = "https://example.com/firmware.bin";
    TEST_ASSERT_NOT_EQUAL(0, downloadUrl.compare(0, strlen(OTA_GITHUB_RELEASE_HOST), OTA_GITHUB_RELEASE_HOST));
}

// --- Asset-name strict match (see Support::isExpectedFirmwareAsset) ---------

void test_asset_name_firmware_bin_is_accepted() {
    TEST_ASSERT_TRUE(isExpectedFirmwareAsset(OTA_FIRMWARE_ASSET));
    TEST_ASSERT_TRUE(isExpectedFirmwareAsset("firmware.bin"));
}

void test_asset_name_similar_names_are_rejected() {
    // Each of these is a plausible misclassification: filesystem image,
    // bootloader blob, board-specific firmware, hash sidecar. Any one of
    // them would have caused the wrong image to be flashed — see the
    // comment on OTA_FIRMWARE_ASSET in OTAConfig.h.
    TEST_ASSERT_FALSE(isExpectedFirmwareAsset("littlefs.bin"));
    TEST_ASSERT_FALSE(isExpectedFirmwareAsset("bootloader.bin"));
    TEST_ASSERT_FALSE(isExpectedFirmwareAsset("ota-firmware.bin"));
    TEST_ASSERT_FALSE(isExpectedFirmwareAsset("firmware.bin.sha256"));
}

void test_asset_name_null_is_rejected() {
    TEST_ASSERT_FALSE(isExpectedFirmwareAsset(nullptr));
}

int runUnityTests() {
    UNITY_BEGIN();
    // version compare
    RUN_TEST(test_version_newer_patch_is_offered);
    RUN_TEST(test_version_older_is_refused);
    RUN_TEST(test_version_equal_is_not_offered);
    RUN_TEST(test_version_untagged_dev_build_is_not_offered_its_base_tag);
    RUN_TEST(test_version_null_current_refuses_update);
    RUN_TEST(test_version_unparseable_tag_returns_zero);
    // redirect-scheme classification
    RUN_TEST(test_redirect_absolute_https_is_accepted);
    RUN_TEST(test_redirect_absolute_http_is_refused);
    RUN_TEST(test_redirect_relative_path_is_accepted);
    RUN_TEST(test_redirect_null_is_refused);
    RUN_TEST(test_redirect_empty_string_is_refused);
    RUN_TEST(test_redirect_truncated_absolute_https_still_accepted);
    RUN_TEST(test_redirect_truncated_absolute_http_still_refused);
    // GitHub URL composition
    RUN_TEST(test_github_api_url_is_composed_from_constants);
    // host allowlist
    RUN_TEST(test_host_allowlist_accepts_github_release_url);
    RUN_TEST(test_host_allowlist_rejects_non_github_host);
    // asset-name strict match
    RUN_TEST(test_asset_name_firmware_bin_is_accepted);
    RUN_TEST(test_asset_name_similar_names_are_rejected);
    RUN_TEST(test_asset_name_null_is_rejected);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
