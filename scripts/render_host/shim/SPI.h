#pragma once

// No-op SPI bus for the host renderer.

#include "Arduino.h"

class SPIClass {
public:
    void begin(int, int, int, int) {}
};

inline SPIClass SPI;
