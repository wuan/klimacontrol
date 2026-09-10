#include "network/LinkMonitor.h"
#include "unity.h"

using Net::LinkMonitor;

void setUp() {}
void tearDown() {}

namespace {
    constexpr uint32_t T0 = 100000;

    void superviseAt(LinkMonitor &m, uint32_t t) {
        m.markConnected(t);
        m.beginSupervision(t);
    }
}

void test_steady_connected_link_reports_nothing() {
    LinkMonitor m;
    superviseAt(m, T0);
    for (uint32_t i = 1; i <= 1000; i++) {
        const auto v = m.poll(true, T0 + i * 1000);
        TEST_ASSERT_FALSE(v.reconnected);
        TEST_ASSERT_FALSE(v.dropped);
        TEST_ASSERT_FALSE(v.forceReconnect);
        TEST_ASSERT_EQUAL(static_cast<int>(LinkMonitor::Restart::None), static_cast<int>(v.restart));
    }
}

void test_drop_and_recovery_transitions() {
    LinkMonitor m;
    superviseAt(m, T0);
    auto v = m.poll(false, T0 + 1000);
    TEST_ASSERT_TRUE(v.dropped);
    TEST_ASSERT_FALSE(v.reconnected);
    v = m.poll(false, T0 + 2000);
    TEST_ASSERT_FALSE(v.dropped);
    v = m.poll(true, T0 + 3000);
    TEST_ASSERT_TRUE(v.reconnected);
    TEST_ASSERT_FALSE(v.dropped);
}

void test_disconnect_event_stamps_reason() {
    LinkMonitor m;
    superviseAt(m, T0);
    m.onStaDisconnected(T0 + 500, 200);
    TEST_ASSERT_EQUAL_UINT8(200, m.disconnectReason());
}

void test_active_reconnect_after_30s_down_using_event_stamp() {
    LinkMonitor m;
    superviseAt(m, T0);
    m.onStaDisconnected(T0 + 500, 8);
    // Just under the threshold: nothing yet.
    auto v = m.poll(false, T0 + 500 + LinkMonitor::ACTIVE_RECONNECT_AFTER_MS - 1);
    TEST_ASSERT_FALSE(v.forceReconnect);
    // At the threshold: first forced reconnect.
    v = m.poll(false, T0 + 500 + LinkMonitor::ACTIVE_RECONNECT_AFTER_MS);
    TEST_ASSERT_TRUE(v.forceReconnect);
    TEST_ASSERT_EQUAL_UINT8(1, v.reconnectAttempt);
    TEST_ASSERT_EQUAL_UINT32(LinkMonitor::ACTIVE_RECONNECT_AFTER_MS, v.downForMs);
}

void test_active_reconnect_falls_back_to_poll_stamp_without_event() {
    // Field failure mode: no DISCONNECTED event is ever delivered.
    LinkMonitor m;
    superviseAt(m, T0);
    const uint32_t dropSeen = T0 + 1000;
    auto v = m.poll(false, dropSeen);
    TEST_ASSERT_TRUE(v.dropped);
    v = m.poll(false, dropSeen + LinkMonitor::ACTIVE_RECONNECT_AFTER_MS);
    TEST_ASSERT_TRUE(v.forceReconnect);
}

void test_active_reconnect_rate_limited_and_exhausts_into_restart() {
    LinkMonitor m;
    superviseAt(m, T0);
    m.onStaDisconnected(T0, 8);
    uint32_t t = T0 + LinkMonitor::ACTIVE_RECONNECT_AFTER_MS;
    uint8_t attempts = 0;
    LinkMonitor::Restart restart = LinkMonitor::Restart::None;
    // Poll every second for long enough to exhaust the attempts.
    for (uint32_t i = 0; i < 400 && restart == LinkMonitor::Restart::None; i++, t += 1000) {
        const auto v = m.poll(false, t);
        if (v.forceReconnect) {
            attempts++;
            TEST_ASSERT_EQUAL_UINT8(attempts, v.reconnectAttempt);
        }
        restart = v.restart;
    }
    TEST_ASSERT_EQUAL_UINT8(LinkMonitor::MAX_ACTIVE_RECONNECT_FAILURES, attempts);
    TEST_ASSERT_EQUAL(static_cast<int>(LinkMonitor::Restart::ReconnectExhausted), static_cast<int>(restart));
    // Attempts are spaced by the minimum interval: 6 attempts span 5 intervals.
    const uint32_t expectedEnd = T0 + LinkMonitor::ACTIVE_RECONNECT_AFTER_MS
                                 + 5 * LinkMonitor::ACTIVE_RECONNECT_MIN_INTERVAL_MS;
    TEST_ASSERT_EQUAL_UINT32(expectedEnd + 1000, t);
}

void test_reconnect_resets_attempt_counter() {
    LinkMonitor m;
    superviseAt(m, T0);
    m.onStaDisconnected(T0, 8);
    auto v = m.poll(false, T0 + LinkMonitor::ACTIVE_RECONNECT_AFTER_MS);
    TEST_ASSERT_TRUE(v.forceReconnect);
    TEST_ASSERT_EQUAL_UINT8(1, m.activeReconnectFailures());
    v = m.poll(true, T0 + LinkMonitor::ACTIVE_RECONNECT_AFTER_MS + 1000);
    TEST_ASSERT_TRUE(v.reconnected);
    TEST_ASSERT_EQUAL_UINT8(0, m.activeReconnectFailures());
}

void test_flapping_link_triggers_no_stable_restart() {
    // Up 30 s, down 1 s, forever: never a 60 s stable streak, and the
    // attempt counter is reset on every brief reconnect, so only the
    // backstop can catch this.
    LinkMonitor m;
    superviseAt(m, T0);
    uint32_t t = T0;
    LinkMonitor::Restart restart = LinkMonitor::Restart::None;
    uint32_t forced = 0;
    for (uint32_t i = 0; i < 2000 && restart == LinkMonitor::Restart::None; i++) {
        t += 1000;
        const bool up = (i % 31) != 30;
        const auto v = m.poll(up, t);
        if (v.forceReconnect) forced++;
        restart = v.restart;
    }
    TEST_ASSERT_EQUAL(static_cast<int>(LinkMonitor::Restart::NoStableLink), static_cast<int>(restart));
    TEST_ASSERT_EQUAL_UINT32(0, forced);
    TEST_ASSERT_UINT32_WITHIN(2000, T0 + LinkMonitor::FORCE_RESTART_NO_STABLE_MS, t);
}

void test_stable_link_advances_backstop_baseline() {
    LinkMonitor m;
    superviseAt(m, T0);
    uint32_t t = T0;
    // 9 minutes solid, then a 1 s flicker, then 9 more minutes solid: the
    // backstop must not fire, because the baseline advanced during the
    // first stable stretch.
    for (uint32_t i = 0; i < 540; i++) { t += 1000; TEST_ASSERT_EQUAL(0, static_cast<int>(m.poll(true, t).restart)); }
    t += 1000; TEST_ASSERT_EQUAL(0, static_cast<int>(m.poll(false, t).restart));
    for (uint32_t i = 0; i < 540; i++) { t += 1000; TEST_ASSERT_EQUAL(0, static_cast<int>(m.poll(true, t).restart)); }
}

void test_mark_connected_only_seeds_when_no_event_arrived() {
    LinkMonitor m;
    m.onStaConnected(5);
    m.markConnected(9);
    // Cannot observe lastConnectMs directly; verify via behaviour: a drop
    // at t=10 without an event still gets the poll stamp and the reconnect
    // timing is unaffected by the seed.
    m.beginSupervision(9);
    auto v = m.poll(false, 10);
    TEST_ASSERT_TRUE(v.dropped);
    v = m.poll(false, 10 + LinkMonitor::ACTIVE_RECONNECT_AFTER_MS);
    TEST_ASSERT_TRUE(v.forceReconnect);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_steady_connected_link_reports_nothing);
    RUN_TEST(test_drop_and_recovery_transitions);
    RUN_TEST(test_disconnect_event_stamps_reason);
    RUN_TEST(test_active_reconnect_after_30s_down_using_event_stamp);
    RUN_TEST(test_active_reconnect_falls_back_to_poll_stamp_without_event);
    RUN_TEST(test_active_reconnect_rate_limited_and_exhausts_into_restart);
    RUN_TEST(test_reconnect_resets_attempt_counter);
    RUN_TEST(test_flapping_link_triggers_no_stable_restart);
    RUN_TEST(test_stable_link_advances_backstop_baseline);
    RUN_TEST(test_mark_connected_only_seeds_when_no_event_arrived);
    return UNITY_END();
}
