#ifndef LEDZ_RAINBOW_H
#define LEDZ_RAINBOW_H
#include "Show.h"
#include "strip/Strip.h"

namespace Show {
    class Rainbow : public Show {
    public:
        Rainbow();

        void execute(Strip::Strip &strip, Iteration iteration) override;
    };
}


#endif //LEDZ_RAINBOW_H