#ifndef LEDZ_WS2812_H
#define LEDZ_WS2812_H
#include <cstdint>
#include <memory>

#ifdef ARDUINO
#include "Adafruit_NeoPixel.h"
#endif
#include "Strip.h"

namespace Strip {
    class Base : public Strip {
#ifdef ARDUINO
        std::unique_ptr<Adafruit_NeoPixel> strip;
        std::unique_ptr<Color[]> colors;
#endif
    public:
        Base(Pin pin, unsigned short length);

        void fill(Color c) override;

        void setPixelColor(PixelIndex pixel_index, Color color) override;

        Color getPixelColor(PixelIndex pixel_index) const override;

        void show() override;

        PixelIndex length() const override;

        void setBrightness(uint8_t brightness) override;

    };
}

#endif //LEDZ_WS2812_H