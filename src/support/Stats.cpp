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
    }

    uint64_t Stats::get_average() const
    {
        return total / count;
    }
} // Support