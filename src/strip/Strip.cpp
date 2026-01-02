#include "Strip.h"

namespace Strip {
    void Strip::setPixelColor(PixelIndex pixel_index, Color color) {
        // Default implementation - can be overridden by subclasses
    }

    Color Strip::getPixelColor(PixelIndex pixel_index) const {
        // Default implementation - can be overridden by subclasses
        return 0;
    }
}