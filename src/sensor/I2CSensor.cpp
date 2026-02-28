#include "I2CSensor.h"

#ifdef ARDUINO
#include <Wire.h>
#endif

namespace Sensor {
    I2CSensor::I2CSensor(uint8_t address) {
        i2cAddress = address;
        initialized = false;
    }

    bool I2CSensor::isConnected() {
#ifdef ARDUINO
        if (!initialized) return false;

        Wire1.beginTransmission(i2cAddress);
        return Wire1.endTransmission() == 0;
#else
        return initialized;
#endif
    }

} // namespace Sensor
