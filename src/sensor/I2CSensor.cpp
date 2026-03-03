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

    bool I2CSensor::isConnected() {
#ifdef ARDUINO
        if (!initialized) return false;

        wire.beginTransmission(i2cAddress);
        return wire.endTransmission() == 0;
#else
        return initialized;
#endif
    }

} // namespace Sensor
