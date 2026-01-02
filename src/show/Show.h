#ifndef LEDZ_SHOW_H
#define LEDZ_SHOW_H

#include <memory>
#include "strip/Strip.h"

namespace Show {
    typedef uint64_t Iteration;

    class Show {
    public:
        virtual ~Show() = default;

        virtual void execute(Strip::Strip &strip, Iteration iteration) = 0;

        /**
         * Check if the show has reached a static state (no more animation)
         * Used for power save mode - when true, the LED task can reduce update frequency
         * @return true if display is static, false if still animating (default)
         */
        virtual bool isComplete() const { return false; }
    };
}
#endif //LEDZ_SHOW_H