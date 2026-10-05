#pragma once

// Minimal Arduino compatibility shim for the host-side display renderer.
// Only what src/display/EPaperDisplay.cpp and Adafruit_GFX actually use;
// everything is a no-op because the mock draws into an in-memory framebuffer.
// The pin constants mirror the board variant so DisplayPins.h's
// static_asserts still guard the pin map.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "WString.h"

#define PROGMEM

// Arduino trig helpers used by Adafruit_GFX.
inline float radians(float deg) {
    return deg * static_cast<float>(M_PI) / 180.0f;
}
inline float degrees(float rad) {
    return rad * 180.0f / static_cast<float>(M_PI);
}

constexpr int HIGH = 1;
constexpr int LOW = 0;
constexpr int INPUT = 0;
constexpr int OUTPUT = 1;

// adafruit_qtpy_esp32s2 variant pins (see DisplayPins.h for the panel map).
constexpr int MISO = 37;
constexpr int SCK = 36;
constexpr int MOSI = 35;
constexpr int A0 = 18;
constexpr int A1 = 17;
constexpr int A2 = 9;
constexpr int A3 = 8;
constexpr int SDA1 = 41;
constexpr int SCL1 = 40;
constexpr int PIN_NEOPIXEL = 39;
constexpr int NEOPIXEL_POWER = 38;

inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) {
    return HIGH;
}
inline void delay(unsigned long) {}
inline void yield() {}
inline unsigned long millis() {
    static unsigned long ticks = 0;
    ticks += 100;
    return ticks;
}

#if defined(__APPLE__)
// strlcpy is in the C library.
#elif defined(__GLIBC__) && defined(__GLIBC_PREREQ__)
#if !__GLIBC_PREREQ(2, 38)
inline size_t strlcpy(char* dst, const char* src, size_t size) {
    const size_t len = strlen(src);
    if (size != 0) {
        const size_t n = len < size - 1 ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}
#endif
#else
inline size_t strlcpy(char* dst, const char* src, size_t size) {
    const size_t len = strlen(src);
    if (size != 0) {
        const size_t n = len < size - 1 ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}
#endif
