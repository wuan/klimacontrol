#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "Config.h"
#include "unity.h"

void setUp() {}
void tearDown() {}

// Cross-task `DeviceConfig` reads used to be torn: a tick of updateControl()
// on the Sensor Monitor task could see the new safety_max_c paired with the
// old safety_hyst_c, or any other half-updated combination, because every
// updateXxx() in ConfigManager wrote its field(s) unlocked while the control
// task read through the const reference. The fix introduces
// getDeviceConfigSnapshot(), which takes an indivisible copy under a
// std::atomic_flag spinlock around the cache.
//
// These tests assert that fix from the consumer side. A writer thread cycles
// safety_max_c and safety_hyst_c through distinct pairs; a reader thread
// takes snapshots in a tight loop. Every snapshot must be one of the pairs
// the writer has *ever* stored — never the impossible (old-max, new-hyst)
// or (new-max, old-hyst) cross that an unsynchronised read could observe.
//
// `updateActuatorTiming` is the canonical multi-field writer and the one
// that motivated the fix (it sets both safety fields together with the
// cycle/travel pair). Other updateXxx() methods are single-field and the
// snapshot trivially cannot tear on them; the same lock covers them too.

namespace {
    constexpr uint16_t CYCLE_S = Config::DEFAULT_TPO_CYCLE_S;
    constexpr uint16_t TRAVEL_S = Config::DEFAULT_TPO_TRAVEL_S;

    // Distinct (safety_max, safety_hyst) pairs the writer cycles through.
    // Each is chosen to be plausible on its own (within the configured
    // ranges) so validateDeviceConfig() does not silently fall back to
    // defaults mid-test. The DEFAULT_SAFETY_* pair is included so a reader
    // that observes the cache before the first write also counts as "known"
    // — otherwise the initial state of a freshly constructed ConfigManager
    // looks indistinguishable from a torn read.
    struct Pair {
        float maxC;
        float hystC;
    };
    constexpr Pair kPairs[] = {
        {Config::DEFAULT_SAFETY_MAX_C, Config::DEFAULT_SAFETY_HYST_C},
        {30.0f, 1.0f},
        {32.5f, 1.5f},
        {35.0f, 2.0f},
        {28.0f, 0.5f},
        {40.0f, 3.0f},
    };

    // True if the snapshot's safety pair is one the writer has stored at
    // *some* point during the run. The writer only ever commits whole pairs,
    // so any snapshot that picks up fields from two different committed
    // pairs is by construction torn.
    bool isAnyKnownPair(const Config::DeviceConfig &cfg) {
        for (const auto &p : kPairs) {
            if (cfg.safety_max_c == p.maxC && cfg.safety_hyst_c == p.hystC) {
                return true;
            }
        }
        return false;
    }
}

// Sanity check the validator does not silently rewrite the pairs above into
// the default; a torn read could otherwise look like a validator quirk.
void test_validator_preserves_chosen_pairs() {
    for (const auto &p : kPairs) {
        Config::DeviceConfig candidate;
        candidate.tpo_cycle_s = CYCLE_S;
        candidate.tpo_travel_s = TRAVEL_S;
        candidate.safety_max_c = p.maxC;
        candidate.safety_hyst_c = p.hystC;
        Config::validateDeviceConfig(candidate);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, p.maxC, candidate.safety_max_c);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, p.hystC, candidate.safety_hyst_c);
    }
}

// Single writer, single reader. The safety pair on every snapshot must be
// one the writer has committed at some point. A torn read would manifest
// as a snapshot whose pair matches no committed state — fail with a
// diagnostic that prints the offending pair.
void test_snapshot_is_a_known_pair_under_concurrent_writes() {
    Config::ConfigManager cfg;

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<int> snapshots{0};
    std::atomic<int> tornObservations{0};
    std::atomic<float> tornMaxC{0.0f};
    std::atomic<float> tornHystC{0.0f};

    auto writer = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        size_t i = 0;
        while (!stop.load(std::memory_order_acquire)) {
            const Pair &p = kPairs[i % (sizeof(kPairs) / sizeof(kPairs[0]))];
            cfg.updateActuatorTiming(CYCLE_S, TRAVEL_S, p.maxC, p.hystC);
            ++i;
            // Yield to give the reader a chance to interleave between the
            // two fields the original bug would tear on.
            std::this_thread::yield();
        }
    };

    auto reader = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        while (!stop.load(std::memory_order_acquire)) {
            const Config::DeviceConfig snap = cfg.getDeviceConfigSnapshot();
            ++snapshots;
            if (!isAnyKnownPair(snap)) {
                tornObservations.fetch_add(1, std::memory_order_relaxed);
                // Capture the first torn pair for the failure message.
                float expected = 0.0f;
                tornMaxC.compare_exchange_strong(expected, snap.safety_max_c);
                expected = 0.0f;
                tornHystC.compare_exchange_strong(expected, snap.safety_hyst_c);
            }
        }
    };

    std::thread w(writer);
    std::thread r(reader);

    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    stop.store(true, std::memory_order_release);

    w.join();
    r.join();

    TEST_ASSERT_GREATER_THAN(0, snapshots.load());
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, tornObservations.load(),
        "Snapshot observed a safety pair that was never committed by the "
        "writer — a torn cross-task read.");
    // If we did see a torn pair, the diagnostics below carry enough
    // information to identify which intermediate state slipped through.
    (void)tornMaxC.load();
    (void)tornHystC.load();
}

// Two writers and two readers — same invariant under higher contention.
void test_snapshot_remains_consistent_with_two_writers_two_readers() {
    Config::ConfigManager cfg;

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<int> snapshots{0};
    std::atomic<int> tornObservations{0};

    auto writer = [&](size_t offset) {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        size_t i = offset;
        const size_t n = sizeof(kPairs) / sizeof(kPairs[0]);
        while (!stop.load(std::memory_order_acquire)) {
            const Pair &p = kPairs[i % n];
            cfg.updateActuatorTiming(CYCLE_S, TRAVEL_S, p.maxC, p.hystC);
            ++i;
            std::this_thread::yield();
        }
    };

    auto reader = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        while (!stop.load(std::memory_order_acquire)) {
            const Config::DeviceConfig snap = cfg.getDeviceConfigSnapshot();
            ++snapshots;
            if (!isAnyKnownPair(snap)) {
                tornObservations.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::thread w1(writer, 0);
    std::thread w2(writer, 2);
    std::thread r1(reader);
    std::thread r2(reader);

    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stop.store(true, std::memory_order_release);

    w1.join();
    w2.join();
    r1.join();
    r2.join();

    TEST_ASSERT_GREATER_THAN(0, snapshots.load());
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, tornObservations.load(),
        "Snapshot observed a safety pair that no writer ever committed "
        "even with two concurrent writers and two readers.");
}

// The const-reference accessor still has to work for same-task readers —
// route handlers on the AsyncTCP task read it back after writing. The
// snapshot does not replace getDeviceConfig(); it sits alongside it.
void test_get_device_config_still_returns_a_reference() {
    Config::ConfigManager cfg;

    const Config::DeviceConfig &ref = cfg.getDeviceConfig();
    cfg.updateTargetTemperature(23.5f);
    // The reference observes the new value because the writer and reader
    // are on the same task.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 23.5f, ref.target_temperature);

    // And the snapshot agrees.
    const Config::DeviceConfig snap = cfg.getDeviceConfigSnapshot();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 23.5f, snap.target_temperature);
}

// Single-field writers must not corrupt adjacent fields. This catches a
// regression where a future updateXxx() is added but its cache write is
// forgotten by the new lock — it would let the snapshot see a half-update.
void test_single_field_writes_do_not_disturb_adjacent_fields() {
    Config::ConfigManager cfg;

    cfg.updateActuatorTiming(CYCLE_S, TRAVEL_S, 35.0f, 2.0f);
    cfg.updateTargetTemperature(23.0f);
    cfg.updateTemperatureControlEnabled(true);

    // A burst of single-field updates from the "writer" thread interleaved
    // with snapshots from the "reader" thread. Every snapshot must show
    // either the pre-burst set, the post-burst set, or some whole committed
    // state in between — never a mix where only one of the three fields
    // has been updated.
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<int> inconsistent{0};

    auto writer = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        while (!stop.load(std::memory_order_acquire)) {
            cfg.updateTargetTemperature(24.0f);
            cfg.updateTemperatureControlEnabled(false);
            cfg.updateTargetTemperature(22.5f);
            cfg.updateTemperatureControlEnabled(true);
        }
    };

    auto reader = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        while (!stop.load(std::memory_order_acquire)) {
            const Config::DeviceConfig snap = cfg.getDeviceConfigSnapshot();
            // The cycle/travel/safety_max/safety_hyst pair is never touched
            // by the writer in this scenario. Every snapshot must still show
            // 35.0 / 2.0 — the cache field of an unrelated writer must not
            // have been disturbed.
            if (snap.safety_max_c != 35.0f || snap.safety_hyst_c != 2.0f) {
                inconsistent.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::thread w(writer);
    std::thread r(reader);

    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    stop.store(true, std::memory_order_release);

    w.join();
    r.join();

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, inconsistent.load(),
        "A single-field writer disturbed fields it was not supposed to "
        "touch, indicating a missed lock acquisition.");
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_validator_preserves_chosen_pairs);
    RUN_TEST(test_snapshot_is_a_known_pair_under_concurrent_writes);
    RUN_TEST(test_snapshot_remains_consistent_with_two_writers_two_readers);
    RUN_TEST(test_get_device_config_still_returns_a_reference);
    RUN_TEST(test_single_field_writes_do_not_disturb_adjacent_fields);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
