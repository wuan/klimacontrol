#include "I2CSensor.h"

namespace Sensor {

#ifdef ARDUINO
    I2CSensor::I2CSensor(uint8_t address, TwoWire &wire) : wire(wire) {
        i2cAddress = address;
        initialized = false;
    }
#else
    I2CSensor::I2CSensor(uint8_t address) {
        i2cAddress = address;
        initialized = false;
    }
#endif

} // namespace Sensor
