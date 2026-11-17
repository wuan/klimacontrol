// Board-variant initialisation for the IDF build.
//
// The Arduino framework environment compiles the board variant's variant.cpp,
// which drives the QT Py's NEOPIXEL_POWER pin high. The Arduino component built
// from source under ESP-IDF does not include variants/<board>/variant.cpp, so
// the core's empty weak initVariant() runs instead and the onboard NeoPixel
// stays unpowered. This file supplies the strong override for the IDF
// environment only; the Arduino environment already has the variant's own
// definition.
#ifdef KLIMA_IDF_BUILD

#include <Arduino.h>

extern "C" void initVariant(void) {
    pinMode(NEOPIXEL_POWER, OUTPUT);
    digitalWrite(NEOPIXEL_POWER, HIGH);
}

#endif // KLIMA_IDF_BUILD
