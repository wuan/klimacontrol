#ifndef KLIMACONTROL_DISPLAY_DISPLAYPINS_H
#define KLIMACONTROL_DISPLAY_DISPLAYPINS_H

#ifdef ARDUINO

#include <Arduino.h>
#include <SPI.h>

// Pin map for the Waveshare 1.54" V2 e-paper module (SSD1681, 200x200).
//
// See docs/EINK_DISPLAY_WIRING.md for the hardware-side instructions: which
// module to buy, the ribbon colour map, decoupling, and troubleshooting. That
// document restates this pin map; change both together.
//
// Deliberately compile-time, not runtime-configurable. Validating user-supplied
// GPIO against the STEMMA QT pins, the NeoPixel pins, the USB pins and the
// strapping pins is a lot of surface area, and a bad value can break the very
// web UI needed to correct it. The board is fixed (`board =
// adafruit_qtpy_esp32s2`), so these are a board property, not a preference.
//
// Values verified against
//   framework-arduinoespressif32/variants/adafruit_qtpy_esp32s2/pins_arduino.h
//
//   Panel   GPIO  QT Py pad
//   CLK     36    SCK
//   DIN     35    MO
//   CS      18    A0
//   DC       8    A3
//   RST      9    A2
//   BUSY    17    A1     (input, active HIGH)
//   VCC      -    3V
//   GND      -    GND
//
// WHY CS IS NOT ON GPIO37 (the MI pad), even though the panel is write-only and
// that pad carries no signal:
//
//   GxEPD2_EPD::init() sets `pinMode(_cs, OUTPUT)` and then calls
//   `_pSPIx->begin()` (GxEPD2_EPD.cpp:59-66). SPIClass::begin() with no
//   arguments resolves MISO to the variant default and calls
//   `spiAttachMISO()`, which on ESP32-S2 FSPI rewrites a negative pin to 37 and
//   executes `pinMode(37, INPUT)` (esp32-hal-spi.c:203-232). A chip select on
//   GPIO37 is therefore reconfigured to an input immediately after being set up
//   as an output, and the panel never sees an assertion. There is no way to
//   suppress the MISO attach through the Arduino SPI API.
//
// GPIO37 is left electrically unconnected; SPI claiming it as an unread input
// is harmless. Flash and PSRAM live on GPIO27-32 and are not broken out.
namespace DisplayPins {

    constexpr int SCK = 36;  // panel CLK
    constexpr int MOSI = 35; // panel DIN
    constexpr int CS = 18;   // A0
    constexpr int DC = 8;    // A3
    constexpr int RST = 9;   // A2
    constexpr int BUSY = 17; // A1

    // Guard the pin map against a variant-file change or a well-meaning
    // "reclaim the unused MI pad" edit. These must fail the build rather than
    // ship as a display that silently never draws.
    static_assert(CS != MISO, "CS must not be on the MISO pin - SPI.begin() reclaims it as an input");
    static_assert(CS != SCK && CS != MOSI, "CS must not collide with the SPI bus");
    static_assert(SCK == ::SCK, "DisplayPins::SCK diverged from the board variant");
    static_assert(MOSI == ::MOSI, "DisplayPins::MOSI diverged from the board variant");
    static_assert(CS == A0, "DisplayPins::CS diverged from the board variant (expected A0)");
    static_assert(DC == A3, "DisplayPins::DC diverged from the board variant (expected A3)");
    static_assert(RST == A2, "DisplayPins::RST diverged from the board variant (expected A2)");
    static_assert(BUSY == A1, "DisplayPins::BUSY diverged from the board variant (expected A1)");

    // Must not collide with the STEMMA QT sensor bus or the status LED.
    static_assert(CS != SDA1 && DC != SDA1 && RST != SDA1 && BUSY != SDA1, "collides with STEMMA QT SDA1");
    static_assert(CS != SCL1 && DC != SCL1 && RST != SCL1 && BUSY != SCL1, "collides with STEMMA QT SCL1");
    static_assert(CS != PIN_NEOPIXEL && DC != PIN_NEOPIXEL && RST != PIN_NEOPIXEL && BUSY != PIN_NEOPIXEL,
                  "collides with the NeoPixel data pin");
    static_assert(CS != NEOPIXEL_POWER && DC != NEOPIXEL_POWER && RST != NEOPIXEL_POWER && BUSY != NEOPIXEL_POWER,
                  "collides with the NeoPixel power pin");

} // namespace DisplayPins

#endif // ARDUINO

#endif // KLIMACONTROL_DISPLAY_DISPLAYPINS_H
