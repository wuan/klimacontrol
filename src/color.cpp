
#include "strip/Strip.h"

Strip::Color wheel(unsigned char wheel_pos) {
    if (wheel_pos > 254) {
        wheel_pos = 254; // Safeguard
    }

    if (wheel_pos < 85) {
        // Green -> Red
        return ((wheel_pos * 3) << 16) + ((255 - wheel_pos * 3) << 8);
    }
    if (wheel_pos < 170) {
        // Red -> Blue
        wheel_pos -= 85;
        return ((255 - wheel_pos * 3) << 16) + wheel_pos * 3;
    }
    wheel_pos -= 170;
    return ((wheel_pos * 3) << 8) + (255 - wheel_pos * 3);
}

Strip::Color color(Strip::ColorComponent red, Strip::ColorComponent green, Strip::ColorComponent blue) {
    return (red << 16) + (green << 8) + blue;
}

Strip::ColorComponent red(Strip::Color color) {
    return (color >> 16) & 0xFF;
}

Strip::ColorComponent green(Strip::Color color) {
    return (color >> 8) & 0xFF;
}

Strip::ColorComponent blue(Strip::Color color) {
    return color & 0xFF;
}