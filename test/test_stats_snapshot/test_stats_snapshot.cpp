#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "support/Stats.h"
#include "unity.h"

void setUp() {}
void tearDown() {}

namespace
{
    // Sequence of values the writer inserts. Chosen so each value leaves
    // a recognisable fingerprint in min/max, and the invariants
    // min <= average <= max  and  min <= max  are easy to verify.
    constexpr uint64_t kValues[] = {50, 10, 30, 20, 40};
    constexpr size_t kValuesN = sizeof(kValues) / sizeof(kValues[0]);

    bool valueInSet(uint64_t v) {
        for (uint64_t candidate : kValues) {
            if (v == candidate) {
                return true;
            }
        }
        return false;
    }
}

// A single snapshot taken without contention must equal the per-field
// getters at that exact moment — this is the property the cross-task
// reader relies on.
void test_single_thread_snapshot_matches_getters() {
    Support::Stats stats;
    for (uint64_t v : kValues) {
        stats.add(v);
    }

    const Support::StatsSnapshot snap = stats.snapshot();

    TEST_ASSERT_EQUAL_UINT64(stats.get_count(), snap.count);
    TEST_ASSERT_EQUAL_UINT64(stats.get_average(), snap.average);
    TEST_ASSERT_EQUAL_UINT64(stats.get_min(), snap.min);
    TEST_ASSERT_EQUAL_UINT64(stats.get_max(), snap.max);

    TEST_ASSERT_EQUAL_UINT64(kValuesN, snap.count);
    TEST_ASSERT_EQUAL_UINT64(30ULL, snap.average);
    TEST_ASSERT_EQUAL_UINT64(10ULL, snap.min);
    TEST_ASSERT_EQUAL_UINT64(50ULL, snap.max);
}

// Empty Stats reports zeros in every field — same as
// get_min / get_max / get_average / get_count already do today.
void test_empty_snapshot_is_all_zeros() {
    Support::Stats stats;

    const Support::StatsSnapshot snap = stats.snapshot();

    TEST_ASSERT_EQUAL_UINT64(0ULL, snap.count);
    TEST_ASSERT_EQUAL_UINT64(0ULL, snap.average);
    TEST_ASSERT_EQUAL_UINT64(0ULL, snap.min);
    TEST_ASSERT_EQUAL_UINT64(0ULL, snap.max);
}

// Concurrent writer + reader. The reader takes snapshots in a tight
// loop; every snapshot must satisfy the identities that the writer
// could only ever have produced as a unit:
//
//   min <= max
//   count > 0  ⇒  min <= average <= max
//   min, max   ∈  set of values the writer has inserted
//
// All three are unconditionally violated by an unsynchronised read of
// the four uint64_t fields (the count/total update could go in
// between the min/max update and the count update, so a snapshot could
// observe a partial state).
void test_snapshot_invariants_under_concurrent_adds() {
    Support::Stats stats;

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<int> snapshots{0};
    std::atomic<int> violations{0};

    auto writer = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        while (!stop.load(std::memory_order_acquire)) {
            for (uint64_t v : kValues) {
                stats.add(v);
            }
        }
    };

    auto reader = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        while (!stop.load(std::memory_order_acquire)) {
            const Support::StatsSnapshot snap = stats.snapshot();
            ++snapshots;

            if (snap.min > snap.max) {
                violations.fetch_add(1, std::memory_order_relaxed);
            }
            if (snap.count > 0) {
                if (snap.average < snap.min || snap.average > snap.max) {
                    violations.fetch_add(1, std::memory_order_relaxed);
                }
                if (!valueInSet(snap.min) || !valueInSet(snap.max)) {
                    violations.fetch_add(1, std::memory_order_relaxed);
                }
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
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, violations.load(),
        "Snapshot violated one of: min > max, average outside [min,max], "
        "or min/max not drawn from the writer's insert set — a torn "
        "cross-task read.");
}

// Two writers, two readers — same invariants under higher contention.
void test_snapshot_invariants_under_two_writers_two_readers() {
    Support::Stats stats;

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<int> snapshots{0};
    std::atomic<int> violations{0};

    auto writer = [&](size_t offset) {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        while (!stop.load(std::memory_order_acquire)) {
            for (size_t j = 0; j < kValuesN; ++j) {
                stats.add(kValues[(j + offset) % kValuesN]);
            }
            std::this_thread::yield();
        }
    };

    auto reader = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        while (!stop.load(std::memory_order_acquire)) {
            const Support::StatsSnapshot snap = stats.snapshot();
            ++snapshots;
            if (snap.min > snap.max) {
                violations.fetch_add(1, std::memory_order_relaxed);
            }
            if (snap.count > 0) {
                if (snap.average < snap.min || snap.average > snap.max) {
                    violations.fetch_add(1, std::memory_order_relaxed);
                }
                if (!valueInSet(snap.min) || !valueInSet(snap.max)) {
                    violations.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    };

    std::thread w1(writer, 0);
    std::thread w2(writer, 2);
    std::thread r1(reader);
    std::thread r2(reader);

    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    stop.store(true, std::memory_order_release);

    w1.join();
    w2.join();
    r1.join();
    r2.join();

    TEST_ASSERT_GREATER_THAN(0, snapshots.load());
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, violations.load(),
        "Snapshot violated one of: min > max, average outside [min,max], "
        "or min/max not drawn from the writer's insert set — a torn "
        "cross-task read under higher contention.");
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_single_thread_snapshot_matches_getters);
    RUN_TEST(test_empty_snapshot_is_all_zeros);
    RUN_TEST(test_snapshot_invariants_under_concurrent_adds);
    RUN_TEST(test_snapshot_invariants_under_two_writers_two_readers);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
