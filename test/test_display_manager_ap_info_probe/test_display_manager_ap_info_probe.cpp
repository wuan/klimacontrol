#include "unity.h"

#include "Config.h"
#include "support/ApPassword.h"

// Smoke tests for the boundary the deferred-display-probe path
// (`probe-display-at-ap-entry`) relies on in native builds.
//
// `DisplayManager::tryBeginForApInfo()` itself is `#ifdef ARDUINO` and
// therefore not reachable from this test binary. What IS reachable, and
// what the AP-mode entry path pins, is the *contract* between
// `Network::startAP()` and the function it calls:
//
//   - `Network::startAP()` constructs a default `Config::DisplayConfig`
//     and hands it to `tryBeginForApInfo()` for the probe. If the
//     default-constructed config stops describing "off, rotation 0",
//     the cold-boot probe path sees a non-zero rotation and the panel
//     comes up mirrored or sideways on a fresh device.
//
//   - The password written by `Support::computeApPassword` is 8 hex
//     chars + NUL (9 bytes), and the buffer in `Network` is sized to
//     hold exactly that. A regression in either side breaks WPA2-PSK.
//
// These tests pin both halves. The hardware path (panel responds or
// doesn't) is verified on device by the boot log.

void setUp() {}
void tearDown() {}

// --- DisplayConfig defaults the AP-mode probe path relies on ---

void test_default_display_config_is_off() {
    Config::DisplayConfig config;
    // Cold-boot probe path: DisplayConfig.enabled must default to false
    // so the deferred-probe branch (rather than the normal-operation
    // short-circuit) actually runs.
    TEST_ASSERT_FALSE(config.enabled);
}

void test_default_display_config_rotation_is_zero() {
    Config::DisplayConfig config;
    // The default rotation is what `Network::startAP()` hands to
    // `tryBeginForApInfo()` on the cold-boot path. Pinning it to 0
    // catches a regression where someone changes the default and the
    // panel mounts mirrored on a fresh device.
    TEST_ASSERT_EQUAL_UINT8(0, config.rotation);
}

void test_default_display_config_interval_is_default() {
    Config::DisplayConfig config;
    // The interval doesn't reach the panel on the AP-mode probe branch
    // (no RefreshPolicy is constructed), but a regression guard on the
    // full structure keeps future readers honest.
    TEST_ASSERT_EQUAL_UINT16(Config::DEFAULT_DISPLAY_INTERVAL, config.interval);
}

// --- AP password buffer sizing ---

void test_compute_ap_password_fits_eight_hex_chars() {
    // Sanity: the password function still produces exactly 8 hex chars
    // (already covered in test_ap_password, repeated here as a guard
    // that the contract crossing this change's boundary holds).
    char buf[9] = {0};
    Support::computeApPassword("AABBCC", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(8, static_cast<int>(strlen(buf)));
    for (int i = 0; i < 8; ++i) {
        const char c = buf[i];
        const bool isHex =
            (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        TEST_ASSERT_TRUE_MESSAGE(isHex, "password must be lowercase hex");
    }
}

// --- Config constants used by the AP-mode probe path ---

void test_default_display_interval_is_sensible() {
    // The default interval sits inside the validator's accepted range;
    // pin both ends so a regression that pulls the default out of range
    // is caught even if the validator is later relaxed.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(Config::MIN_DISPLAY_INTERVAL,
                                        Config::DEFAULT_DISPLAY_INTERVAL);
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(Config::MAX_DISPLAY_INTERVAL,
                                     Config::DEFAULT_DISPLAY_INTERVAL);
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_default_display_config_is_off);
    RUN_TEST(test_default_display_config_rotation_is_zero);
    RUN_TEST(test_default_display_config_interval_is_default);
    RUN_TEST(test_compute_ap_password_fits_eight_hex_chars);
    RUN_TEST(test_default_display_interval_is_sensible);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}