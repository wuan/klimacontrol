#include "I2CScanner.h"
#include "sensor/SHT4x.h"
#include "sensor/BME680.h"
#include "sensor/SGP40.h"
#include "sensor/BMP3xx.h"
#include "sensor/SCD4x.h"
#include "sensor/TSL2591.h"
#include "sensor/PM25.h"
#include "sensor/VEML7700.h"
#include "sensor/DPS310.h"
#include "sensor/BH1750.h"

#ifdef ARDUINO
#include <Wire.h>
#include <Arduino.h>
#endif

namespace I2CScanner {

    static const SensorInfo REGISTRY[] = {
        {Sensor::SHT4x::type(), Sensor::SHT4x::addresses(), Sensor::SHT4x::addressCount()},
        {Sensor::BME680::type(), Sensor::BME680::addresses(), Sensor::BME680::addressCount()},
        {Sensor::SGP40::type(), Sensor::SGP40::addresses(), Sensor::SGP40::addressCount()},
        {Sensor::BMP3xx::type(), Sensor::BMP3xx::addresses(), Sensor::BMP3xx::addressCount()},
        {Sensor::SCD4x::type(), Sensor::SCD4x::addresses(), Sensor::SCD4x::addressCount()},
        {Sensor::TSL2591::type(), Sensor::TSL2591::addresses(), Sensor::TSL2591::addressCount()},
        {Sensor::PM25::type(), Sensor::PM25::addresses(), Sensor::PM25::addressCount()},
        {Sensor::VEML7700::type(), Sensor::VEML7700::addresses(), Sensor::VEML7700::addressCount()},
        {Sensor::DPS310::type(), Sensor::DPS310::addresses(), Sensor::DPS310::addressCount()},
        {Sensor::BH1750Sensor::type(), Sensor::BH1750Sensor::addresses(), Sensor::BH1750Sensor::addressCount()},
    };
    static constexpr size_t REGISTRY_COUNT = sizeof(REGISTRY) / sizeof(REGISTRY[0]);

    const SensorInfo* getRegistry(size_t &count) {
        count = REGISTRY_COUNT;
        return REGISTRY;
    }

    std::vector<const char*> sensorsForAddress(uint8_t address) {
        std::vector<const char*> result;
        for (size_t i = 0; i < REGISTRY_COUNT; i++) {
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
                Serial.printf("I2CScanner: Found device at 0x%02X (%s)\r\n",
                              addr, type ? type : "unknown");
            }
        }

        Serial.printf("I2CScanner: Scan complete, found %u devices\r\n", devices.size());
#endif

        return devices;
    }

} // namespace I2CScanner
