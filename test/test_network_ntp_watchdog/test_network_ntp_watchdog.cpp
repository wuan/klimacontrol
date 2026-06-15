#include <atomic>
#include <functional>
#include <stdexcept>

#include "support/NetworkWatchdog.h"
#include "unity.h"

void setUp() {
    // No-op: each test installs and resets its own hook.
}
void tearDown() {
    // Always reset to the default between tests so a hook installed by one
    // test does not leak into the next.
    Support::resetWdtResetHook();
}

// 3.3 — hook fires exactly twice (before + after) on the happy path.
void test_guarded_call_fires_hook_twice_on_success() {
    std::atomic<int> count{0};
    Support::setWdtResetHook([&count]() { count.fetch_add(1); });

    bool result = Support::guardedCall([]() { return true; });

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_INT(2, count.load());
}

// 3.4a — guardedCall forwards the callable's return value (true path).
void test_guarded_call_returns_true() {
    Support::setWdtResetHook([]() {});
    bool result = Support::guardedCall([]() { return true; });
    TEST_ASSERT_TRUE(result);
}

// 3.4b — guardedCall forwards the callable's return value (false path).
void test_guarded_call_returns_false() {
    Support::setWdtResetHook([]() {});
    bool result = Support::guardedCall([]() { return false; });
    TEST_ASSERT_FALSE(result);
}

// 3.5a — the hook is still called after a callable that returns false
// (i.e. the after-hook is not skipped on the failure path).
void test_guarded_call_fires_after_hook_on_false_result() {
    std::atomic<int> count{0};
    Support::setWdtResetHook([&count]() { count.fetch_add(1); });

    bool result = Support::guardedCall([]() { return false; });

    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL_INT(2, count.load());
}

// 3.5b — the after-hook is fired even when the callable throws, and the
// exception propagates out.
void test_guarded_call_fires_after_hook_on_throw() {
    std::atomic<int> count{0};
    Support::setWdtResetHook([&count]() { count.fetch_add(1); });

    bool threw = false;
    try {
        Support::guardedCall([]() -> bool { throw std::runtime_error("boom"); });
    } catch (const std::runtime_error&) {
        threw = true;
    }

    TEST_ASSERT_TRUE(threw);
    TEST_ASSERT_EQUAL_INT(2, count.load());
}

// feedWdt() invokes the currently installed hook once.
void test_feed_wdt_invokes_hook() {
    std::atomic<int> count{0};
    Support::setWdtResetHook([&count]() { count.fetch_add(1); });

    Support::feedWdt();
    Support::feedWdt();
    Support::feedWdt();

    TEST_ASSERT_EQUAL_INT(3, count.load());
}

// resetWdtResetHook() restores the default (no-op on native). After reset,
// guardedCall still works (it falls back to the default) but the counting
// hook from the previous test is no longer invoked.
void test_reset_hook_clears_custom_hook() {
    std::atomic<int> count{0};
    Support::setWdtResetHook([&count]() { count.fetch_add(1); });
    Support::resetWdtResetHook();

    bool result = Support::guardedCall([]() { return true; });

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_INT(0, count.load());
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_guarded_call_fires_hook_twice_on_success);
    RUN_TEST(test_guarded_call_returns_true);
    RUN_TEST(test_guarded_call_returns_false);
    RUN_TEST(test_guarded_call_fires_after_hook_on_false_result);
    RUN_TEST(test_guarded_call_fires_after_hook_on_throw);
    RUN_TEST(test_feed_wdt_invokes_hook);
    RUN_TEST(test_reset_hook_clears_custom_hook);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
