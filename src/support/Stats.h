//
// Created by Andreas Würl on 06.03.26.
//

#ifndef KLIMACONTROL_STATS_H
#define KLIMACONTROL_STATS_H
#include <cstdint>

namespace Support
{
    class Stats
    {
        uint64_t total = 0;
        uint64_t count = 0;
        uint64_t min_value = UINT64_MAX;
        uint64_t max_value = 0;

    public:
        void add(uint64_t value);
        uint64_t get_average() const;
        uint64_t get_min() const;
        uint64_t get_max() const;
        uint64_t get_count() const;
    };
} // Support

#endif //KLIMACONTROL_STATS_H