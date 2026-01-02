#include "Layout.h"
#include "../color.h"
#include <cmath>
#ifdef ARDUINO
#include <esp32-hal.h>
#include <USBCDC.h>
#endif


namespace Strip {
    PixelIndex Layout::real_index(PixelIndex index) const {
        // if not 0 <= index < len(self):
        // raise IndexError("Index out of range")

        // Apply reverse first (within logical layout space)
        if (reverse) {
            index = length() - index - 1;
        }

        // Then apply dead LED offset to get physical strip index
        if (!mirror) {
            if (dead_leds > 0) {
                index += dead_leds;
            }
        } else {
            if (dead_leds < 0) {
                index += int(-dead_leds / 2);
            }
        }
        return index;
    }

    void Layout::fill(Color color) {
        strip.fill(color);
        turnOffDeadLeds();
    }

    void Layout::setPixelColor(PixelIndex pixel_index, Color color) {
        pixel_index = real_index(pixel_index);
        strip.setPixelColor(pixel_index, color);
        if (mirror) {
            strip.setPixelColor(strip.length() - pixel_index - 1, color);
        }
    }

    Color Layout::getPixelColor(PixelIndex pixel_index) const {
        return strip.getPixelColor(real_index(pixel_index));
    }

    PixelIndex Layout::length() const {
        return int((strip.length() - abs(dead_leds)) / (mirror ? 2 : 1));
    }

    void Layout::show() {
        strip.show();
    }

    void Layout::turnOffDeadLeds() {
        if (dead_leds == 0) {
            return;
        }
        if (mirror) {
            turnOffMirroredDeadLeds();
        } else {
            turnOffPlainDeadLeds();
        }
    }

    void Layout::turnOffMirroredDeadLeds() {
        if (dead_leds > 0) {
            turnOffMiddleLeds();
        } else {
            turnOffEdgeLeds();
        }
    }

    void Layout::turnOffPlainDeadLeds() {
        if (dead_leds > 0) {
            turnOffBeginningLeds();
        } else {
            turnOffEndLeds();
        }
    }

    void Layout::turnOffMiddleLeds() {
        // Turn off LEDs in the middle (positive dead, mirrored)
        PixelIndex left_side_end = length();
        PixelIndex dead_start = left_side_end;
        PixelIndex dead_end = dead_start + dead_leds;
        setRangeToBlack(dead_start, dead_end);
    }

    void Layout::turnOffEdgeLeds() {
        // Turn off LEDs at both edges (negative dead, mirrored)
        PixelIndex half_dead = abs(dead_leds / 2);
        Color black = color(0, 0, 0);
        for (PixelIndex i = 0; i < half_dead; i++) {
            strip.setPixelColor(i, black);
            strip.setPixelColor(strip.length() - i - 1, black);
        }
    }

    void Layout::turnOffBeginningLeds() {
        // Turn off LEDs at the beginning (positive dead, non-mirrored)
        setRangeToBlack(0, abs(dead_leds));
    }

    void Layout::turnOffEndLeds() {
        // Turn off LEDs at the end (negative dead, non-mirrored)
        PixelIndex start = strip.length() + dead_leds; // dead_leds is negative
        setRangeToBlack(start, strip.length());
    }

    void Layout::setRangeToBlack(PixelIndex start, PixelIndex end) {
        Color black = color(0, 0, 0);
        for (PixelIndex i = start; i < end; i++) {
            strip.setPixelColor(i, black);
        }
    }

    Layout::Layout(Strip &strip, bool reverse, bool mirror, PixelIndex dead_leds) : strip(strip), reverse(reverse),
        mirror(mirror), dead_leds(dead_leds) {
        turnOffDeadLeds(); // Clear dead LEDs on initialization
    }

    void Layout::setBrightness(uint8_t brightness) {
        strip.setBrightness(brightness);
    }
}
