//
// Created by Andreas Würl on 06.03.26.
//

#include "Stats.h"

namespace Support
{
    void Stats::add(uint64_t value)
    {
        total += value;
        count++;
        if (value < min_value) min_value = value;
        if (value > max_value) max_value = value;
    }

    uint64_t Stats::get_average() const
    {
        if (count == 0) return 0;
        return total / count;
    }

    uint64_t Stats::get_min() const
    {
        if (count == 0) return 0;
        return min_value;
    }

    uint64_t Stats::get_max() const
    {
        return max_value;
    }

    uint64_t Stats::get_count() const
    {
        return count;
    }
} // Support
