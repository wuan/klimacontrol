#include "I2CScanner.h"
#include "sensor/SHT4x.h"
#include "sensor/BME680.h"
#include "sensor/SGP40.h"

#ifdef ARDUINO
#include <Wire.h>
#include <Arduino.h>
#endif

namespace I2CScanner {

    static const SensorInfo REGISTRY[] = {
        {Sensor::SHT4x::type(), Sensor::SHT4x::addresses(), Sensor::SHT4x::addressCount()},
        {Sensor::BME680::type(), Sensor::BME680::addresses(), Sensor::BME680::addressCount()},
        {Sensor::SGP40::type(), Sensor::SGP40::addresses(), Sensor::SGP40::addressCount()},
    };
    static constexpr size_t REGISTRY_SIZE = sizeof(REGISTRY) / sizeof(REGISTRY[0]);

    const SensorInfo* getRegistry(size_t &count) {
        count = REGISTRY_SIZE;
        return REGISTRY;
    }

    std::vector<const char*> sensorsForAddress(uint8_t address) {
        std::vector<const char*> result;
        for (size_t i = 0; i < REGISTRY_SIZE; i++) {
            for (uint8_t j = 0; j < REGISTRY[i].addressCount; j++) {
                if (REGISTRY[i].addresses[j] == address) {
                    result.push_back(REGISTRY[i].name);
                    break;
                }
            }
        }
        return result;
    }

    const char* identifyAddress(uint8_t address) {
        auto names = sensorsForAddress(address);
        return names.empty() ? nullptr : names[0];
    }

    std::vector<I2CDevice> scan() {
        std::vector<I2CDevice> devices;

#ifdef ARDUINO
        Serial.println("I2CScanner: Scanning Wire bus...");

        for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
            Wire1.beginTransmission(addr);
            uint8_t error = Wire1.endTransmission();

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
