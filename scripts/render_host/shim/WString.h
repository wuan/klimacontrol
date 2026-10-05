#pragma once

// Minimal stand-in for Arduino's WString, just what Adafruit_GFX's String
// overloads touch. The flash-string helper only needs to exist as a type
// for pointer casts.

#include <string>

class __FlashStringHelper;

class String {
public:
    String() = default;
    String(const char* s) : value(s ? s : "") {}
    String(const std::string& s) : value(s) {}

    unsigned int length() const { return static_cast<unsigned int>(value.length()); }
    const char* c_str() const { return value.c_str(); }
    char charAt(unsigned int i) const { return value[i]; }

private:
    std::string value;
};
