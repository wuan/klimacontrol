#ifndef I2CSCANNER_H
#define I2CSCANNER_H

#include <cstdint>
#include <vector>

struct I2CDevice {
    uint8_t address;
    const char* knownType;  // nullptr if unknown
};

struct SensorInfo {
    const char* name;
    const uint8_t* addresses;
    uint8_t addressCount;
};

namespace I2CScanner {
    std::vector<I2CDevice> scan();
    const char* identifyAddress(uint8_t address);

    // Returns the full sensor registry
    const SensorInfo* getRegistry(size_t &count);

    // Returns list of possible sensor names for an address
    std::vector<const char*> sensorsForAddress(uint8_t address);
}

#endif // I2CSCANNER_H
