#ifndef UNTITLED_MOCKSTRIP_H
#define UNTITLED_MOCKSTRIP_H

#include <vector>

#include "color.h"

// Mock strip for testing
class MockStrip : public Strip::Strip {
private:
    std::vector<::Strip::Color> pixels;
    ::Strip::PixelIndex pixel_count;

public:
    MockStrip(::Strip::PixelIndex count) : pixel_count(count) {
        pixels.resize(count, 0x000000);
    }

    void fill(::Strip::Color c) override {
        for (::Strip::PixelIndex i = 0; i < pixel_count; i++) {
            pixels[i] = c;
        }
    }

    void setPixelColor(::Strip::PixelIndex pixel_index, ::Strip::Color col) override {
        if (pixel_index >= 0 && pixel_index < pixel_count) {
            pixels[pixel_index] = col;
        }
    }

    ::Strip::Color getPixelColor(::Strip::PixelIndex pixel_index) const override {
        if (pixel_index >= 0 && pixel_index < pixel_count) {
            return pixels[pixel_index];
        }
        return 0;
    }

    ::Strip::PixelIndex length() const override {
        return pixel_count;
    }

    void show() override {
        // Mock implementation
    }

    void setBrightness(uint8_t brightness) override {
        // Mock implementation
    }
};
#endif //UNTITLED_MOCKSTRIP_H