#include "unity.h"

#include <cstring>

#include "actuator/HeatingActuator.h"

using Actuator::Agreement;
using Actuator::HeatingActuator;
using Actuator::ReportedState;
using Actuator::reportedState;

void setUp() {}
void tearDown() {}

// What a display is allowed to claim. The rule that matters: once a channel is
// assigned, the device must not report "heating" on the strength of its own
// intent — only on an observation. A confident green dot during a manifold
// outage is the display lying about the one thing anyone looks at it for.

void test_disabled_beats_everything() {
    TEST_ASSERT_EQUAL(static_cast<int>(ReportedState::Disabled),
                      static_cast<int>(reportedState(false, true, Agreement::HeatingOk, 1.0f)));
    TEST_ASSERT_EQUAL(static_cast<int>(ReportedState::Disabled),
                      static_cast<int>(reportedState(false, false, Agreement::Unknown, 1.0f)));
}

void test_unassigned_falls_back_to_demand() {
    // No actuator exists to confirm anything, and the demand is then the only
    // observable fact — so the indicator stays useful on an un-migrated device.
    TEST_ASSERT_EQUAL(static_cast<int>(ReportedState::Heating),
                      static_cast<int>(reportedState(true, false, Agreement::Unknown, 0.4f)));
    TEST_ASSERT_EQUAL(static_cast<int>(ReportedState::Idle),
                      static_cast<int>(reportedState(true, false, Agreement::Unknown, 0.0f)));
}

void test_assigned_ignores_demand_and_trusts_observation() {
    // Full demand but the relay says closed: the honest answer is Idle, not
    // Heating. This is the case the whole requirement exists for.
    TEST_ASSERT_EQUAL(static_cast<int>(ReportedState::Idle),
                      static_cast<int>(reportedState(true, true, Agreement::ClosedOk, 1.0f)));
    TEST_ASSERT_EQUAL(static_cast<int>(ReportedState::Heating),
                      static_cast<int>(reportedState(true, true, Agreement::HeatingOk, 0.0f)));
}

void test_unobserved_is_unknown_not_a_guess() {
    TEST_ASSERT_EQUAL(static_cast<int>(ReportedState::Unknown),
                      static_cast<int>(reportedState(true, true, Agreement::Unknown, 1.0f)));
}

void test_disagreement_is_a_fault() {
    // Contact closed but no current: a dead or disconnected wax head.
    TEST_ASSERT_EQUAL(static_cast<int>(ReportedState::Fault),
                      static_cast<int>(reportedState(true, true, Agreement::NoActuator, 1.0f)));
    // Commanded on, relay says open.
    TEST_ASSERT_EQUAL(static_cast<int>(ReportedState::Fault),
                      static_cast<int>(reportedState(true, true, Agreement::RelayRefused, 1.0f)));
}

void test_every_state_has_a_name() {
    const ReportedState all[] = {ReportedState::Disabled, ReportedState::Idle,
                                 ReportedState::Heating, ReportedState::Unknown,
                                 ReportedState::Fault};
    for (ReportedState s : all) {
        TEST_ASSERT_NOT_NULL(Actuator::reportedStateName(s));
    }
}

// --- agreement, on the real class ---

void test_agreement_unknown_before_any_observation() {
    HeatingActuator a;
    TEST_ASSERT_EQUAL(static_cast<int>(Agreement::Unknown), static_cast<int>(a.agreement(1000)));
}

void test_unassigned_actuator_stays_inert() {
    HeatingActuator a;
    Config::DeviceConfig cfg; // no host, no channel
    TEST_ASSERT_FALSE(a.configure(cfg));
    a.tick(1.0f, true, 1000);
    TEST_ASSERT_FALSE(a.commandedOpen());
    TEST_ASSERT_FALSE(a.isConforming());
}

void test_assignment_requires_both_host_and_channel() {
    HeatingActuator a;
    Config::DeviceConfig cfg;
    std::strncpy(cfg.actuator_host, "192.168.1.10", sizeof(cfg.actuator_host) - 1);
    cfg.actuator_channel = Config::ACTUATOR_CHANNEL_UNASSIGNED;
    TEST_ASSERT_FALSE(a.configure(cfg));

    cfg.actuator_channel = 2;
    TEST_ASSERT_TRUE(a.configure(cfg));
}

void test_reassignment_discards_stale_conformance() {
    // A different channel is a different valve; nothing known about the old one
    // may carry over, least of all whether it was safe to drive.
    HeatingActuator a;
    Config::DeviceConfig cfg;
    std::strncpy(cfg.actuator_host, "192.168.1.10", sizeof(cfg.actuator_host) - 1);
    cfg.actuator_channel = 1;
    a.configure(cfg);
    cfg.actuator_channel = 2;
    a.configure(cfg);
    TEST_ASSERT_EQUAL(static_cast<int>(Actuator::Conformance::NotRead),
                      static_cast<int>(a.conformance()));
    TEST_ASSERT_FALSE(a.commandedOpen());
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_disabled_beats_everything);
    RUN_TEST(test_unassigned_falls_back_to_demand);
    RUN_TEST(test_assigned_ignores_demand_and_trusts_observation);
    RUN_TEST(test_unobserved_is_unknown_not_a_guess);
    RUN_TEST(test_disagreement_is_a_fault);
    RUN_TEST(test_every_state_has_a_name);
    RUN_TEST(test_agreement_unknown_before_any_observation);
    RUN_TEST(test_unassigned_actuator_stays_inert);
    RUN_TEST(test_assignment_requires_both_host_and_channel);
    RUN_TEST(test_reassignment_discards_stale_conformance);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
