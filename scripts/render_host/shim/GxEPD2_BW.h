#pragma once

// Host stand-in for GxEPD2_BW. Derives from the real Adafruit_GFX so every
// draw call — text, fonts, primitives — runs the firmware's own rendering
// path bit for bit, landing in an in-memory framebuffer instead of over SPI.
// The paged draw loop is collapsed to a single pass over the full panel,
// which is pixel-identical to the union of GxEPD2's page windows.

#include "Adafruit_GFX.h"
#include "Arduino.h"

#define GxEPD_BLACK 0x0000
#define GxEPD_WHITE 0xFFFF

class GxEPD2_154_D67 {
public:
    static constexpr int16_t WIDTH = 200;
    static constexpr int16_t HEIGHT = 200;
    GxEPD2_154_D67(int16_t, int16_t, int16_t, int16_t) {}
};

// Invoked when a paged draw completes: the full framebuffer, 1 byte per
// pixel, 1 = white. Defined by the harness binary.
extern void render_host_frame(const uint8_t* fb, int16_t w, int16_t h);

template <typename PANEL, uint16_t PAGE_HEIGHT> class GxEPD2_BW : public Adafruit_GFX {
public:
    explicit GxEPD2_BW(const PANEL&) : Adafruit_GFX(PANEL::WIDTH, PANEL::HEIGHT) {}

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || x >= PANEL::WIDTH || y < 0 || y >= PANEL::HEIGHT) {
            return;
        }
        fb[static_cast<size_t>(y) * PANEL::WIDTH + x] = (color == GxEPD_WHITE) ? 1 : 0;
    }

    bool init(uint32_t, bool, uint16_t, bool) { return true; }
    void firstPage() {}
    bool nextPage() {
        render_host_frame(fb, PANEL::WIDTH, PANEL::HEIGHT);
        return false;
    }
    void setFullWindow() {}
    void setPartialWindow(int16_t, int16_t, int16_t, int16_t) {}
    void hibernate() {}
    void powerOff() {}

private:
    uint8_t fb[PANEL::WIDTH * PANEL::HEIGHT] = {};
};
