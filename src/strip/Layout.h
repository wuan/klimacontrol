#ifndef LEDZ_LAYOUT_H
#define LEDZ_LAYOUT_H
#include "Strip.h"

namespace Strip {
    class Layout : public Strip {
        Strip &strip;
        bool reverse;
        bool mirror;
        PixelIndex dead_leds;

        PixelIndex real_index(PixelIndex index) const;

        // Dead LED clearing methods
        void turnOffDeadLeds();

        void turnOffMirroredDeadLeds();

        void turnOffPlainDeadLeds();

        void turnOffMiddleLeds();

        void turnOffEdgeLeds();

        void turnOffBeginningLeds();

        void turnOffEndLeds();

        void setRangeToBlack(PixelIndex start, PixelIndex end);

    public:
        Layout(Strip &strip, bool reverse = false, bool mirror = false, PixelIndex dead_leds = 0);

        void fill(Color color) override;

        void setPixelColor(PixelIndex pixel_index, Color color) override;

        Color getPixelColor(PixelIndex pixel_index) const override;

        PixelIndex length() const override;

        void show() override;

        void setBrightness(uint8_t brightness) override;
    };
}

#endif //LEDZ_LAYOUT_H
