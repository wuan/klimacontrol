#include "I2CScanner.h"

#ifdef ARDUINO
#include <Wire.h>
#include <Arduino.h>
#endif

namespace I2CScanner {

    const char* identifyAddress(uint8_t address) {
        switch (address) {
            case 0x44: return "SHT4x";
            case 0x45: return "SHT4x (alt)";
            case 0x76: return "BME680";
            case 0x77: return "BME680 (alt)";
            default: return nullptr;
        }
    }

    std::vector<I2CDevice> scan() {
        std::vector<I2CDevice> devices;

#ifdef ARDUINO
        Serial.println("I2CScanner: Scanning Wire bus...");

        for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
            Wire.beginTransmission(addr);
            uint8_t error = Wire.endTransmission();

            if (error == 0) {
                const char* type = identifyAddress(addr);
                devices.push_back({addr, type});
                Serial.printf("I2CScanner: Found device at 0x%02X (%s)\n",
                              addr, type ? type : "unknown");
            }
        }

        Serial.printf("I2CScanner: Scan complete, found %u devices\n", devices.size());
#endif

        return devices;
    }

} // namespace I2CScanner
