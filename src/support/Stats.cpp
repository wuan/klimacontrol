//
// Created by Andreas Würl on 06.03.26.
//

#include "Stats.h"

namespace Support
{
    void Stats::add(uint64_t value)
    {
        lockStats();
        total += value;
        count++;
        if (value < min_value) min_value = value;
        if (value > max_value) max_value = value;
        unlockStats();
    }

    StatsSnapshot Stats::snapshot() const
    {
        lockStats();
        StatsSnapshot snap{};
        snap.count = count;
        if (count == 0) {
            // Matches get_min() / get_max() / get_average()'s zero-sample
            // contract: a freshly created Stats reports 0 in every field.
            snap.average = 0;
            snap.min = 0;
            snap.max = 0;
        } else {
            snap.average = total / count;
            snap.min = min_value;
            snap.max = max_value;
        }
        unlockStats();
        return snap;
    }

    uint64_t Stats::get_average() const
    {
        lockStats();
        uint64_t result = (count == 0) ? 0 : total / count;
        unlockStats();
        return result;
    }

    uint64_t Stats::get_min() const
    {
        lockStats();
        uint64_t result = (count == 0) ? 0 : min_value;
        unlockStats();
        return result;
    }

    uint64_t Stats::get_max() const
    {
        lockStats();
        uint64_t result = max_value;
        unlockStats();
        return result;
    }

    uint64_t Stats::get_count() const
    {
        lockStats();
        uint64_t result = count;
        unlockStats();
        return result;
    }
} // Support
