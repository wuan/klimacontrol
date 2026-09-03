//
// Created by Andreas Würl on 06.03.26.
//

#ifndef KLIMACONTROL_STATS_H
#define KLIMACONTROL_STATS_H
#include <atomic>
#include <cstdint>

namespace Support
{
    // Indivisible copy of the four cycle-delay counters. Cross-task readers
    // SHALL go through Stats::snapshot() — see the
    // "Cross-task reads of `Support::Stats` use a snapshot accessor"
    // requirement in openspec/specs/system-architecture/spec.md.
    struct StatsSnapshot
    {
        uint64_t count;
        uint64_t average;
        uint64_t min;
        uint64_t max;
    };

    class Stats
    {
        uint64_t total = 0;
        uint64_t count = 0;
        uint64_t min_value = UINT64_MAX;
        uint64_t max_value = 0;

        // Spinlock protecting the four counters. std::atomic_flag is the
        // same primitive the project uses for deviceConfigLock /
        // restartLock (Config.h:386 / :360): lock-free on the toolchain,
        // no FreeRTOS dependency, and the critical section is strictly
        // shorter than the per-field getters' callers can observe.
        // The mutex is mutable so const getters can take/release it.
        mutable std::atomic_flag lock = ATOMIC_FLAG_INIT;

        void lockStats() const {
            while (lock.test_and_set(std::memory_order_acquire)) {
                // spin
            }
        }

        void unlockStats() const {
            lock.clear(std::memory_order_release);
        }

    public:
        void add(uint64_t value);

        // Cross-task accessor. Returns an indivisible copy of all four
        // counters observed under one acquisition of the spinlock, so
        // a single caller cannot see a mix of pre-update and post-update
        // field values.
        StatsSnapshot snapshot() const;

        // Per-field getters. Each acquires the spinlock for one read.
        // Prefer snapshot() in any cross-task context.
        uint64_t get_average() const;
        uint64_t get_min() const;
        uint64_t get_max() const;
        uint64_t get_count() const;
    };
} // Support

#endif //KLIMACONTROL_STATS_H
