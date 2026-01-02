#include "color.h"
#include "Rainbow.h"

namespace Show {
    Rainbow::Rainbow() {
    }

    void Rainbow::execute(Strip::Strip &strip, Iteration iteration) {
        for (Strip::PixelIndex index = 0; index < strip.length(); index++) {
            auto color = wheel((iteration + index) % 255);

            strip.setPixelColor(index, color);
        }
    }
}