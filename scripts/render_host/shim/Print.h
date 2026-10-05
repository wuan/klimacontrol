#pragma once

// Minimal stand-in for Arduino's Print base class: just the overloads the
// display code reaches through Adafruit_GFX.

#include <cstdint>
#include <cstdio>
#include <cstring>

class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t* buffer, size_t size) {
        size_t n = 0;
        for (size_t i = 0; i < size; ++i) {
            n += write(buffer[i]);
        }
        return n;
    }
    size_t write(const char* str) {
        return str == nullptr ? 0 : write(reinterpret_cast<const uint8_t*>(str), strlen(str));
    }

    size_t print(const char* str) { return write(str); }
    size_t print(char c) { return write(static_cast<uint8_t>(c)); }
    size_t print(int v) { return printNumber("%d", v); }
    size_t print(unsigned int v) { return printNumber("%u", v); }
    size_t print(long v) { return printNumber("%ld", v); }
    size_t print(unsigned long v) { return printNumber("%lu", v); }
    size_t print(double v, int digits = 2) {
        char fmt[8];
        snprintf(fmt, sizeof(fmt), "%%.%df", digits);
        return printNumber(fmt, v);
    }

    size_t println() { return write("\r\n"); }
    size_t println(const char* str) { return print(str) + println(); }

private:
    template <typename T> size_t printNumber(const char* fmt, T v) {
        char buf[40];
        snprintf(buf, sizeof(buf), fmt, v);
        return write(buf);
    }
};
